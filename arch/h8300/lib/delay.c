// SPDX-License-Identifier: GPL-2.0
/*
 * delay loops
 *
 * Copyright (C) 2015 Yoshinori Sato
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <asm/param.h>
#include <asm/processor.h>
#include <asm/timex.h>

void __delay(unsigned long cycles)
{
	__asm__ volatile ("1: dec.l #1,%0\n\t"
			  "bne 1b":"=r"(cycles):"0"(cycles));
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
