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

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/preempt.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/timer.h>

#ifdef CONFIG_SMP
# include <asm/smp.h>
#endif

/* simple loop based delay: */
static void delay_loop(unsigned long loops)
{
	int d0;

	__asm__ __volatile__(
		"\tjmp 1f\n"
		".align 16\n"
		"1:\tjmp 2f\n"
		".align 16\n"
		"2:\tdecl %0\n\tjns 2b"
		:"=&a" (d0)
		:"0" (loops));
}

/* TSC based delay: */
static void delay_tsc(unsigned long loops)
{
	unsigned long bclock, now;

	preempt_disable();		/* TSC's are per-cpu */
	rdtscl(bclock);
	do {
		rep_nop();
		rdtscl(now);
	} while ((now-bclock) < loops);
	preempt_enable();
}

/*
 * Since we calibrate only once at boot, this
 * function should be set once at boot and not changed
 */
static void (*delay_fn)(unsigned long) = delay_loop;

void use_tsc_delay(void)
{
	delay_fn = delay_tsc;
}

int __devinit read_current_timer(unsigned long *timer_val)
{
	if (delay_fn == delay_tsc) {
		rdtscl(*timer_val);
		return 0;
	}
	return -1;
}

void __delay(unsigned long loops)
{
	delay_fn(loops);
}

inline void __const_udelay(unsigned long xloops)
{
	int d0;

	xloops *= 4;
	__asm__("mull %0"
		:"=d" (xloops), "=&a" (d0)
		:"1" (xloops), "0"
		(cpu_data(raw_smp_processor_id()).loops_per_jiffy * (HZ/4)));

	__delay(++xloops);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7); /* 2**32 / 1000000 (rounded up) */
}

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005); /* 2**32 / 1000000000 (rounded up) */
}

EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
