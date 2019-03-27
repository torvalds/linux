//===-- xray_fdr_flags.cc ---------------------------------------*- C++ -*-===//
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
// XRay FDR flag parsing logic.
//===----------------------------------------------------------------------===//

#include "xray_fdr_flags.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "xray_defs.h"

using namespace __sanitizer;

namespace __xray {

FDRFlags xray_fdr_flags_dont_use_directly; // use via fdrFlags().

void FDRFlags::setDefaults() XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "xray_fdr_flags.inc"
#undef XRAY_FLAG
}

void registerXRayFDRFlags(FlagParser *P, FDRFlags *F) XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description)                       \
  RegisterFlag(P, #Name, Description, &F->Name);
#include "xray_fdr_flags.inc"
#undef XRAY_FLAG
}

const char *useCompilerDefinedFDRFlags() XRAY_NEVER_INSTRUMENT {
#ifdef XRAY_FDR_OPTIONS
  return SANITIZER_STRINGIFY(XRAY_FDR_OPTIONS);
#else
  return "";
#endif
}

} // namespace __xray
