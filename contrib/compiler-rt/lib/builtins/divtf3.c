//===-- lib/divtf3.c - Quad-precision division --------------------*- C -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements quad-precision soft-float division
// with the IEEE-754 default rounding (to nearest, ties to even).
//
// For simplicity, this implementation currently flushes denormals to zero.
// It should be a fairly straightforward exercise to implement gradual
// underflow with correct rounding.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
COMPILER_RT_ABI fp_t __divtf3(fp_t a, fp_t b) {

    const unsigned int aExponent = toRep(a) >> significandBits & maxExponent;
    const unsigned int bExponent = toRep(b) >> significandBits & maxExponent;
    const rep_t quotientSign = (toRep(a) ^ toRep(b)) & signBit;

    rep_t aSignificand = toRep(a) & significandMask;
    rep_t bSignificand = toRep(b) & significandMask;
    int scale = 0;

    // Detect if a or b is zero, denormal, infinity, or NaN.
    if (aExponent-1U >= maxExponent-1U || bExponent-1U >= maxExponent-1U) {

        const rep_t aAbs = toRep(a) & absMask;
        const rep_t bAbs = toRep(b) & absMask;

        // NaN / anything = qNaN
        if (aAbs > infRep) return fromRep(toRep(a) | quietBit);
        // anything / NaN = qNaN
        if (bAbs > infRep) return fromRep(toRep(b) | quietBit);

        if (aAbs == infRep) {
            // infinity / infinity = NaN
            if (bAbs == infRep) return fromRep(qnanRep);
            // infinity / anything else = +/- infinity
            else return fromRep(aAbs | quotientSign);
        }

        // anything else / infinity = +/- 0
        if (bAbs == infRep) return fromRep(quotientSign);

        if (!aAbs) {
            // zero / zero = NaN
            if (!bAbs) return fromRep(qnanRep);
            // zero / anything else = +/- zero
            else return fromRep(quotientSign);
        }
        // anything else / zero = +/- infinity
        if (!bAbs) return fromRep(infRep | quotientSign);

        // one or both of a or b is denormal, the other (if applicable) is a
        // normal number.  Renormalize one or both of a and b, and set scale to
        // include the necessary exponent adjustment.
        if (aAbs < implicitBit) scale += normalize(&aSignificand);
        if (bAbs < implicitBit) scale -= normalize(&bSignificand);
    }

    // Or in the implicit significand bit.  (If we fell through from the
    // denormal path it was already set by normalize( ), but setting it twice
    // won't hurt anything.)
    aSignificand |= implicitBit;
    bSignificand |= implicitBit;
    int quotientExponent = aExponent - bExponent + scale;

    // Align the significand of b as a Q63 fixed-point number in the range
    // [1, 2.0) and get a Q64 approximate reciprocal using a small minimax
    // polynomial approximation: reciprocal = 3/4 + 1/sqrt(2) - b/2.  This
    // is accurate to about 3.5 binary digits.
    const uint64_t q63b = bSignificand >> 49;
    uint64_t recip64 = UINT64_C(0x7504f333F9DE6484) - q63b;
    // 0x7504f333F9DE6484 / 2^64 + 1 = 3/4 + 1/sqrt(2)

    // Now refine the reciprocal estimate using a Newton-Raphson iteration:
    //
    //     x1 = x0 * (2 - x0 * b)
    //
    // This doubles the number of correct binary digits in the approximation
    // with each iteration.
    uint64_t correction64;
    correction64 = -((rep_t)recip64 * q63b >> 64);
    recip64 = (rep_t)recip64 * correction64 >> 63;
    correction64 = -((rep_t)recip64 * q63b >> 64);
    recip64 = (rep_t)recip64 * correction64 >> 63;
    correction64 = -((rep_t)recip64 * q63b >> 64);
    recip64 = (rep_t)recip64 * correction64 >> 63;
    correction64 = -((rep_t)recip64 * q63b >> 64);
    recip64 = (rep_t)recip64 * correction64 >> 63;
    correction64 = -((rep_t)recip64 * q63b >> 64);
    recip64 = (rep_t)recip64 * correction64 >> 63;

    // recip64 might have overflowed to exactly zero in the preceeding
    // computation if the high word of b is exactly 1.0.  This would sabotage
    // the full-width final stage of the computation that follows, so we adjust
    // recip64 downward by one bit.
    recip64--;

    // We need to perform one more iteration to get us to 112 binary digits;
    // The last iteration needs to happen with extra precision.
    const uint64_t q127blo = bSignificand << 15;
    rep_t correction, reciprocal;

    // NOTE: This operation is equivalent to __multi3, which is not implemented
    //       in some architechure
    rep_t r64q63, r64q127, r64cH, r64cL, dummy;
    wideMultiply((rep_t)recip64, (rep_t)q63b, &dummy, &r64q63);
    wideMultiply((rep_t)recip64, (rep_t)q127blo, &dummy, &r64q127);

    correction = -(r64q63 + (r64q127 >> 64));

    uint64_t cHi = correction >> 64;
    uint64_t cLo = correction;

    wideMultiply((rep_t)recip64, (rep_t)cHi, &dummy, &r64cH);
    wideMultiply((rep_t)recip64, (rep_t)cLo, &dummy, &r64cL);

    reciprocal = r64cH + (r64cL >> 64);

    // We already adjusted the 64-bit estimate, now we need to adjust the final
    // 128-bit reciprocal estimate downward to ensure that it is strictly smaller
    // than the infinitely precise exact reciprocal.  Because the computation
    // of the Newton-Raphson step is truncating at every step, this adjustment
    // is small; most of the work is already done.
    reciprocal -= 2;

    // The numerical reciprocal is accurate to within 2^-112, lies in the
    // interval [0.5, 1.0), and is strictly smaller than the true reciprocal
    // of b.  Multiplying a by this reciprocal thus gives a numerical q = a/b
    // in Q127 with the following properties:
    //
    //    1. q < a/b
    //    2. q is in the interval [0.5, 2.0)
    //    3. the error in q is bounded away from 2^-113 (actually, we have a
    //       couple of bits to spare, but this is all we need).

    // We need a 128 x 128 multiply high to compute q, which isn't a basic
    // operation in C, so we need to be a little bit fussy.
    rep_t quotient, quotientLo;
    wideMultiply(aSignificand << 2, reciprocal, &quotient, &quotientLo);

    // Two cases: quotient is in [0.5, 1.0) or quotient is in [1.0, 2.0).
    // In either case, we are going to compute a residual of the form
    //
    //     r = a - q*b
    //
    // We know from the construction of q that r satisfies:
    //
    //     0 <= r < ulp(q)*b
    //
    // if r is greater than 1/2 ulp(q)*b, then q rounds up.  Otherwise, we
    // already have the correct result.  The exact halfway case cannot occur.
    // We also take this time to right shift quotient if it falls in the [1,2)
    // range and adjust the exponent accordingly.
    rep_t residual;
    rep_t qb;

    if (quotient < (implicitBit << 1)) {
        wideMultiply(quotient, bSignificand, &dummy, &qb);
        residual = (aSignificand << 113) - qb;
        quotientExponent--;
    } else {
        quotient >>= 1;
        wideMultiply(quotient, bSignificand, &dummy, &qb);
        residual = (aSignificand << 112) - qb;
    }

    const int writtenExponent = quotientExponent + exponentBias;

    if (writtenExponent >= maxExponent) {
        // If we have overflowed the exponent, return infinity.
        return fromRep(infRep | quotientSign);
    }
    else if (writtenExponent < 1) {
        // Flush denormals to zero.  In the future, it would be nice to add
        // code to round them correctly.
        return fromRep(quotientSign);
    }
    else {
        const bool round = (residual << 1) >= bSignificand;
        // Clear the implicit bit
        rep_t absResult = quotient & significandMask;
        // Insert the exponent
        absResult |= (rep_t)writtenExponent << significandBits;
        // Round
        absResult += round;
        // Insert the sign and return
        const long double result = fromRep(absResult | quotientSign);
        return result;
    }
}

#endif
