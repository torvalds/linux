/*===-- divsc3.c - Implement __divsc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __divsc3 for the compiler_rt library.
 *
 *===----------------------------------------------------------------------===
 */

#define SINGLE_PRECISION
#include "fp_lib.h"
#include "int_lib.h"
#include "int_math.h"

/* Returns: the quotient of (a + ib) / (c + id) */

COMPILER_RT_ABI Fcomplex
__divsc3(float __a, float __b, float __c, float __d)
{
    int __ilogbw = 0;
    float __logbw =
        __compiler_rt_logbf(crt_fmaxf(crt_fabsf(__c), crt_fabsf(__d)));
    if (crt_isfinite(__logbw))
    {
        __ilogbw = (int)__logbw;
        __c = crt_scalbnf(__c, -__ilogbw);
        __d = crt_scalbnf(__d, -__ilogbw);
    }
    float __denom = __c * __c + __d * __d;
    Fcomplex z;
    COMPLEX_REAL(z) = crt_scalbnf((__a * __c + __b * __d) / __denom, -__ilogbw);
    COMPLEX_IMAGINARY(z) = crt_scalbnf((__b * __c - __a * __d) / __denom, -__ilogbw);
    if (crt_isnan(COMPLEX_REAL(z)) && crt_isnan(COMPLEX_IMAGINARY(z)))
    {
        if ((__denom == 0) && (!crt_isnan(__a) || !crt_isnan(__b)))
        {
            COMPLEX_REAL(z) = crt_copysignf(CRT_INFINITY, __c) * __a;
            COMPLEX_IMAGINARY(z) = crt_copysignf(CRT_INFINITY, __c) * __b;
        }
        else if ((crt_isinf(__a) || crt_isinf(__b)) &&
                 crt_isfinite(__c) && crt_isfinite(__d))
        {
            __a = crt_copysignf(crt_isinf(__a) ? 1 : 0, __a);
            __b = crt_copysignf(crt_isinf(__b) ? 1 : 0, __b);
            COMPLEX_REAL(z) = CRT_INFINITY * (__a * __c + __b * __d);
            COMPLEX_IMAGINARY(z) = CRT_INFINITY * (__b * __c - __a * __d);
        }
        else if (crt_isinf(__logbw) && __logbw > 0 &&
                 crt_isfinite(__a) && crt_isfinite(__b))
        {
            __c = crt_copysignf(crt_isinf(__c) ? 1 : 0, __c);
            __d = crt_copysignf(crt_isinf(__d) ? 1 : 0, __d);
            COMPLEX_REAL(z) = 0 * (__a * __c + __b * __d);
            COMPLEX_IMAGINARY(z) = 0 * (__b * __c - __a * __d);
        }
    }
    return z;
}
