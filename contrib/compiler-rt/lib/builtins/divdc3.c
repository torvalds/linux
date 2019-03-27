/* ===-- divdc3.c - Implement __divdc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __divdc3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#define DOUBLE_PRECISION
#include "fp_lib.h"
#include "int_lib.h"
#include "int_math.h"

/* Returns: the quotient of (a + ib) / (c + id) */

COMPILER_RT_ABI Dcomplex
__divdc3(double __a, double __b, double __c, double __d)
{
    int __ilogbw = 0;
    double __logbw = __compiler_rt_logb(crt_fmax(crt_fabs(__c), crt_fabs(__d)));
    if (crt_isfinite(__logbw))
    {
        __ilogbw = (int)__logbw;
        __c = crt_scalbn(__c, -__ilogbw);
        __d = crt_scalbn(__d, -__ilogbw);
    }
    double __denom = __c * __c + __d * __d;
    Dcomplex z;
    COMPLEX_REAL(z) = crt_scalbn((__a * __c + __b * __d) / __denom, -__ilogbw);
    COMPLEX_IMAGINARY(z) = crt_scalbn((__b * __c - __a * __d) / __denom, -__ilogbw);
    if (crt_isnan(COMPLEX_REAL(z)) && crt_isnan(COMPLEX_IMAGINARY(z)))
    {
        if ((__denom == 0.0) && (!crt_isnan(__a) || !crt_isnan(__b)))
        {
            COMPLEX_REAL(z) = crt_copysign(CRT_INFINITY, __c) * __a;
            COMPLEX_IMAGINARY(z) = crt_copysign(CRT_INFINITY, __c) * __b;
        }
        else if ((crt_isinf(__a) || crt_isinf(__b)) &&
                 crt_isfinite(__c) && crt_isfinite(__d))
        {
            __a = crt_copysign(crt_isinf(__a) ? 1.0 : 0.0, __a);
            __b = crt_copysign(crt_isinf(__b) ? 1.0 : 0.0, __b);
            COMPLEX_REAL(z) = CRT_INFINITY * (__a * __c + __b * __d);
            COMPLEX_IMAGINARY(z) = CRT_INFINITY * (__b * __c - __a * __d);
        }
        else if (crt_isinf(__logbw) && __logbw > 0.0 &&
                 crt_isfinite(__a) && crt_isfinite(__b))
        {
            __c = crt_copysign(crt_isinf(__c) ? 1.0 : 0.0, __c);
            __d = crt_copysign(crt_isinf(__d) ? 1.0 : 0.0, __d);
            COMPLEX_REAL(z) = 0.0 * (__a * __c + __b * __d);
            COMPLEX_IMAGINARY(z) = 0.0 * (__b * __c - __a * __d);
        }
    }
    return z;
}
