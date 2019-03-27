//===- FuzzerExtraCounters.cpp - Extra coverage counters ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Extra coverage counters defined by user code.
//===----------------------------------------------------------------------===//

#include "FuzzerDefs.h"

#if LIBFUZZER_LINUX || LIBFUZZER_NETBSD || LIBFUZZER_FREEBSD ||                \
    LIBFUZZER_OPENBSD
__attribute__((weak)) extern uint8_t __start___libfuzzer_extra_counters;
__attribute__((weak)) extern uint8_t __stop___libfuzzer_extra_counters;

namespace fuzzer {
uint8_t *ExtraCountersBegin() { return &__start___libfuzzer_extra_counters; }
uint8_t *ExtraCountersEnd() { return &__stop___libfuzzer_extra_counters; }
ATTRIBUTE_NO_SANITIZE_ALL
void ClearExtraCounters() {  // hand-written memset, don't asan-ify.
  uintptr_t *Beg = reinterpret_cast<uintptr_t*>(ExtraCountersBegin());
  uintptr_t *End = reinterpret_cast<uintptr_t*>(ExtraCountersEnd());
  for (; Beg < End; Beg++) {
    *Beg = 0;
    __asm__ __volatile__("" : : : "memory");
  }
}

}  // namespace fuzzer

#else
// TODO: implement for other platforms.
namespace fuzzer {
uint8_t *ExtraCountersBegin() { return nullptr; }
uint8_t *ExtraCountersEnd() { return nullptr; }
void ClearExtraCounters() {}
}  // namespace fuzzer

#endif
