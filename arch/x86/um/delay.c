/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 * Mostly copied from arch/x86/lib/delay.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/param.h>

void __delay(unsigned long loops)
{
	asm volatile(
		"test %0,%0\n"
		"jz 3f\n"
		"jmp 1f\n"

		".align 16\n"
		"1: jmp 2f\n"

		".align 16\n"
		"2: dec %0\n"
		" jnz 2b\n"
		"3: dec %0\n"

		: /* we don't need output */
		: "a" (loops)
	);
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	int d0;

	xloops *= 4;
	asm("mull %%edx"
		: "=d" (xloops), "=&a" (d0)
		: "1" (xloops), "0"
		(loops_per_jiffy * (HZ/4)));

	__delay(++xloops);
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
