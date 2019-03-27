/* ===-- fixunsdfti.c - Implement __fixunsdfti -----------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT
#define DOUBLE_PRECISION
#include "fp_lib.h"
typedef tu_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI tu_int
__fixunsdfti(fp_t a) {
    return __fixuint(a);
}
#endif /* CRT_HAS_128BIT */
