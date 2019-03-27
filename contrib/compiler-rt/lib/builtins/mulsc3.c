/* ===-- mulsc3.c - Implement __mulsc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __mulsc3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"
#include "int_math.h"

/* Returns: the product of a + ib and c + id */

COMPILER_RT_ABI Fcomplex
__mulsc3(float __a, float __b, float __c, float __d)
{
    float __ac = __a * __c;
    float __bd = __b * __d;
    float __ad = __a * __d;
    float __bc = __b * __c;
    Fcomplex z;
    COMPLEX_REAL(z) = __ac - __bd;
    COMPLEX_IMAGINARY(z) = __ad + __bc;
    if (crt_isnan(COMPLEX_REAL(z)) && crt_isnan(COMPLEX_IMAGINARY(z)))
    {
        int __recalc = 0;
        if (crt_isinf(__a) || crt_isinf(__b))
        {
            __a = crt_copysignf(crt_isinf(__a) ? 1 : 0, __a);
            __b = crt_copysignf(crt_isinf(__b) ? 1 : 0, __b);
            if (crt_isnan(__c))
                __c = crt_copysignf(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysignf(0, __d);
            __recalc = 1;
        }
        if (crt_isinf(__c) || crt_isinf(__d))
        {
            __c = crt_copysignf(crt_isinf(__c) ? 1 : 0, __c);
            __d = crt_copysignf(crt_isinf(__d) ? 1 : 0, __d);
            if (crt_isnan(__a))
                __a = crt_copysignf(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysignf(0, __b);
            __recalc = 1;
        }
        if (!__recalc && (crt_isinf(__ac) || crt_isinf(__bd) ||
                          crt_isinf(__ad) || crt_isinf(__bc)))
        {
            if (crt_isnan(__a))
                __a = crt_copysignf(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysignf(0, __b);
            if (crt_isnan(__c))
                __c = crt_copysignf(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysignf(0, __d);
            __recalc = 1;
        }
        if (__recalc)
        {
            COMPLEX_REAL(z) = CRT_INFINITY * (__a * __c - __b * __d);
            COMPLEX_IMAGINARY(z) = CRT_INFINITY * (__a * __d + __b * __c);
        }
    }
    return z;
}
