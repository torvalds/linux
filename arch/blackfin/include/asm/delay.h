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
 * close approximation borrowed from m68knommu to avoid 64-bit math
 */

#define	HZSCALE		(268435456 / (1000000/HZ))

static inline void udelay(unsigned long usecs)
{
	extern unsigned long loops_per_jiffy;
	__delay((((usecs * HZSCALE) >> 11) * (loops_per_jiffy >> 11)) >> 6);
}

#endif
