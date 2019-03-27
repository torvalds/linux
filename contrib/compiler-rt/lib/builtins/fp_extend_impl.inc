//=-lib/fp_extend_impl.inc - low precision -> high precision conversion -*-- -//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a fairly generic conversion from a narrower to a wider
// IEEE-754 floating-point type.  The constants and types defined following the
// includes below parameterize the conversion.
//
// It does not support types that don't use the usual IEEE-754 interchange
// formats; specifically, some work would be needed to adapt it to
// (for example) the Intel 80-bit format or PowerPC double-double format.
//
// Note please, however, that this implementation is only intended to support
// *widening* operations; if you need to convert to a *narrower* floating-point
// type (e.g. double -> float), then this routine will not do what you want it
// to.
//
// It also requires that integer types at least as large as both formats
// are available on the target platform; this may pose a problem when trying
// to add support for quad on some 32-bit systems, for example.  You also may
// run into trouble finding an appropriate CLZ function for wide source types;
// you will likely need to roll your own on some platforms.
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

#include "fp_extend.h"

static __inline dst_t __extendXfYf2__(src_t a) {
    // Various constants whose values follow from the type parameters.
    // Any reasonable optimizer will fold and propagate all of these.
    const int srcBits = sizeof(src_t)*CHAR_BIT;
    const int srcExpBits = srcBits - srcSigBits - 1;
    const int srcInfExp = (1 << srcExpBits) - 1;
    const int srcExpBias = srcInfExp >> 1;

    const src_rep_t srcMinNormal = SRC_REP_C(1) << srcSigBits;
    const src_rep_t srcInfinity = (src_rep_t)srcInfExp << srcSigBits;
    const src_rep_t srcSignMask = SRC_REP_C(1) << (srcSigBits + srcExpBits);
    const src_rep_t srcAbsMask = srcSignMask - 1;
    const src_rep_t srcQNaN = SRC_REP_C(1) << (srcSigBits - 1);
    const src_rep_t srcNaNCode = srcQNaN - 1;

    const int dstBits = sizeof(dst_t)*CHAR_BIT;
    const int dstExpBits = dstBits - dstSigBits - 1;
    const int dstInfExp = (1 << dstExpBits) - 1;
    const int dstExpBias = dstInfExp >> 1;

    const dst_rep_t dstMinNormal = DST_REP_C(1) << dstSigBits;

    // Break a into a sign and representation of the absolute value
    const src_rep_t aRep = srcToRep(a);
    const src_rep_t aAbs = aRep & srcAbsMask;
    const src_rep_t sign = aRep & srcSignMask;
    dst_rep_t absResult;

    // If sizeof(src_rep_t) < sizeof(int), the subtraction result is promoted
    // to (signed) int.  To avoid that, explicitly cast to src_rep_t.
    if ((src_rep_t)(aAbs - srcMinNormal) < srcInfinity - srcMinNormal) {
        // a is a normal number.
        // Extend to the destination type by shifting the significand and
        // exponent into the proper position and rebiasing the exponent.
        absResult = (dst_rep_t)aAbs << (dstSigBits - srcSigBits);
        absResult += (dst_rep_t)(dstExpBias - srcExpBias) << dstSigBits;
    }

    else if (aAbs >= srcInfinity) {
        // a is NaN or infinity.
        // Conjure the result by beginning with infinity, then setting the qNaN
        // bit (if needed) and right-aligning the rest of the trailing NaN
        // payload field.
        absResult = (dst_rep_t)dstInfExp << dstSigBits;
        absResult |= (dst_rep_t)(aAbs & srcQNaN) << (dstSigBits - srcSigBits);
        absResult |= (dst_rep_t)(aAbs & srcNaNCode) << (dstSigBits - srcSigBits);
    }

    else if (aAbs) {
        // a is denormal.
        // renormalize the significand and clear the leading bit, then insert
        // the correct adjusted exponent in the destination type.
        const int scale = src_rep_t_clz(aAbs) - src_rep_t_clz(srcMinNormal);
        absResult = (dst_rep_t)aAbs << (dstSigBits - srcSigBits + scale);
        absResult ^= dstMinNormal;
        const int resultExponent = dstExpBias - srcExpBias - scale + 1;
        absResult |= (dst_rep_t)resultExponent << dstSigBits;
    }

    else {
        // a is zero.
        absResult = 0;
    }

    // Apply the signbit to (dst_t)abs(a).
    const dst_rep_t result = absResult | (dst_rep_t)sign << (dstBits - srcBits);
    return dstFromRep(result);
}
