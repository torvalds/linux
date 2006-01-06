/*
 * This file contains the implementation of the
 * Linux re-IPL support
 *
 * (C) Copyright IBM Corp. 2005
 *
 * Author(s): Volker Sameske (sameske@de.ibm.com)
 *
 */

#include <linux/kernel.h>

static unsigned int reipl_diag_rc1;
static unsigned int reipl_diag_rc2;

/*
 * re-IPL the system using the last used IPL parameters
 */
void reipl_diag(void)
{
        asm volatile (
		"   la   %%r4,0\n"
		"   la   %%r5,0\n"
                "   diag %%r4,%2,0x308\n"
                "0:\n"
		"   st   %%r4,%0\n"
		"   st   %%r5,%1\n"
                ".section __ex_table,\"a\"\n"
#ifdef CONFIG_64BIT
                "   .align 8\n"
                "   .quad 0b, 0b\n"
#else
                "   .align 4\n"
                "   .long 0b, 0b\n"
#endif
                ".previous\n"
                : "=m" (reipl_diag_rc1), "=m" (reipl_diag_rc2)
		: "d" (3) : "cc", "4", "5" );
}
