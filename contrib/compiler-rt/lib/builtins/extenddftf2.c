//===-- lib/extenddftf2.c - double -> quad conversion -------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
#define SRC_DOUBLE
#define DST_QUAD
#include "fp_extend_impl.inc"

COMPILER_RT_ABI long double __extenddftf2(double a) {
    return __extendXfYf2__(a);
}

#endif
