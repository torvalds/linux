/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_TIMEX_H
#define _ASM_POWERPC_TIMEX_H

#ifdef __KERNEL__

/*
 * PowerPC architecture timex specifications
 */

#include <asm/cputable.h>
#include <asm/vdso/timebase.h>

#define CLOCK_TICK_RATE	1024000 /* Underlying HZ */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles(void)
{
	return mftb();
}

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_TIMEX_H */
