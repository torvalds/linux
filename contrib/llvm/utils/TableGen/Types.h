//===- Types.h - Helper for the selection of C++ types. ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_TYPES_H
#define LLVM_UTILS_TABLEGEN_TYPES_H

#include <cstdint>

namespace llvm {
/// Returns the smallest unsigned integer type that can hold the given range.
/// MaxSize indicates the largest size of integer to consider (in bits) and only
/// supports values of at least 32.
const char *getMinimalTypeForRange(uint64_t Range, unsigned MaxSize = 64);

/// Returns the smallest unsigned integer type that can hold the given bitfield.
const char *getMinimalTypeForEnumBitfield(uint64_t Size);
}

#endif
