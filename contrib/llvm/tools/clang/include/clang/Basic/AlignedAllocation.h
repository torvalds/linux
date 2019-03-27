//===--- AlignedAllocation.h - Aligned Allocation ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines a function that returns the minimum OS versions supporting
/// C++17's aligned allocation functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ALIGNED_ALLOCATION_H
#define LLVM_CLANG_BASIC_ALIGNED_ALLOCATION_H

#include "llvm/ADT/Triple.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"

namespace clang {

inline llvm::VersionTuple alignedAllocMinVersion(llvm::Triple::OSType OS) {
  switch (OS) {
  default:
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX: // Earliest supporting version is 10.14.
    return llvm::VersionTuple(10U, 14U);
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS: // Earliest supporting version is 11.0.0.
    return llvm::VersionTuple(11U);
  case llvm::Triple::WatchOS: // Earliest supporting version is 4.0.0.
    return llvm::VersionTuple(4U);
  }

  llvm_unreachable("Unexpected OS");
}

} // end namespace clang

#endif // LLVM_CLANG_BASIC_ALIGNED_ALLOCATION_H
