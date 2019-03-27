//===-- esan_flags.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Esan runtime flags.
//===----------------------------------------------------------------------===//

#ifndef ESAN_FLAGS_H
#define ESAN_FLAGS_H

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_flag_parser.h"

namespace __esan {

class Flags {
public:
#define ESAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "esan_flags.inc"
#undef ESAN_FLAG

  void setDefaults();
};

extern Flags EsanFlagsDontUseDirectly;
inline Flags *getFlags() {
  return &EsanFlagsDontUseDirectly;
}

void initializeFlags();

} // namespace __esan

#endif // ESAN_FLAGS_H
