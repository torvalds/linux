//===-- lib/addsf3.c - Single-precision addition ------------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements single-precision soft-float addition with the IEEE-754
// default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define SINGLE_PRECISION
#include "fp_add_impl.inc"

COMPILER_RT_ABI float __addsf3(float a, float b) {
    return __addXf3__(a, b);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI float __aeabi_fadd(float a, float b) {
  return __addsf3(a, b);
}
#else
AEABI_RTABI float __aeabi_fadd(float a, float b) COMPILER_RT_ALIAS(__addsf3);
#endif
#endif
