/*
 * delay.h - delay functions
 *
 * Copyright (c) 2004-2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_DELAY_H__
#define __ASM_DELAY_H__

#include <mach/anomaly.h>

static inline void __delay(unsigned long loops)
{
	if (ANOMALY_05000312) {
		/* Interrupted loads to loop registers -> bad */
		unsigned long tmp;
		__asm__ __volatile__(
			"[--SP] = LC0;"
			"[--SP] = LT0;"
			"[--SP] = LB0;"
			"LSETUP (1f,1f) LC0 = %1;"
			"1: NOP;"
			/* We take advantage of the fact that LC0 is 0 at
			 * the end of the loop.  Otherwise we'd need some
			 * NOPs after the CLI here.
			 */
			"CLI %0;"
			"LB0 = [SP++];"
			"LT0 = [SP++];"
			"LC0 = [SP++];"
			"STI %0;"
			: "=d" (tmp)
			: "a" (loops)
		);
	} else
		__asm__ __volatile__ (
			"LSETUP(1f, 1f) LC0 = %0;"
			"1: NOP;"
			:
			: "a" (loops)
			: "LT0", "LB0", "LC0"
		);
}

#include <linux/param.h>	/* needed for HZ */

/*
 * Use only for very small delays ( < 1 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
static inline void udelay(unsigned long usecs)
{
	extern unsigned long loops_per_jiffy;
	__delay(usecs * loops_per_jiffy / (1000000 / HZ));
}

#endif
