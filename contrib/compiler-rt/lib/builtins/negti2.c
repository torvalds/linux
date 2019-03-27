/* ===-- negti2.c - Implement __negti2 -------------------------------------===
 *
 *      	       The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __negti2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Returns: -a */

COMPILER_RT_ABI ti_int
__negti2(ti_int a)
{
    /* Note: this routine is here for API compatibility; any sane compiler
     * should expand it inline.
     */
    return -a;
}

#endif /* CRT_HAS_128BIT */
