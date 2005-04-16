/*
 *	Precise Delay Loops for SuperH
 *
 *	Copyright (C) 1999 Niibe Yutaka & Kaz Kojima
 */

#include <linux/sched.h>
#include <linux/delay.h>

void __delay(unsigned long loops)
{
	__asm__ __volatile__(
		"tst	%0, %0\n\t"
		"1:\t"
		"bf/s	1b\n\t"
		" dt	%0"
		: "=r" (loops)
		: "0" (loops)
		: "t");
}

inline void __const_udelay(unsigned long xloops)
{
	__asm__("dmulu.l	%0, %2\n\t"
		"sts	mach, %0"
		: "=r" (xloops)
		: "0" (xloops), "r" (cpu_data[_smp_processor_id()].loops_per_jiffy)
		: "macl", "mach");
	__delay(xloops * HZ);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00000005);
}

