/* MN10300 Low level time management
 *
 * Copyright (C) 2007-2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/i386/kernel/time.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/cnt32_to_63.h>
#include <asm/irq.h>
#include <asm/div64.h>
#include <asm/processor.h>
#include <asm/intctl-regs.h>
#include <asm/rtc.h>

#ifdef CONFIG_MN10300_RTC
unsigned long mn10300_ioclk;		/* system I/O clock frequency */
unsigned long mn10300_iobclk;		/* system I/O clock frequency */
unsigned long mn10300_tsc_per_HZ;	/* number of ioclks per jiffy */
#endif /* CONFIG_MN10300_RTC */

static unsigned long mn10300_last_tsc;	/* time-stamp counter at last time
					 * interrupt occurred */

static irqreturn_t timer_interrupt(int irq, void *dev_id);

static struct irqaction timer_irq = {
	.handler	= timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_SHARED | IRQF_TIMER,
	.name		= "timer",
};

static unsigned long sched_clock_multiplier;

/*
 * scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	union {
		unsigned long long ll;
		unsigned l[2];
	} tsc64, result;
	unsigned long tsc, tmp;
	unsigned product[3]; /* 96-bit intermediate value */

	/* read the TSC value
	 */
	tsc = 0 - get_cycles(); /* get_cycles() counts down */

	/* expand to 64-bits.
	 * - sched_clock() must be called once a minute or better or the
	 *   following will go horribly wrong - see cnt32_to_63()
	 */
	tsc64.ll = cnt32_to_63(tsc) & 0x7fffffffffffffffULL;

	/* scale the 64-bit TSC value to a nanosecond value via a 96-bit
	 * intermediate
	 */
	asm("mulu	%2,%0,%3,%0	\n"	/* LSW * mult ->  0:%3:%0 */
	    "mulu	%2,%1,%2,%1	\n"	/* MSW * mult -> %2:%1:0 */
	    "add	%3,%1		\n"
	    "addc	0,%2		\n"	/* result in %2:%1:%0 */
	    : "=r"(product[0]), "=r"(product[1]), "=r"(product[2]), "=r"(tmp)
	    :  "0"(tsc64.l[0]),  "1"(tsc64.l[1]),  "2"(sched_clock_multiplier)
	    : "cc");

	result.l[0] = product[1] << 16 | product[0] >> 16;
	result.l[1] = product[2] << 16 | product[1] >> 16;

	return result.ll;
}

/*
 * initialise the scheduler clock
 */
static void __init mn10300_sched_clock_init(void)
{
	sched_clock_multiplier =
		__muldiv64u(NSEC_PER_SEC, 1 << 16, MN10300_TSCCLK);
}

/*
 * advance the kernel's time keeping clocks (xtime and jiffies)
 * - we use Timer 0 & 1 cascaded as a clock to nudge us the next time
 *   there's a need to update
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	unsigned tsc, elapse;

	write_seqlock(&xtime_lock);

	while (tsc = get_cycles(),
	       elapse = mn10300_last_tsc - tsc, /* time elapsed since last
						 * tick */
	       elapse > MN10300_TSC_PER_HZ
	       ) {
		mn10300_last_tsc -= MN10300_TSC_PER_HZ;

		/* advance the kernel's time tracking system */
		profile_tick(CPU_PROFILING);
		do_timer(1);
		check_rtc_time();
	}

	write_sequnlock(&xtime_lock);

	update_process_times(user_mode(get_irq_regs()));

	return IRQ_HANDLED;
}

/*
 * initialise the various timers used by the main part of the kernel
 */
void __init time_init(void)
{
	/* we need the prescalar running to be able to use IOCLK/8
	 * - IOCLK runs at 1/4 (ST5 open) or 1/8 (ST5 closed) internal CPU clock
	 * - IOCLK runs at Fosc rate (crystal speed)
	 */
	TMPSCNT |= TMPSCNT_ENABLE;

	startup_timestamp_counter();

	printk(KERN_INFO
	       "timestamp counter I/O clock running at %lu.%02lu"
	       " (calibrated against RTC)\n",
	       MN10300_TSCCLK / 1000000, (MN10300_TSCCLK / 10000) % 100);

	xtime.tv_sec = get_initial_rtc_time();
	xtime.tv_nsec = 0;

	mn10300_last_tsc = TMTSCBC;

	/* use timer 0 & 1 cascaded to tick at as close to HZ as possible */
	setup_irq(TMJCIRQ, &timer_irq);

	set_intr_level(TMJCIRQ, TMJCICR_LEVEL);

	startup_jiffies_counter();

#ifdef CONFIG_MN10300_WD_TIMER
	/* start the watchdog timer */
	watchdog_go();
#endif

	mn10300_sched_clock_init();
}
