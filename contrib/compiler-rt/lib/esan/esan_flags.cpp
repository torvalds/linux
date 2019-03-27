//===-- esan_flags.cc -------------------------------------------*- C++ -*-===//
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
// Esan flag parsing logic.
//===----------------------------------------------------------------------===//

#include "esan_flags.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"

using namespace __sanitizer;

namespace __esan {

static const char EsanOptsEnv[] = "ESAN_OPTIONS";

Flags EsanFlagsDontUseDirectly;

void Flags::setDefaults() {
#define ESAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "esan_flags.inc"
#undef ESAN_FLAG
}

static void registerEsanFlags(FlagParser *Parser, Flags *F) {
#define ESAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(Parser, #Name, Description, &F->Name);
#include "esan_flags.inc"
#undef ESAN_FLAG
}

void initializeFlags() {
  SetCommonFlagsDefaults();
  Flags *F = getFlags();
  F->setDefaults();

  FlagParser Parser;
  registerEsanFlags(&Parser, F);
  RegisterCommonFlags(&Parser);
  Parser.ParseString(GetEnv(EsanOptsEnv));

  InitializeCommonFlags();
  if (Verbosity())
    ReportUnrecognizedFlags();
  if (common_flags()->help)
    Parser.PrintFlagDescriptions();

  __sanitizer_set_report_path(common_flags()->log_path);
}

} // namespace __esan
