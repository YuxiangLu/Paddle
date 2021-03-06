/* Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/any.hpp>

#include "paddle/fluid/extension/include/dll_decl.h"
#include "paddle/fluid/extension/include/tensor.h"

/**
 * Op Meta Info Related Define.
 *
 * Used to maintain operator core information.
 *
 */

namespace paddle {
namespace framework {
class PD_DLL_DECL OpMetaInfoHelper;
}  // namespace framework

using Tensor = paddle::Tensor;

#define PD_DISABLE_COPY_AND_ASSIGN(classname)      \
 private:                                          \
  classname(const classname&) = delete;            \
  classname(classname&&) = delete;                 \
  classname& operator=(const classname&) = delete; \
  classname& operator=(classname&&) = delete

#if defined _WIN32
#define HANDLE_THE_ERROR try {
#define END_HANDLE_THE_ERROR            \
  }                                     \
  catch (const std::exception& e) {     \
    std::cerr << e.what() << std::endl; \
    throw e;                            \
  }
#else
#define HANDLE_THE_ERROR
#define END_HANDLE_THE_ERROR
#endif

#define PD_THROW(err_msg)              \
  do {                                 \
    HANDLE_THE_ERROR                   \
    throw std::runtime_error(err_msg); \
    END_HANDLE_THE_ERROR               \
  } while (0)

///////////////// Util Define and Function ////////////////

inline std::string Grad(const std::string& var_name) {
  std::string result;
  result.reserve(var_name.size() + 5U);
  result += var_name;
  result += "@GRAD";
  return result;
}

////////////////////// Kernel Function (PD_KERNEL) ////////////////////////

// Record Op kernel core function
using KernelFunc = std::vector<Tensor> (*)(std::vector<Tensor> inputs,
                                           std::vector<boost::any> attrs);

#define PD_SPECIALIZE_ComputeCallHelper(attr_type)                          \
  template <typename... Tail>                                               \
  struct ComputeCallHelper<attr_type, Tail...> {                            \
    template <int in_idx, int attr_idx, typename... PreviousArgs>           \
    static Return Compute(std::vector<Tensor> inputs,                       \
                          std::vector<boost::any> attrs,                    \
                          const PreviousArgs&... pargs) {                   \
      try {                                                                 \
        attr_type arg = boost::any_cast<attr_type>(attrs[attr_idx]);        \
        return ComputeCallHelper<Tail...>::template Compute<in_idx,         \
                                                            attr_idx + 1>(  \
            inputs, attrs, pargs..., arg);                                  \
      } catch (boost::bad_any_cast&) {                                      \
        PD_THROW(                                                           \
            "Attribute cast error in custom operator. Expected " #attr_type \
            " value.");                                                     \
      }                                                                     \
    }                                                                       \
  }

template <typename T>
struct TypeTag {};

template <typename F, F f>
struct KernelFuncImpl;

template <typename Return, typename... Args, Return (*impl_fn)(Args...)>
struct KernelFuncImpl<Return (*)(Args...), impl_fn> {
  static Return Compute(std::vector<Tensor> inputs,
                        std::vector<boost::any> attrs) {
    return ComputeCallHelper<Args..., TypeTag<int>>::template Compute<0, 0>(
        inputs, attrs);
  }

 private:
  template <typename... RemainingArgs>
  struct ComputeCallHelper;

  // for Tensor input
  template <typename... Tail>
  struct ComputeCallHelper<const Tensor&, Tail...> {
    template <int in_idx, int attr_idx, typename... PreviousArgs>
    static Return Compute(std::vector<Tensor> inputs,
                          std::vector<boost::any> attrs,
                          const PreviousArgs&... pargs) {
      static_assert(attr_idx == 0,
                    "Input tensor should appear before attributes.");
      const Tensor& arg = inputs[in_idx];
      return ComputeCallHelper<Tail...>::template Compute<in_idx + 1, attr_idx>(
          inputs, attrs, pargs..., arg);
    }
  };

