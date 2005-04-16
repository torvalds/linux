/*
 *	Precise Delay Loops for i386
 *
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	The __delay function must _NOT_ be inlined as its execution time
 *	depends wildly on alignment on many x86 processors. The additional
 *	jump magic is needed to get the timing stable on all the CPU's
 *	we have to worry about.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/timer.h>

#ifdef CONFIG_SMP
#include <asm/smp.h>
#endif

extern struct timer_opts* timer;

void __delay(unsigned long loops)
{
	cur_timer->delay(loops);
}

inline void __const_udelay(unsigned long xloops)
{
	int d0;
	xloops *= 4;
	__asm__("mull %0"
		:"=d" (xloops), "=&a" (d0)
		:"1" (xloops),"0" (cpu_data[_smp_processor_id()].loops_per_jiffy * (HZ/4)));
        __delay(++xloops);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7);  /* 2**32 / 1000000 (rounded up) */
}

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005);  /* 2**32 / 1000000000 (rounded up) */
}
