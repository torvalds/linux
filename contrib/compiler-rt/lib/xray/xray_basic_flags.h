//===-- xray_basic_flags.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instruementation system.
//
// XRay Basic Mode runtime flags.
//===----------------------------------------------------------------------===//

#ifndef XRAY_BASIC_FLAGS_H
#define XRAY_BASIC_FLAGS_H

#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __xray {

struct BasicFlags {
#define XRAY_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "xray_basic_flags.inc"
#undef XRAY_FLAG

  void setDefaults();
};

extern BasicFlags xray_basic_flags_dont_use_directly;
extern void registerXRayBasicFlags(FlagParser *P, BasicFlags *F);
const char *useCompilerDefinedBasicFlags();
inline BasicFlags *basicFlags() { return &xray_basic_flags_dont_use_directly; }

} // namespace __xray

#endif // XRAY_BASIC_FLAGS_H
