/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_JIT_XLA_COMPILATION_CACHE_H_
#define TENSORFLOW_COMPILER_JIT_XLA_COMPILATION_CACHE_H_

#include "tensorflow/compiler/tf2xla/xla_compiler.h"
#include "tensorflow/compiler/tf2xla/xla_context.h"
#include "tensorflow/compiler/xla/client/local_client.h"
#include "tensorflow/core/common_runtime/device.h"
#include "tensorflow/core/common_runtime/device_mgr.h"
#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/lib/core/threadpool.h"
#include "tensorflow/core/platform/mutex.h"
#include "tensorflow/core/platform/thread_annotations.h"

namespace tensorflow {

// Struct that represents a possibly-absent Tensor.
struct OptionalTensor {
  bool present = false;  // Is the tensor present?
  Tensor value;          // If present, what is the Tensor's value?
};

// The XlaCompilationCache class caches the results of the XlaCompiler class,
// which converts a Tensorflow graph into a compiled XLA compilation.
//
// Since XLA computations must have static shapes, the cache generates a new
// XLA computation for each new set of input shapes.
//
// Currently no cache eviction policy is implemented and the cache grows without
// bound.
class XlaCompilationCache : public ResourceBase {
 public:
  explicit XlaCompilationCache(const XlaCompiler::Options& options);
  ~XlaCompilationCache() override;

  // Compiles a function into a XlaCompiler::CompilationResult that can be used
  // to execute an XLA Computation. Compilation results are cached.
  // `function` is the name of a Tensorflow function to compile.
  // `num_constant_args` is the number of compile-time constant arguments to
  // `function`. `variable_args` is a snapshot of the current values of the
  // resource variable arguments to `function`; uninitialized variables are
  // represented by an absent OptionalTensor.
  // The result of compilation is written to `*compilation_result`, which must
  // be non-null. If `executable` is non-null, also builds an
  // xla::LocalExecutable and sets `executable to point to it. The resulting
  // executable pointer may be null if the computation has no non-constant
  // outputs.
  Status Compile(const NameAttrList& function, int num_constant_args,
                 const std::vector<OptionalTensor>& variable_args,
                 OpKernelContext* ctx,
                 const XlaCompiler::CompilationResult** compilation_result,
                 xla::LocalExecutable** executable);

  xla::Client* client() const { return compiler_.client(); }

  string DebugString() override;

 private:
  XlaCompiler compiler_;
  std::unique_ptr<FunctionLibraryRuntime> function_library_runtime_;

  // Describes the types, shapes and any compile-time constant arguments
  // to a kernel. Key that uniquely identifies a compilation output.
  struct Signature {
    string name;

    std::vector<std::pair<DataType, TensorShape>> arg_types;

    // List of Tensor values for compile-time constant arguments to the
    // compilation, ordered by argument number. Tensors must be in host memory.
    std::vector<Tensor> arg_values;

    bool operator==(const Signature& other) const;

    struct Hash {
      uint64 operator()(const Signature& signature) const;
    };
  };
  static string SignatureDebugString(const Signature& sig);

  // Builds the signature for a compilation.
  Status BuildSignature(const NameAttrList& function, int num_constant_args,
                        const std::vector<OptionalTensor>& variable_args,
                        OpKernelContext* ctx, Signature* signature);

  // The value associated with a cache entry.
  struct Entry {
    mutex mu;

    // Have we tried compiling this entry?
    bool compiled = false;

    // Did compilation succeed?
    Status compilation_status GUARDED_BY(mu);

    // Output of the XlaCompiler.
    XlaCompiler::CompilationResult compilation_result GUARDED_BY(mu);

    // The XLA executable compiled from <computation>. May be null if no
    // executable has been built.
    std::unique_ptr<xla::LocalExecutable> executable GUARDED_BY(mu);
  };

  mutex mu_;
  std::unordered_map<Signature, std::unique_ptr<Entry>, Signature::Hash> cache_
      GUARDED_BY(mu_);

  TF_DISALLOW_COPY_AND_ASSIGN(XlaCompilationCache);
};

}  // namespace tensorflow

#endif  // TENSORFLOW_COMPILER_JIT_XLA_COMPILATION_CACHE_H_
