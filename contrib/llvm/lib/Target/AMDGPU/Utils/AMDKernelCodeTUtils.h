//===- AMDGPUKernelCodeTUtils.h - helpers for amd_kernel_code_t -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file AMDKernelCodeTUtils.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_UTILS_AMDKERNELCODETUTILS_H
#define LLVM_LIB_TARGET_AMDGPU_UTILS_AMDKERNELCODETUTILS_H

#include "AMDKernelCodeT.h"

namespace llvm {

class MCAsmParser;
class raw_ostream;
class StringRef;

void printAmdKernelCodeField(const amd_kernel_code_t &C, int FldIndex,
                             raw_ostream &OS);

void dumpAmdKernelCode(const amd_kernel_code_t *C, raw_ostream &OS,
                       const char *tab);

bool parseAmdKernelCodeField(StringRef ID, MCAsmParser &Parser,
                             amd_kernel_code_t &C, raw_ostream &Err);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_UTILS_AMDKERNELCODETUTILS_H
