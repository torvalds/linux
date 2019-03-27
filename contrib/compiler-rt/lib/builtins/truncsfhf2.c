//===-- lib/truncsfhf2.c - single -> half conversion --------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define SRC_SINGLE
#define DST_HALF
#include "fp_trunc_impl.inc"

// Use a forwarding definition and noinline to implement a poor man's alias,
// as there isn't a good cross-platform way of defining one.
COMPILER_RT_ABI NOINLINE uint16_t __truncsfhf2(float a) {
    return __truncXfYf2__(a);
}

COMPILER_RT_ABI uint16_t __gnu_f2h_ieee(float a) {
    return __truncsfhf2(a);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI uint16_t __aeabi_f2h(float a) {
  return __truncsfhf2(a);
}
#else
AEABI_RTABI uint16_t __aeabi_f2h(float a) COMPILER_RT_ALIAS(__truncsfhf2);
#endif
#endif
