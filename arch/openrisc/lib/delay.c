/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      version 2 as published by the Free Software Foundation
 *
 * Precise Delay Loops
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/delay.h>
#include <asm/timex.h>
#include <asm/processor.h>

int read_current_timer(unsigned long *timer_value)
{
	*timer_value = mfspr(SPR_TTCR);
	return 0;
}

void __delay(unsigned long cycles)
{
	cycles_t start = get_cycles();

	while ((get_cycles() - start) < cycles)
		cpu_relax();
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	unsigned long long loops;

	loops = (unsigned long long)xloops * loops_per_jiffy * HZ;

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
