//===-- scudo_flags.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Header for scudo_flags.cpp.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_FLAGS_H_
#define SCUDO_FLAGS_H_

namespace __scudo {

struct Flags {
#define SCUDO_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "scudo_flags.inc"
#undef SCUDO_FLAG

  void setDefaults();
};

Flags *getFlags();

void initFlags();

}  // namespace __scudo

#endif  // SCUDO_FLAGS_H_
