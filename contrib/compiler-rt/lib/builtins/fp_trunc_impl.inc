//= lib/fp_trunc_impl.inc - high precision -> low precision conversion *-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a fairly generic conversion from a wider to a narrower
// IEEE-754 floating-point type in the default (round to nearest, ties to even)
// rounding mode.  The constants and types defined following the includes below
// parameterize the conversion.
//
// This routine can be trivially adapted to support conversions to
// half-precision or from quad-precision. It does not support types that don't
// use the usual IEEE-754 interchange formats; specifically, some work would be
// needed to adapt it to (for example) the Intel 80-bit format or PowerPC
// double-double format.
//
// Note please, however, that this implementation is only intended to support
// *narrowing* operations; if you need to convert to a *wider* floating-point
// type (e.g. float -> double), then this routine will not do what you want it
// to.
//
// It also requires that integer types at least as large as both formats
// are available on the target platform; this may pose a problem when trying
// to add support for quad on some 32-bit systems, for example.
//
// Finally, the following assumptions are made:
//
// 1. floating-point types and integer types have the same endianness on the
//    target platform
//
// 2. quiet NaNs, if supported, are indicated by the leading bit of the
//    significand field being set
//
//===----------------------------------------------------------------------===//

#include "fp_trunc.h"

static __inline dst_t __truncXfYf2__(src_t a) {
    // Various constants whose values follow from the type parameters.
    // Any reasonable optimizer will fold and propagate all of these.
    const int srcBits = sizeof(src_t)*CHAR_BIT;
    const int srcExpBits = srcBits - srcSigBits - 1;
    const int srcInfExp = (1 << srcExpBits) - 1;
    const int srcExpBias = srcInfExp >> 1;

    const src_rep_t srcMinNormal = SRC_REP_C(1) << srcSigBits;
    const src_rep_t srcSignificandMask = srcMinNormal - 1;
    const src_rep_t srcInfinity = (src_rep_t)srcInfExp << srcSigBits;
    const src_rep_t srcSignMask = SRC_REP_C(1) << (srcSigBits + srcExpBits);
    const src_rep_t srcAbsMask = srcSignMask - 1;
    const src_rep_t roundMask = (SRC_REP_C(1) << (srcSigBits - dstSigBits)) - 1;
    const src_rep_t halfway = SRC_REP_C(1) << (srcSigBits - dstSigBits - 1);
    const src_rep_t srcQNaN = SRC_REP_C(1) << (srcSigBits - 1);
    const src_rep_t srcNaNCode = srcQNaN - 1;

    const int dstBits = sizeof(dst_t)*CHAR_BIT;
    const int dstExpBits = dstBits - dstSigBits - 1;
    const int dstInfExp = (1 << dstExpBits) - 1;
    const int dstExpBias = dstInfExp >> 1;

    const int underflowExponent = srcExpBias + 1 - dstExpBias;
    const int overflowExponent = srcExpBias + dstInfExp - dstExpBias;
    const src_rep_t underflow = (src_rep_t)underflowExponent << srcSigBits;
    const src_rep_t overflow = (src_rep_t)overflowExponent << srcSigBits;

    const dst_rep_t dstQNaN = DST_REP_C(1) << (dstSigBits - 1);
    const dst_rep_t dstNaNCode = dstQNaN - 1;

    // Break a into a sign and representation of the absolute value
    const src_rep_t aRep = srcToRep(a);
    const src_rep_t aAbs = aRep & srcAbsMask;
    const src_rep_t sign = aRep & srcSignMask;
    dst_rep_t absResult;

    if (aAbs - underflow < aAbs - overflow) {
        // The exponent of a is within the range of normal numbers in the
        // destination format.  We can convert by simply right-shifting with
        // rounding and adjusting the exponent.
        absResult = aAbs >> (srcSigBits - dstSigBits);
        absResult -= (dst_rep_t)(srcExpBias - dstExpBias) << dstSigBits;

        const src_rep_t roundBits = aAbs & roundMask;
        // Round to nearest
        if (roundBits > halfway)
            absResult++;
        // Ties to even
        else if (roundBits == halfway)
            absResult += absResult & 1;
    }
    else if (aAbs > srcInfinity) {
        // a is NaN.
        // Conjure the result by beginning with infinity, setting the qNaN
        // bit and inserting the (truncated) trailing NaN field.
        absResult = (dst_rep_t)dstInfExp << dstSigBits;
        absResult |= dstQNaN;
        absResult |= ((aAbs & srcNaNCode) >> (srcSigBits - dstSigBits)) & dstNaNCode;
    }
    else if (aAbs >= overflow) {
        // a overflows to infinity.
        absResult = (dst_rep_t)dstInfExp << dstSigBits;
    }
    else {
        // a underflows on conversion to the destination type or is an exact
        // zero.  The result may be a denormal or zero.  Extract the exponent
        // to get the shift amount for the denormalization.
        const int aExp = aAbs >> srcSigBits;
        const int shift = srcExpBias - dstExpBias - aExp + 1;

        const src_rep_t significand = (aRep & srcSignificandMask) | srcMinNormal;

        // Right shift by the denormalization amount with sticky.
        if (shift > srcSigBits) {
            absResult = 0;
        } else {
            const bool sticky = significand << (srcBits - shift);
            src_rep_t denormalizedSignificand = significand >> shift | sticky;
            absResult = denormalizedSignificand >> (srcSigBits - dstSigBits);
            const src_rep_t roundBits = denormalizedSignificand & roundMask;
            // Round to nearest
            if (roundBits > halfway)
                absResult++;
            // Ties to even
            else if (roundBits == halfway)
                absResult += absResult & 1;
        }
    }

    // Apply the signbit to (dst_t)abs(a).
    const dst_rep_t result = absResult | sign >> (srcBits - dstBits);
    return dstFromRep(result);
}
