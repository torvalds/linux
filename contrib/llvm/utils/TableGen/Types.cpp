//===- Types.cpp - Helper for the selection of C++ data types. ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Types.h"

// For LLVM_ATTRIBUTE_UNUSED
#include "llvm/Support/Compiler.h"

#include <cassert>

using namespace llvm;

const char *llvm::getMinimalTypeForRange(uint64_t Range, unsigned MaxSize LLVM_ATTRIBUTE_UNUSED) {
  // TODO: The original callers only used 32 and 64 so these are the only
  //       values permitted. Rather than widen the supported values we should
  //       allow 64 for the callers that currently use 32 and remove the
  //       argument altogether.
  assert((MaxSize == 32 || MaxSize == 64) && "Unexpected size");
  assert(MaxSize <= 64 && "Unexpected size");
  assert(((MaxSize > 32) ? Range <= 0xFFFFFFFFFFFFFFFFULL
                         : Range <= 0xFFFFFFFFULL) &&
         "Enum too large");

  if (Range > 0xFFFFFFFFULL)
    return "uint64_t";
  if (Range > 0xFFFF)
    return "uint32_t";
  if (Range > 0xFF)
    return "uint16_t";
  return "uint8_t";
}

const char *llvm::getMinimalTypeForEnumBitfield(uint64_t Size) {
  uint64_t MaxIndex = Size;
  if (MaxIndex > 0)
    MaxIndex--;
  assert(MaxIndex <= 64 && "Too many bits");
  return getMinimalTypeForRange(1ULL << MaxIndex);
}
