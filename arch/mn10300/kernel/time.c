/* MN10300 Low level time management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
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
	.mask		= CPU_MASK_NONE,
	.name		= "timer",
};

/*
 * scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	union {
		unsigned long long l;
		u32 w[2];
	} quot;

	quot.w[0] = mn10300_last_tsc - get_cycles();
	quot.w[1] = 1000000000;

	asm("mulu %2,%3,%0,%1"
	    : "=r"(quot.w[1]), "=r"(quot.w[0])
	    : "0"(quot.w[1]), "1"(quot.w[0])
	    : "cc");

	do_div(quot.l, MN10300_TSCCLK);

	return quot.l;
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
		update_process_times(user_mode(get_irq_regs()));
		check_rtc_time();
	}

	write_sequnlock(&xtime_lock);
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
}
