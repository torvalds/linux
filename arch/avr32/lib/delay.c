/*
 *      Precise Delay Loops for avr32
 *
 *      Copyright (C) 1993 Linus Torvalds
 *      Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/timex.h>
#include <linux/param.h>
#include <linux/types.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/sysreg.h>

int __devinit read_current_timer(unsigned long *timer_value)
{
	*timer_value = sysreg_read(COUNT);
	return 0;
}

void __delay(unsigned long loops)
{
	unsigned bclock, now;

	bclock = sysreg_read(COUNT);
	do {
		now = sysreg_read(COUNT);
	} while ((now - bclock) < loops);
}

inline void __const_udelay(unsigned long xloops)
{
	unsigned long long loops;

	asm("mulu.d %0, %1, %2"
	    : "=r"(loops)
	    : "r"(current_cpu_data.loops_per_jiffy * HZ), "r"(xloops));
	__delay(loops >> 32);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7); /* 2**32 / 1000000 (rounded up) */
}

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005); /* 2**32 / 1000000000 (rounded up) */
}
