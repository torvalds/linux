//===--- Stack.h - Utilities for dealing with stack space -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines utilities for dealing with stack allocation and stack space.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_STACK_H
#define LLVM_CLANG_BASIC_STACK_H

#include <cstddef>

namespace clang {
  /// The amount of stack space that Clang would like to be provided with.
  /// If less than this much is available, we may be unable to reach our
  /// template instantiation depth limit and other similar limits.
  constexpr size_t DesiredStackSize = 8 << 20;
} // end namespace clang

#endif // LLVM_CLANG_BASIC_STACK_H
