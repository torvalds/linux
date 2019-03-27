//===-- lib/adddf3.c - Double-precision addition ------------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements double-precision soft-float addition with the IEEE-754
// default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "fp_add_impl.inc"

COMPILER_RT_ABI double __adddf3(double a, double b){
    return __addXf3__(a, b);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI double __aeabi_dadd(double a, double b) {
  return __adddf3(a, b);
}
#else
AEABI_RTABI double __aeabi_dadd(double a, double b) COMPILER_RT_ALIAS(__adddf3);
#endif
#endif
