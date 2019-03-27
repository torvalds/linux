//===-- tsan_flags.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
// NOTE: This file may be included into user code.
//===----------------------------------------------------------------------===//

#ifndef TSAN_FLAGS_H
#define TSAN_FLAGS_H

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_deadlock_detector_interface.h"

namespace __tsan {

struct Flags : DDFlags {
#define TSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "tsan_flags.inc"
#undef TSAN_FLAG

  void SetDefaults();
  void ParseFromString(const char *str);
};

void InitializeFlags(Flags *flags, const char *env);
}  // namespace __tsan

#endif  // TSAN_FLAGS_H
