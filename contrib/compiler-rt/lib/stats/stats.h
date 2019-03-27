//===-- stats.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Data definitions for sanitizer statistics gathering.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_STATS_STATS_H
#define SANITIZER_STATS_STATS_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __sanitizer {

// Number of bits in data that are used for the sanitizer kind. Needs to match
// llvm::kSanitizerStatKindBits in
// llvm/include/llvm/Transforms/Utils/SanitizerStats.h
enum { kKindBits = 3 };

struct StatInfo {
  uptr addr;
  uptr data;
};

struct StatModule {
  StatModule *next;
  u32 size;
  StatInfo infos[1];
};

inline uptr CountFromData(uptr data) {
  return data & ((1ull << (sizeof(uptr) * 8 - kKindBits)) - 1);
}

}

#endif
