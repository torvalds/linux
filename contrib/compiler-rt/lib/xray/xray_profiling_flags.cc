//===-- xray_flags.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// XRay runtime flags.
//===----------------------------------------------------------------------===//

#include "xray_profiling_flags.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "xray_defs.h"

namespace __xray {

// Storage for the profiling flags.
ProfilerFlags xray_profiling_flags_dont_use_directly;

void ProfilerFlags::setDefaults() XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "xray_profiling_flags.inc"
#undef XRAY_FLAG
}

void registerProfilerFlags(FlagParser *P,
                           ProfilerFlags *F) XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description)                       \
  RegisterFlag(P, #Name, Description, &F->Name);
#include "xray_profiling_flags.inc"
#undef XRAY_FLAG
}

} // namespace __xray
