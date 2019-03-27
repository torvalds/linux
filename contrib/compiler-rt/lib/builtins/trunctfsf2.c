//===-- lib/trunctfsf2.c - quad -> single conversion --------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
#define SRC_QUAD
#define DST_SINGLE
#include "fp_trunc_impl.inc"

COMPILER_RT_ABI float __trunctfsf2(long double a) {
    return __truncXfYf2__(a);
}

#endif
