//===-- lib/subtf3.c - Quad-precision subtraction -----------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements quad-precision soft-float subtraction with the
// IEEE-754 default rounding (to nearest, ties to even).
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
COMPILER_RT_ABI fp_t __addtf3(fp_t a, fp_t b);

// Subtraction; flip the sign bit of b and add.
COMPILER_RT_ABI fp_t
__subtf3(fp_t a, fp_t b) {
    return __addtf3(a, fromRep(toRep(b) ^ signBit));
}

#endif
