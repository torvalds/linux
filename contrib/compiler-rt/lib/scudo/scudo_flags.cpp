//===-- scudo_flags.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Hardened Allocator flag parsing logic.
///
//===----------------------------------------------------------------------===//

#include "scudo_flags.h"
#include "scudo_interface_internal.h"
#include "scudo_utils.h"

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_flag_parser.h"

namespace __scudo {

static Flags ScudoFlags;  // Use via getFlags().

void Flags::setDefaults() {
#define SCUDO_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "scudo_flags.inc"
#undef SCUDO_FLAG
}

static void RegisterScudoFlags(FlagParser *parser, Flags *f) {
#define SCUDO_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "scudo_flags.inc"
#undef SCUDO_FLAG
}

static const char *getCompileDefinitionScudoDefaultOptions() {
#ifdef SCUDO_DEFAULT_OPTIONS
  return SANITIZER_STRINGIFY(SCUDO_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

static const char *getScudoDefaultOptions() {
  return (&__scudo_default_options) ? __scudo_default_options() : "";
}

void initFlags() {
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.exitcode = 1;
    OverrideCommonFlags(cf);
  }
  Flags *f = getFlags();
  f->setDefaults();

  FlagParser ScudoParser;
  RegisterScudoFlags(&ScudoParser, f);
  RegisterCommonFlags(&ScudoParser);

  // Override from compile definition.
  ScudoParser.ParseString(getCompileDefinitionScudoDefaultOptions());

  // Override from user-specified string.
  ScudoParser.ParseString(getScudoDefaultOptions());

  // Override from environment.
  ScudoParser.ParseString(GetEnv("SCUDO_OPTIONS"));

  InitializeCommonFlags();

  // Sanity checks and default settings for the Quarantine parameters.

  if (f->QuarantineSizeMb >= 0) {
    // Backward compatible logic if QuarantineSizeMb is set.
    if (f->QuarantineSizeKb >= 0) {
      dieWithMessage("ERROR: please use either QuarantineSizeMb (deprecated) "
          "or QuarantineSizeKb, but not both\n");
    }
    if (f->QuarantineChunksUpToSize >= 0) {
      dieWithMessage("ERROR: QuarantineChunksUpToSize cannot be used in "
          " conjunction with the deprecated QuarantineSizeMb option\n");
    }
    // If everything is in order, update QuarantineSizeKb accordingly.
    f->QuarantineSizeKb = f->QuarantineSizeMb * 1024;
  } else {
    // Otherwise proceed with the new options.
    if (f->QuarantineSizeKb < 0) {
      const int DefaultQuarantineSizeKb = FIRST_32_SECOND_64(64, 256);
      f->QuarantineSizeKb = DefaultQuarantineSizeKb;
    }
    if (f->QuarantineChunksUpToSize < 0) {
      const int DefaultQuarantineChunksUpToSize = FIRST_32_SECOND_64(512, 2048);
      f->QuarantineChunksUpToSize = DefaultQuarantineChunksUpToSize;
    }
  }

  // We enforce an upper limit for the chunk quarantine threshold of 4Mb.
  if (f->QuarantineChunksUpToSize > (4 * 1024 * 1024)) {
    dieWithMessage("ERROR: the chunk quarantine threshold is too large\n");
  }

  // We enforce an upper limit for the quarantine size of 32Mb.
  if (f->QuarantineSizeKb > (32 * 1024)) {
    dieWithMessage("ERROR: the quarantine size is too large\n");
  }

  if (f->ThreadLocalQuarantineSizeKb < 0) {
    const int DefaultThreadLocalQuarantineSizeKb = FIRST_32_SECOND_64(16, 64);
    f->ThreadLocalQuarantineSizeKb = DefaultThreadLocalQuarantineSizeKb;
  }
  // And an upper limit of 8Mb for the thread quarantine cache.
  if (f->ThreadLocalQuarantineSizeKb > (8 * 1024)) {
    dieWithMessage("ERROR: the per thread quarantine cache size is too "
        "large\n");
  }
  if (f->ThreadLocalQuarantineSizeKb == 0 && f->QuarantineSizeKb > 0) {
    dieWithMessage("ERROR: ThreadLocalQuarantineSizeKb can be set to 0 only "
        "when QuarantineSizeKb is set to 0\n");
  }
}

Flags *getFlags() {
  return &ScudoFlags;
}

}  // namespace __scudo

#if !SANITIZER_SUPPORTS_WEAK_HOOKS
SANITIZER_INTERFACE_WEAK_DEF(const char*, __scudo_default_options, void) {
  return "";
}
#endif
