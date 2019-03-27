//===-- hwasan_flags.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef HWASAN_FLAGS_H
#define HWASAN_FLAGS_H

namespace __hwasan {

struct Flags {
#define HWASAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "hwasan_flags.inc"
#undef HWASAN_FLAG

  void SetDefaults();
};

Flags *flags();

}  // namespace __hwasan

#endif  // HWASAN_FLAGS_H