  PD_SPECIALIZE_ComputeCallHelper(bool);
  PD_SPECIALIZE_ComputeCallHelper(int);
  PD_SPECIALIZE_ComputeCallHelper(float);
  PD_SPECIALIZE_ComputeCallHelper(int64_t);
  PD_SPECIALIZE_ComputeCallHelper(std::string);
  PD_SPECIALIZE_ComputeCallHelper(std::vector<int>);
  PD_SPECIALIZE_ComputeCallHelper(std::vector<float>);
  PD_SPECIALIZE_ComputeCallHelper(std::vector<int64_t>);
  PD_SPECIALIZE_ComputeCallHelper(std::vector<std::string>);
  // TODO(chenweihang): support other attribute type if needed.
  // Why not support other attribute type here?
  // - boost::blank, std::vector<bool> and std::vector<double>
  //   are not used in op
  // - BlockDesc* and std::vector<BlockDesc*> are used in framework
  // end: base template
  template <typename T>
  struct ComputeCallHelper<TypeTag<T>> {
    template <int in_idx, int attr_idx>
    static Return Compute(std::vector<Tensor> inputs,
                          std::vector<boost::any> attrs, const Args&... args) {
      return impl_fn(args...);
    }
  };
};

#define PD_KERNEL(...) \
  ::paddle::KernelFuncImpl<decltype(&__VA_ARGS__), &__VA_ARGS__>::Compute

/////////////// InferShape Function (PD_INFER_SHAPE) ///////////////

// Record Op infershape core function
using InferShapeFunc = std::vector<std::vector<int64_t>> (*)(
    std::vector<std::vector<int64_t>> input_shapes);

template <typename F, F f>
struct InferShapeFuncImpl;

template <typename Return, typename... Args, Return (*impl_fn)(Args...)>
struct InferShapeFuncImpl<Return (*)(Args...), impl_fn> {
  static Return InferShape(std::vector<std::vector<int64_t>> input_shapes) {
    return InferShapeCallHelper<Args..., TypeTag<int>>::template InferShape<0>(
        input_shapes);
  }

 private:
  template <typename... RemainingArgs>
  struct InferShapeCallHelper;

  // only one type input: std::vector<int64_t>
  template <typename... Tail>
  struct InferShapeCallHelper<std::vector<int64_t>, Tail...> {
    template <int in_idx, typename... PreviousArgs>
    static Return InferShape(std::vector<std::vector<int64_t>> input_shapes,
                             const PreviousArgs&... pargs) {
      std::vector<int64_t> arg = input_shapes[in_idx];
      return InferShapeCallHelper<Tail...>::template InferShape<in_idx + 1>(
          input_shapes, pargs..., arg);
    }
  };

  // end: base template
  template <typename T>
  struct InferShapeCallHelper<TypeTag<T>> {
    template <int in_idx>
    static Return InferShape(std::vector<std::vector<int64_t>> input_shapes,
                             const Args&... args) {
      return impl_fn(args...);
    }
  };
};

#define PD_INFER_SHAPE(...) \
  ::paddle::InferShapeFuncImpl<decltype(&__VA_ARGS__), &__VA_ARGS__>::InferShape

/////////////// InferDataType Function (PD_INFER_DTYPE) ///////////////

// Record Op Infer dtype core function
using InferDtypeFunc =
    std::vector<DataType> (*)(std::vector<DataType> input_dtypes);

template <typename F, F f>
struct InferDtypeFuncImpl;

template <typename Return, typename... Args, Return (*impl_fn)(Args...)>
struct InferDtypeFuncImpl<Return (*)(Args...), impl_fn> {
  static Return InferDtype(std::vector<DataType> input_dtypes) {
    return InferDtypeCallHelper<Args..., TypeTag<int>>::template InferDtype<0>(
        input_dtypes);
  }

 private:
  template <typename... RemainingArgs>
  struct InferDtypeCallHelper;

  // Only one type input now: DataType
  template <typename... Tail>
  struct InferDtypeCallHelper<DataType, Tail...> {
    template <int in_idx, typename... PreviousArgs>
    static Return InferDtype(std::vector<DataType> input_dtypes,
                             const PreviousArgs&... pargs) {
      DataType arg = input_dtypes[in_idx];
      return InferDtypeCallHelper<Tail...>::template InferDtype<in_idx + 1>(
          input_dtypes, pargs..., arg);
    }
  };

  // end: base template
  template <typename T>
  struct InferDtypeCallHelper<TypeTag<T>> {
    template <int in_idx>
    static Return InferDtype(std::vector<DataType> input_dtypes,
                             const Args&... args) {
      return impl_fn(args...);
    }
  };
};

#define PD_INFER_DTYPE(...) \
  ::paddle::InferDtypeFuncImpl<decltype(&__VA_ARGS__), &__VA_ARGS__>::InferDtype

