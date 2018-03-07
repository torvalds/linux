// SPDX-License-Identifier: GPL-2.0
/*
 *	Precise Delay Loops for i386
 *
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	Copyright (C) 2008 Jiri Hladky <hladky _dot_ jiri _at_ gmail _dot_ com>
 *
 *	The __delay function must _NOT_ be inlined as its execution time
 *	depends wildly on alignment on many x86 processors. The additional
 *	jump magic is needed to get the timing stable on all the CPU's
 *	we have to worry about.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/preempt.h>
#include <linux/delay.h>

#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/timer.h>
#include <asm/mwait.h>

#ifdef CONFIG_SMP
# include <asm/smp.h>
#endif

/* simple loop based delay: */
static void delay_loop(unsigned long loops)
{
	asm volatile(
		"	test %0,%0	\n"
		"	jz 3f		\n"
		"	jmp 1f		\n"

		".align 16		\n"
		"1:	jmp 2f		\n"

		".align 16		\n"
		"2:	dec %0		\n"
		"	jnz 2b		\n"
		"3:	dec %0		\n"

		: /* we don't need output */
		:"a" (loops)
	);
}

/* TSC based delay: */
static void delay_tsc(unsigned long __loops)
{
	u64 bclock, now, loops = __loops;
	int cpu;

	preempt_disable();
	cpu = smp_processor_id();
	bclock = rdtsc_ordered();
	for (;;) {
		now = rdtsc_ordered();
		if ((now - bclock) >= loops)
			break;

		/* Allow RT tasks to run */
		preempt_enable();
		rep_nop();
		preempt_disable();

		/*
		 * It is possible that we moved to another CPU, and
		 * since TSC's are per-cpu we need to calculate
		 * that. The delay must guarantee that we wait "at
		 * least" the amount of time. Being moved to another
		 * CPU could make the wait longer but we just need to
		 * make sure we waited long enough. Rebalance the
		 * counter for this CPU.
		 */
		if (unlikely(cpu != smp_processor_id())) {
			loops -= (now - bclock);
			cpu = smp_processor_id();
			bclock = rdtsc_ordered();
		}
	}
	preempt_enable();
}

/*
 * On some AMD platforms, MWAITX has a configurable 32-bit timer, that
 * counts with TSC frequency. The input value is the loop of the
 * counter, it will exit when the timer expires.
 */
static void delay_mwaitx(unsigned long __loops)
{
	u64 start, end, delay, loops = __loops;

	/*
	 * Timer value of 0 causes MWAITX to wait indefinitely, unless there
	 * is a store on the memory monitored by MONITORX.
	 */
	if (loops == 0)
		return;

	start = rdtsc_ordered();

	for (;;) {
		delay = min_t(u64, MWAITX_MAX_LOOPS, loops);

		/*
		 * Use cpu_tss_rw as a cacheline-aligned, seldomly
		 * accessed per-cpu variable as the monitor target.
		 */
		__monitorx(raw_cpu_ptr(&cpu_tss_rw), 0, 0);

		/*
		 * AMD, like Intel, supports the EAX hint and EAX=0xf
		 * means, do not enter any deep C-state and we use it
		 * here in delay() to minimize wakeup latency.
		 */
		__mwaitx(MWAITX_DISABLE_CSTATES, delay, MWAITX_ECX_TIMER_ENABLE);

		end = rdtsc_ordered();

		if (loops <= end - start)
			break;

		loops -= end - start;

		start = end;
	}
}

/*
 * Since we calibrate only once at boot, this
 * function should be set once at boot and not changed
 */
static void (*delay_fn)(unsigned long) = delay_loop;

void use_tsc_delay(void)
{
	if (delay_fn == delay_loop)
		delay_fn = delay_tsc;
}

void use_mwaitx_delay(void)
{
	delay_fn = delay_mwaitx;
}

int read_current_timer(unsigned long *timer_val)
{
	if (delay_fn == delay_tsc) {
		*timer_val = rdtsc();
		return 0;
	}
	return -1;
}

void __delay(unsigned long loops)
{
	delay_fn(loops);
}
EXPORT_SYMBOL(__delay);

inline void __const_udelay(unsigned long xloops)
{
	unsigned long lpj = this_cpu_read(cpu_info.loops_per_jiffy) ? : loops_per_jiffy;
	int d0;

	xloops *= 4;
	asm("mull %%edx"
		:"=d" (xloops), "=&a" (d0)
		:"1" (xloops), "0" (lpj * (HZ / 4)));

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
