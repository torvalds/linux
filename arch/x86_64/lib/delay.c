/*
 *	Precise Delay Loops for x86-64
 *
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 *	The __delay function must _NOT_ be inlined as its execution time
 *	depends wildly on alignment on many x86 processors. 
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/delay.h>
#include <asm/msr.h>

#ifdef CONFIG_SMP
#include <asm/smp.h>
#endif

int read_current_timer(unsigned long *timer_value)
{
	rdtscll(*timer_value);
	return 0;
}

void __delay(unsigned long loops)
{
	unsigned bclock, now;
	
	rdtscl(bclock);
	do
	{
		rep_nop(); 
		rdtscl(now);
	}
	while((now-bclock) < loops);
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	__delay((xloops * HZ * cpu_data[raw_smp_processor_id()].loops_per_jiffy) >> 32);
}
EXPORT_SYMBOL(__const_udelay);

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c6);  /* 2**32 / 1000000 */
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005);  /* 2**32 / 1000000000 (rounded up) */
}
EXPORT_SYMBOL(__ndelay);
