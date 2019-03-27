/* ===-- fixunssfdi.c - Implement __fixunssfdi -----------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 */

#define SINGLE_PRECISION
#include "fp_lib.h"

#ifndef __SOFT_FP__
/* Support for systems that have hardware floating-point; can set the invalid
 * flag as a side-effect of computation.
 */

COMPILER_RT_ABI du_int
__fixunssfdi(float a)
{
    if (a <= 0.0f) return 0;
    double da = a;
    su_int high = da / 4294967296.f;               /* da / 0x1p32f; */
    su_int low = da - (double)high * 4294967296.f; /* high * 0x1p32f; */
    return ((du_int)high << 32) | low;
}

#else
/* Support for systems that don't have hardware floating-point; there are no
 * flags to set, and we don't want to code-gen to an unknown soft-float
 * implementation.
 */

typedef du_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI du_int
__fixunssfdi(fp_t a) {
    return __fixuint(a);
}

#endif

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI du_int __aeabi_f2ulz(fp_t a) {
  return __fixunssfdi(a);
}
#else
AEABI_RTABI du_int __aeabi_f2ulz(fp_t a) COMPILER_RT_ALIAS(__fixunssfdi);
#endif
#endif
