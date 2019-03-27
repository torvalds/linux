//===-- lib/truncdfhf2.c - double -> half conversion --------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define SRC_DOUBLE
#define DST_HALF
#include "fp_trunc_impl.inc"

COMPILER_RT_ABI uint16_t __truncdfhf2(double a) {
    return __truncXfYf2__(a);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI uint16_t __aeabi_d2h(double a) {
  return __truncdfhf2(a);
}
#else
AEABI_RTABI uint16_t __aeabi_d2h(double a) COMPILER_RT_ALIAS(__truncdfhf2);
#endif
#endif
