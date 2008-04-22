/*
 * arch/sh/lib64/udelay.c
 *
 * Delay routines, using a pre-computed "loops_per_jiffy" value.
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sched.h>
#include <asm/param.h>

/*
 * Use only for very small delays (< 1 msec).
 *
 * The active part of our cycle counter is only 32-bits wide, and
 * we're treating the difference between two marks as signed.  On
 * a 1GHz box, that's about 2 seconds.
 */

void __delay(unsigned long loops)
{
	long long dummy;
	__asm__ __volatile__("gettr	tr0, %1\n\t"
			     "pta	$+4, tr0\n\t"
			     "addi	%0, -1, %0\n\t"
			     "bne	%0, r63, tr0\n\t"
			     "ptabs	%1, tr0\n\t":"=r"(loops),
			     "=r"(dummy)
			     :"0"(loops));
}

inline void __const_udelay(unsigned long xloops)
{
	__delay(xloops * (HZ * cpu_data[raw_smp_processor_id()].loops_per_jiffy));
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00000005);
}
