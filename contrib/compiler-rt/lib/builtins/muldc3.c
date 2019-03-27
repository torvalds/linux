/* ===-- muldc3.c - Implement __muldc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __muldc3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"
#include "int_math.h"

/* Returns: the product of a + ib and c + id */

COMPILER_RT_ABI Dcomplex
__muldc3(double __a, double __b, double __c, double __d)
{
    double __ac = __a * __c;
    double __bd = __b * __d;
    double __ad = __a * __d;
    double __bc = __b * __c;
    Dcomplex z;
    COMPLEX_REAL(z) = __ac - __bd;
    COMPLEX_IMAGINARY(z) = __ad + __bc;
    if (crt_isnan(COMPLEX_REAL(z)) && crt_isnan(COMPLEX_IMAGINARY(z)))
    {
        int __recalc = 0;
        if (crt_isinf(__a) || crt_isinf(__b))
        {
            __a = crt_copysign(crt_isinf(__a) ? 1 : 0, __a);
            __b = crt_copysign(crt_isinf(__b) ? 1 : 0, __b);
            if (crt_isnan(__c))
                __c = crt_copysign(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysign(0, __d);
            __recalc = 1;
        }
        if (crt_isinf(__c) || crt_isinf(__d))
        {
            __c = crt_copysign(crt_isinf(__c) ? 1 : 0, __c);
            __d = crt_copysign(crt_isinf(__d) ? 1 : 0, __d);
            if (crt_isnan(__a))
                __a = crt_copysign(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysign(0, __b);
            __recalc = 1;
        }
        if (!__recalc && (crt_isinf(__ac) || crt_isinf(__bd) ||
                          crt_isinf(__ad) || crt_isinf(__bc)))
        {
            if (crt_isnan(__a))
                __a = crt_copysign(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysign(0, __b);
            if (crt_isnan(__c))
                __c = crt_copysign(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysign(0, __d);
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
