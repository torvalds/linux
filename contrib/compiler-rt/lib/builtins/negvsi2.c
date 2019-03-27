/* ===-- negvsi2.c - Implement __negvsi2 -----------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __negvsi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: -a */

/* Effects: aborts if -a overflows */

COMPILER_RT_ABI si_int
__negvsi2(si_int a)
{
    const si_int MIN = (si_int)1 << ((int)(sizeof(si_int) * CHAR_BIT)-1);
    if (a == MIN)
        compilerrt_abort();
    return -a;
}
