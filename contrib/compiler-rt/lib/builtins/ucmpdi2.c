/* ===-- ucmpdi2.c - Implement __ucmpdi2 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __ucmpdi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns:  if (a <  b) returns 0
 *           if (a == b) returns 1
 *           if (a >  b) returns 2
 */

COMPILER_RT_ABI si_int
__ucmpdi2(du_int a, du_int b)
{
    udwords x;
    x.all = a;
    udwords y;
    y.all = b;
    if (x.s.high < y.s.high)
        return 0;
    if (x.s.high > y.s.high)
        return 2;
    if (x.s.low < y.s.low)
        return 0;
    if (x.s.low > y.s.low)
        return 2;
    return 1;
}

#ifdef __ARM_EABI__
/* Returns: if (a <  b) returns -1
*           if (a == b) returns  0
*           if (a >  b) returns  1
*/
COMPILER_RT_ABI si_int
__aeabi_ulcmp(di_int a, di_int b)
{
	return __ucmpdi2(a, b) - 1;
}
#endif

