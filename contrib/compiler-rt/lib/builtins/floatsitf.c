//===-- lib/floatsitf.c - integer -> quad-precision conversion ----*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements integer to quad-precision conversion for the
// compiler-rt library in the IEEE-754 default round-to-nearest, ties-to-even
// mode.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
COMPILER_RT_ABI fp_t __floatsitf(int a) {

    const int aWidth = sizeof a * CHAR_BIT;

    // Handle zero as a special case to protect clz
    if (a == 0)
        return fromRep(0);

    // All other cases begin by extracting the sign and absolute value of a
    rep_t sign = 0;
    unsigned aAbs = (unsigned)a;
    if (a < 0) {
        sign = signBit;
        aAbs = ~(unsigned)a + 1U;
    }

    // Exponent of (fp_t)a is the width of abs(a).
    const int exponent = (aWidth - 1) - __builtin_clz(aAbs);
    rep_t result;

    // Shift a into the significand field and clear the implicit bit.
    const int shift = significandBits - exponent;
    result = (rep_t)aAbs << shift ^ implicitBit;

    // Insert the exponent
    result += (rep_t)(exponent + exponentBias) << significandBits;
    // Insert the sign bit and return
    return fromRep(result | sign);
}

#endif
