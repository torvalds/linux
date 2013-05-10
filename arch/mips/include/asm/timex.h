/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 1999, 2003 by Ralf Baechle
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#ifdef __KERNEL__

#include <asm/mipsregs.h>

/*
 * This is the clock rate of the i8253 PIT.  A MIPS system may not have
 * a PIT by the symbol is used all over the kernel including some APIs.
 * So keeping it defined to the number for the PIT is the only sane thing
 * for now.
 */
#define CLOCK_TICK_RATE 1193182

/*
 * Standard way to access the cycle counter.
 * Currently only used on SMP for scheduling.
 *
 * Only the low 32 bits are available as a continuously counting entity.
 * But this only means we'll force a reschedule every 8 seconds or so,
 * which isn't an evil thing.
 *
 * We know that all SMP capable CPUs have cycle counters.
 */

typedef unsigned int cycles_t;

static inline cycles_t get_cycles(void)
{
	return 0;
}

#endif /* __KERNEL__ */

#endif /*  _ASM_TIMEX_H */
