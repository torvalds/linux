//===-- lib/addtf3.c - Quad-precision addition --------------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements quad-precision soft-float addition with the IEEE-754
// default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
#include "fp_add_impl.inc"

COMPILER_RT_ABI long double __addtf3(long double a, long double b){
    return __addXf3__(a, b);
}

#endif
