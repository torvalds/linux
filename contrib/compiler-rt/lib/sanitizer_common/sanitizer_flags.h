//===-- sanitizer_flags.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_FLAGS_H
#define SANITIZER_FLAGS_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

enum HandleSignalMode {
  kHandleSignalNo,
  kHandleSignalYes,
  kHandleSignalExclusive,
};

struct CommonFlags {
#define COMMON_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "sanitizer_flags.inc"
#undef COMMON_FLAG

  void SetDefaults();
  void CopyFrom(const CommonFlags &other);
};

// Functions to get/set global CommonFlags shared by all sanitizer runtimes:
extern CommonFlags common_flags_dont_use;
inline const CommonFlags *common_flags() {
  return &common_flags_dont_use;
}

inline void SetCommonFlagsDefaults() {
  common_flags_dont_use.SetDefaults();
}

// This function can only be used to setup tool-specific overrides for
// CommonFlags defaults. Generally, it should only be used right after
// SetCommonFlagsDefaults(), but before ParseCommonFlagsFromString(), and
// only during the flags initialization (i.e. before they are used for
// the first time).
inline void OverrideCommonFlags(const CommonFlags &cf) {
  common_flags_dont_use.CopyFrom(cf);
}

void SubstituteForFlagValue(const char *s, char *out, uptr out_size);

class FlagParser;
void RegisterCommonFlags(FlagParser *parser,
                         CommonFlags *cf = &common_flags_dont_use);
void RegisterIncludeFlags(FlagParser *parser, CommonFlags *cf);

// Should be called after parsing all flags. Sets up common flag values
// and perform initializations common to all sanitizers (e.g. setting
// verbosity).
void InitializeCommonFlags(CommonFlags *cf = &common_flags_dont_use);
}  // namespace __sanitizer

#endif  // SANITIZER_FLAGS_H