////////////////////// Op Meta Info //////////////////////

class PD_DLL_DECL OpMetaInfo {
 public:
  explicit OpMetaInfo(const std::string& op_name) : name_(op_name) {}

  // format: {"<name1>", "<name2>", ...}
  OpMetaInfo& Inputs(std::vector<std::string>&& inputs);

  // format: {"<name1>", "<name2>", ...}
  OpMetaInfo& Outputs(std::vector<std::string>&& outputs);

  // format: {"<name1>:<type1>", "<name1>:<type1>", ...}
  OpMetaInfo& Attrs(std::vector<std::string>&& attrs);

  // format: PD_KERNEL(...)
  OpMetaInfo& SetKernelFn(KernelFunc&& func);

  // format: PD_INFER_SHAPE(...)
  OpMetaInfo& SetInferShapeFn(InferShapeFunc&& func);

  // format: PD_INFER_DTYPE(...)
  OpMetaInfo& SetInferDtypeFn(InferDtypeFunc&& func);

 private:
  friend class framework::OpMetaInfoHelper;

  // 1. desc info
  std::string name_;
  std::vector<std::string> inputs_;
  std::vector<std::string> outputs_;
  std::vector<std::string> attrs_;

  // 2. func info
  KernelFunc kernel_fn_;
  InferShapeFunc infer_shape_fn_;
  InferDtypeFunc infer_dtype_fn_;
};

//////////////// Op Meta Info Map /////////////////

class PD_DLL_DECL OpMetaInfoMap {
 public:
  // this function's impl should keep in header file.
  // if move to cc file, meta info can not be added
  // into map
  static OpMetaInfoMap& Instance() {
    static OpMetaInfoMap g_custom_op_meta_info_map;
    return g_custom_op_meta_info_map;
  }

  std::vector<OpMetaInfo>& operator[](const std::string& name);

  const std::unordered_map<std::string, std::vector<OpMetaInfo>>& GetMap()
      const;

 private:
  OpMetaInfoMap() = default;
  std::unordered_map<std::string, std::vector<OpMetaInfo>> map_;

  PD_DISABLE_COPY_AND_ASSIGN(OpMetaInfoMap);
};

//////////////// Op Meta Info Builder /////////////////

class PD_DLL_DECL OpMetaInfoBuilder {
 public:
  explicit OpMetaInfoBuilder(std::string&& name);
  OpMetaInfoBuilder& Inputs(std::vector<std::string>&& inputs);
  OpMetaInfoBuilder& Outputs(std::vector<std::string>&& outputs);
  OpMetaInfoBuilder& Attrs(std::vector<std::string>&& attrs);
  OpMetaInfoBuilder& SetKernelFn(KernelFunc func);
  OpMetaInfoBuilder& SetInferShapeFn(InferShapeFunc func);
  OpMetaInfoBuilder& SetInferDtypeFn(InferDtypeFunc func);
  OpMetaInfoBuilder& SetBackwardOp(const std::string& bwd_op_name);

 private:
  // Forward Op name
  std::string name_;
  // Point to the currently constructed op meta info
  OpMetaInfo* info_ptr_;
};

/////////////////////// Op register API /////////////////////////

// For inference: compile directly with framework
// Call after PD_BUILD_OP(...)
void RegisterAllCustomOperator();

// Using this api to load compiled custom operator's dynamic library and
// register Custom
// Operator into it
void LoadCustomOperatorLib(const std::string& dso_name);

/////////////////////// Op register Macro /////////////////////////

#define PD_BUILD_OP_WITH_COUNTER(op_name, counter)                  \
  static ::paddle::OpMetaInfoBuilder __op_meta_info_##counter##__ = \
      ::paddle::OpMetaInfoBuilder(op_name)

#define PD_BUILD_OP_INNER(op_name, counter) \
  PD_BUILD_OP_WITH_COUNTER(op_name, counter)

#define PD_BUILD_OP(op_name) PD_BUILD_OP_INNER(op_name, __COUNTER__)

}  // namespace paddle

///////////////////// C API ///////////////////

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
// C-API to get global OpMetaInfoMap.
__declspec(dllexport) inline paddle::OpMetaInfoMap& PD_GetOpMetaInfoMap() {
  return paddle::OpMetaInfoMap::Instance();
}
#endif  // _WIN32

#ifdef __cplusplus
}
#endif
