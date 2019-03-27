//===-- sancov_flags.cc -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Sanitizer Coverage runtime flags.
//
//===----------------------------------------------------------------------===//

#include "sancov_flags.h"
#include "sanitizer_flag_parser.h"
#include "sanitizer_platform.h"

SANITIZER_INTERFACE_WEAK_DEF(const char*, __sancov_default_options, void) {
  return "";
}

using namespace __sanitizer;

namespace __sancov {

SancovFlags sancov_flags_dont_use_directly;  // use via flags();

void SancovFlags::SetDefaults() {
#define SANCOV_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "sancov_flags.inc"
#undef SANCOV_FLAG
}

static void RegisterSancovFlags(FlagParser *parser, SancovFlags *f) {
#define SANCOV_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "sancov_flags.inc"
#undef SANCOV_FLAG
}

static const char *MaybeCallSancovDefaultOptions() {
  return (&__sancov_default_options) ? __sancov_default_options() : "";
}

void InitializeSancovFlags() {
  SancovFlags *f = sancov_flags();
  f->SetDefaults();

  FlagParser parser;
  RegisterSancovFlags(&parser, f);

  parser.ParseString(MaybeCallSancovDefaultOptions());
  parser.ParseString(GetEnv("SANCOV_OPTIONS"));

  ReportUnrecognizedFlags();
  if (f->help) parser.PrintFlagDescriptions();
}

}  // namespace __sancov
