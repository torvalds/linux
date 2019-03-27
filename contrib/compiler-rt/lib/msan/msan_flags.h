//===-- msan_flags.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef MSAN_FLAGS_H
#define MSAN_FLAGS_H

namespace __msan {

struct Flags {
#define MSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "msan_flags.inc"
#undef MSAN_FLAG

  void SetDefaults();
};

Flags *flags();

}  // namespace __msan

#endif  // MSAN_FLAGS_H
