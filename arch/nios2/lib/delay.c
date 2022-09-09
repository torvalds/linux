// SPDX-License-Identifier: GPL-2.0-only
/* Copyright Altera Corporation (C) 2014. All rights reserved.
 */

#include <linux/module.h>
#include <asm/delay.h>
#include <asm/param.h>
#include <asm/processor.h>
#include <asm/timex.h>

void __delay(unsigned long cycles)
{
	cycles_t start = get_cycles();

	while ((get_cycles() - start) < cycles)
		cpu_relax();
}
EXPORT_SYMBOL(__delay);

void __const_udelay(unsigned long xloops)
{
	u64 loops;

	loops = (u64)xloops * loops_per_jiffy * HZ;

	__delay(loops >> 32);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x10C7UL); /* 2**32 / 1000000 (rounded up) */
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x5UL); /* 2**32 / 1000000000 (rounded up) */
}
EXPORT_SYMBOL(__ndelay);
