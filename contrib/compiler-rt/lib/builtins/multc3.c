/* ===-- multc3.c - Implement __multc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __multc3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"
#include "int_math.h"

/* Returns: the product of a + ib and c + id */

COMPILER_RT_ABI long double _Complex
__multc3(long double a, long double b, long double c, long double d)
{
    long double ac = a * c;
    long double bd = b * d;
    long double ad = a * d;
    long double bc = b * c;
    long double _Complex z;
    __real__ z = ac - bd;
    __imag__ z = ad + bc;
    if (crt_isnan(__real__ z) && crt_isnan(__imag__ z)) {
        int recalc = 0;
        if (crt_isinf(a) || crt_isinf(b)) {
            a = crt_copysignl(crt_isinf(a) ? 1 : 0, a);
            b = crt_copysignl(crt_isinf(b) ? 1 : 0, b);
            if (crt_isnan(c))
                c = crt_copysignl(0, c);
            if (crt_isnan(d))
                d = crt_copysignl(0, d);
            recalc = 1;
        }
        if (crt_isinf(c) || crt_isinf(d)) {
            c = crt_copysignl(crt_isinf(c) ? 1 : 0, c);
            d = crt_copysignl(crt_isinf(d) ? 1 : 0, d);
            if (crt_isnan(a))
                a = crt_copysignl(0, a);
            if (crt_isnan(b))
                b = crt_copysignl(0, b);
            recalc = 1;
        }
        if (!recalc && (crt_isinf(ac) || crt_isinf(bd) ||
                          crt_isinf(ad) || crt_isinf(bc))) {
            if (crt_isnan(a))
                a = crt_copysignl(0, a);
            if (crt_isnan(b))
                b = crt_copysignl(0, b);
            if (crt_isnan(c))
                c = crt_copysignl(0, c);
            if (crt_isnan(d))
                d = crt_copysignl(0, d);
            recalc = 1;
        }
        if (recalc) {
            __real__ z = CRT_INFINITY * (a * c - b * d);
            __imag__ z = CRT_INFINITY * (a * d + b * c);
        }
    }
    return z;
}
