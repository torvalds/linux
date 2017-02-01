/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Delay routines using pre computed loops_per_jiffy value.
 *
 * vineetg: Feb 2012
 *  -Rewrote in "C" to avoid dealing with availability of H/w MPY
 *  -Also reduced the num of MPY operations from 3 to 2
 *
 * Amit Bhor: Codito Technologies 2004
 */

#ifndef __ASM_ARC_UDELAY_H
#define __ASM_ARC_UDELAY_H

#include <asm/param.h>		/* HZ */

static inline void __delay(unsigned long loops)
{
	__asm__ __volatile__(
	"	mov lp_count, %0	\n"
	"	lp  1f			\n"
	"	nop			\n"
	"1:				\n"
	:
        : "r"(loops)
        : "lp_count");
}

extern void __bad_udelay(void);

/*
 * Normal Math for computing loops in "N" usecs
 *  -we have precomputed @loops_per_jiffy
 *  -1 sec has HZ jiffies
 * loops per "N" usecs = ((loops_per_jiffy * HZ / 1000000) * N)
 *
 * Approximate Division by multiplication:
 *  -Mathematically if we multiply and divide a number by same value the
 *   result remains unchanged:  In this case, we use 2^32
 *  -> (loops_per_N_usec * 2^32 ) / 2^32
 *  -> (((loops_per_jiffy * HZ / 1000000) * N) * 2^32) / 2^32
 *  -> (loops_per_jiffy * HZ * N * 4295) / 2^32
 *
 *  -Divide by 2^32 is very simply right shift by 32
 *  -We simply need to ensure that the multiply per above eqn happens in
 *   64-bit precision (if CPU doesn't support it - gcc can emaulate it)
 */

static inline void __udelay(unsigned long usecs)
{
	unsigned long loops;

	/* (u64) cast ensures 64 bit MPY - real or emulated
	 * HZ * 4295 is pre-evaluated by gcc - hence only 2 mpy ops
	 */
	loops = ((u64) usecs * 4295 * HZ * loops_per_jiffy) >> 32;

	__delay(loops);
}

#define udelay(n) (__builtin_constant_p(n) ? ((n) > 20000 ? __bad_udelay() \
				: __udelay(n)) : __udelay(n))

#endif /* __ASM_ARC_UDELAY_H */
