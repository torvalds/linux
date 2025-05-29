/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_DIAG288_H
#define _ASM_S390_DIAG288_H

#include <asm/asm-extable.h>
#include <asm/types.h>

#define MIN_INTERVAL 15	    /* Minimal time supported by diag288 */
#define MAX_INTERVAL 3600   /* One hour should be enough - pure estimation */

#define WDT_DEFAULT_TIMEOUT 30

/* Function codes - init, change, cancel */
#define WDT_FUNC_INIT 0
#define WDT_FUNC_CHANGE 1
#define WDT_FUNC_CANCEL 2
#define WDT_FUNC_CONCEAL 0x80000000

/* Action codes for LPAR watchdog */
#define LPARWDT_RESTART 0

static inline int __diag288(unsigned int func, unsigned int timeout,
			    unsigned long action, unsigned int len)
{
	union register_pair r1 = { .even = func, .odd = timeout, };
	union register_pair r3 = { .even = action, .odd = len, };
	int rc = -EINVAL;

	asm volatile(
		"	diag	%[r1],%[r3],0x288\n"
		"0:	lhi	%[rc],0\n"
		"1:"
		EX_TABLE(0b, 1b)
		: [rc] "+d" (rc)
		: [r1] "d" (r1.pair), [r3] "d" (r3.pair)
		: "cc", "memory");
	return rc;
}

#endif /* _ASM_S390_DIAG288_H */
