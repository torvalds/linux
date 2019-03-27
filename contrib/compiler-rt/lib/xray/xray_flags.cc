//===-- xray_flags.cc -------------------------------------------*- C++ -*-===//
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
// XRay flag parsing logic.
//===----------------------------------------------------------------------===//

#include "xray_flags.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "xray_defs.h"

using namespace __sanitizer;

namespace __xray {

Flags xray_flags_dont_use_directly; // use via flags().

void Flags::setDefaults() XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "xray_flags.inc"
#undef XRAY_FLAG
}

void registerXRayFlags(FlagParser *P, Flags *F) XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description)                       \
  RegisterFlag(P, #Name, Description, &F->Name);
#include "xray_flags.inc"
#undef XRAY_FLAG
}

// This function, as defined with the help of a macro meant to be introduced at
// build time of the XRay runtime, passes in a statically defined list of
// options that control XRay. This means users/deployments can tweak the
// defaults that override the hard-coded defaults in the xray_flags.inc at
// compile-time using the XRAY_DEFAULT_OPTIONS macro.
const char *useCompilerDefinedFlags() XRAY_NEVER_INSTRUMENT {
#ifdef XRAY_DEFAULT_OPTIONS
  // Do the double-layered string conversion to prevent badly crafted strings
  // provided through the XRAY_DEFAULT_OPTIONS from causing compilation issues
  // (or changing the semantics of the implementation through the macro). This
  // ensures that we convert whatever XRAY_DEFAULT_OPTIONS is defined as a
  // string literal.
  return SANITIZER_STRINGIFY(XRAY_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

void initializeFlags() XRAY_NEVER_INSTRUMENT {
  SetCommonFlagsDefaults();
  auto *F = flags();
  F->setDefaults();

  FlagParser XRayParser;
  registerXRayFlags(&XRayParser, F);
  RegisterCommonFlags(&XRayParser);

  // Use options defaulted at compile-time for the runtime.
  const char *XRayCompileFlags = useCompilerDefinedFlags();
  XRayParser.ParseString(XRayCompileFlags);

  // Override from environment variables.
  XRayParser.ParseString(GetEnv("XRAY_OPTIONS"));

  // Override from command line.
  InitializeCommonFlags();

  if (Verbosity())
    ReportUnrecognizedFlags();

  if (common_flags()->help) {
    XRayParser.PrintFlagDescriptions();
  }
}

} // namespace __xray
