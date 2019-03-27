//===-- scudo_utils.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Header for scudo_utils.cpp.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_UTILS_H_
#define SCUDO_UTILS_H_

#include "sanitizer_common/sanitizer_common.h"

#include <string.h>

namespace __scudo {

template <class Dest, class Source>
INLINE Dest bit_cast(const Source& source) {
  static_assert(sizeof(Dest) == sizeof(Source), "Sizes are not equal!");
  Dest dest;
  memcpy(&dest, &source, sizeof(dest));
  return dest;
}

void NORETURN dieWithMessage(const char *Format, ...);

bool hasHardwareCRC32();

}  // namespace __scudo

#endif  // SCUDO_UTILS_H_
