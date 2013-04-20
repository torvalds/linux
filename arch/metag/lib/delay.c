/*
 *	Precise Delay Loops for Meta
 *
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	Copyright (C) 2007,2009 Imagination Technologies Ltd.
 *
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <asm/core_reg.h>
#include <asm/processor.h>

/*
 * TXTACTCYC is only 24 bits, so on chips with fast clocks it will wrap
 * many times per-second. If it does wrap __delay will return prematurely,
 * but this is only likely with large delay values.
 *
 * We also can't implement read_current_timer() with TXTACTCYC due to
 * this wrapping behaviour.
 */
#define rdtimer(t) t = __core_reg_get(TXTACTCYC)

void __delay(unsigned long loops)
{
	unsigned long bclock, now;

	rdtimer(bclock);
	do {
		asm("NOP");
		rdtimer(now);
	} while ((now-bclock) < loops);
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	u64 loops = (u64)xloops * (u64)loops_per_jiffy * HZ;
	__delay(loops >> 32);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7); /* 2**32 / 1000000 (rounded up) */
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005); /* 2**32 / 1000000000 (rounded up) */
}
EXPORT_SYMBOL(__ndelay);
