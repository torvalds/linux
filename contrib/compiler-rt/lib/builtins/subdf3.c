//===-- lib/adddf3.c - Double-precision subtraction ---------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements double-precision soft-float subtraction with the
// IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_lib.h"

// Subtraction; flip the sign bit of b and add.
COMPILER_RT_ABI fp_t
__subdf3(fp_t a, fp_t b) {
    return __adddf3(a, fromRep(toRep(b) ^ signBit));
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI fp_t __aeabi_dsub(fp_t a, fp_t b) {
  return __subdf3(a, b);
}
#else
AEABI_RTABI fp_t __aeabi_dsub(fp_t a, fp_t b) COMPILER_RT_ALIAS(__subdf3);
#endif
#endif
