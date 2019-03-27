//===--- Cuda.h - Utilities for compiling CUDA code  ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_CUDA_H
#define LLVM_CLANG_BASIC_CUDA_H

namespace llvm {
class StringRef;
} // namespace llvm

namespace clang {

enum class CudaVersion {
  UNKNOWN,
  CUDA_70,
  CUDA_75,
  CUDA_80,
  CUDA_90,
  CUDA_91,
  CUDA_92,
  CUDA_100,
  LATEST = CUDA_100,
};
const char *CudaVersionToString(CudaVersion V);

// No string -> CudaVersion conversion function because there's no canonical
// spelling of the various CUDA versions.

enum class CudaArch {
  UNKNOWN,
  SM_20,
  SM_21,
  SM_30,
  SM_32,
  SM_35,
  SM_37,
  SM_50,
  SM_52,
  SM_53,
  SM_60,
  SM_61,
  SM_62,
  SM_70,
  SM_72,
  SM_75,
  GFX600,
  GFX601,
  GFX700,
  GFX701,
  GFX702,
  GFX703,
  GFX704,
  GFX801,
  GFX802,
  GFX803,
  GFX810,
  GFX900,
  GFX902,
  GFX904,
  GFX906,
  GFX909,
  LAST,
};
const char *CudaArchToString(CudaArch A);

// The input should have the form "sm_20".
CudaArch StringToCudaArch(llvm::StringRef S);

enum class CudaVirtualArch {
  UNKNOWN,
  COMPUTE_20,
  COMPUTE_30,
  COMPUTE_32,
  COMPUTE_35,
  COMPUTE_37,
  COMPUTE_50,
  COMPUTE_52,
  COMPUTE_53,
  COMPUTE_60,
  COMPUTE_61,
  COMPUTE_62,
  COMPUTE_70,
  COMPUTE_72,
  COMPUTE_75,
  COMPUTE_AMDGCN,
};
const char *CudaVirtualArchToString(CudaVirtualArch A);

// The input should have the form "compute_20".
CudaVirtualArch StringToCudaVirtualArch(llvm::StringRef S);

/// Get the compute_xx corresponding to an sm_yy.
CudaVirtualArch VirtualArchForCudaArch(CudaArch A);

/// Get the earliest CudaVersion that supports the given CudaArch.
CudaVersion MinVersionForCudaArch(CudaArch A);

/// Get the latest CudaVersion that supports the given CudaArch.
CudaVersion MaxVersionForCudaArch(CudaArch A);

} // namespace clang

#endif
