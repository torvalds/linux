//===--- BitmaskEnum.h - wrapper of LLVM's bitmask enum facility-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Provides LLVM's BitmaskEnum facility to enumeration types declared in
/// namespace clang.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_BITMASKENUM_H
#define LLVM_CLANG_BASIC_BITMASKENUM_H

#include "llvm/ADT/BitmaskEnum.h"

namespace clang {
  LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();
}

#endif
