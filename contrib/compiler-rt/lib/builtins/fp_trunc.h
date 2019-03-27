//=== lib/fp_trunc.h - high precision -> low precision conversion *- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Set source and destination precision setting
//
//===----------------------------------------------------------------------===//

#ifndef FP_TRUNC_HEADER
#define FP_TRUNC_HEADER

#include "int_lib.h"

#if defined SRC_SINGLE
typedef float src_t;
typedef uint32_t src_rep_t;
#define SRC_REP_C UINT32_C
static const int srcSigBits = 23;

#elif defined SRC_DOUBLE
typedef double src_t;
typedef uint64_t src_rep_t;
#define SRC_REP_C UINT64_C
static const int srcSigBits = 52;

#elif defined SRC_QUAD
typedef long double src_t;
typedef __uint128_t src_rep_t;
#define SRC_REP_C (__uint128_t)
static const int srcSigBits = 112;

#else
#error Source should be double precision or quad precision!
#endif //end source precision

#if defined DST_DOUBLE
typedef double dst_t;
typedef uint64_t dst_rep_t;
#define DST_REP_C UINT64_C
static const int dstSigBits = 52;

#elif defined DST_SINGLE
typedef float dst_t;
typedef uint32_t dst_rep_t;
#define DST_REP_C UINT32_C
static const int dstSigBits = 23;

#elif defined DST_HALF
typedef uint16_t dst_t;
typedef uint16_t dst_rep_t;
#define DST_REP_C UINT16_C
static const int dstSigBits = 10;

#else
#error Destination should be single precision or double precision!
#endif //end destination precision

// End of specialization parameters.  Two helper routines for conversion to and
// from the representation of floating-point data as integer values follow.

static __inline src_rep_t srcToRep(src_t x) {
    const union { src_t f; src_rep_t i; } rep = {.f = x};
    return rep.i;
}

static __inline dst_t dstFromRep(dst_rep_t x) {
    const union { dst_t f; dst_rep_t i; } rep = {.i = x};
    return rep.f;
}

#endif // FP_TRUNC_HEADER
