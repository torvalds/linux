//===-- lib/negdf2.c - double-precision negation ------------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements double-precision soft-float negation.
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

COMPILER_RT_ABI fp_t
__negdf2(fp_t a) {
    return fromRep(toRep(a) ^ signBit);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_dneg(fp_t a) {
  return __negdf2(a);
}
#else
AEABI_RTABI fp_t __aeabi_dneg(fp_t a) COMPILER_RT_ALIAS(__negdf2);
#endif
#endif
