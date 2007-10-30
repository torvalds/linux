/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (c) 2003, 2004  Maciej W. Rozycki
 *
 * Common time service routines for MIPS machines.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/bug.h>
#include <linux/clockchips.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/cpu-features.h>
#include <asm/div64.h>
#include <asm/smtc_ipi.h>
#include <asm/time.h>

/*
 * forward reference
 */
DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

int __weak rtc_mips_set_time(unsigned long sec)
{
	return 0;
}
EXPORT_SYMBOL(rtc_mips_set_time);

int __weak rtc_mips_set_mmss(unsigned long nowtime)
{
	return rtc_mips_set_time(nowtime);
}

int update_persistent_clock(struct timespec now)
{
	return rtc_mips_set_mmss(now.tv_sec);
}

/*
 * High precision timer functions for a R4k-compatible timer.
 */
static cycle_t c0_hpt_read(void)
{
	return read_c0_count();
}

int (*mips_timer_state)(void);

int null_perf_irq(void)
{
	return 0;
}

EXPORT_SYMBOL(null_perf_irq);

int (*perf_irq)(void) = null_perf_irq;

EXPORT_SYMBOL(perf_irq);

/*
 * time_init() - it does the following things.
 *
 * 1) plat_time_init() -
 * 	a) (optional) set up RTC routines,
 *      b) (optional) calibrate and set the mips_hpt_frequency
 *	    (only needed if you intended to use cpu counter as timer interrupt
 *	     source)
 * 2) calculate a couple of cached variables for later usage
 */

unsigned int mips_hpt_frequency;

static struct clocksource clocksource_mips = {
	.name		= "MIPS",
	.read		= c0_hpt_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static unsigned int __init calibrate_hpt(void)
{
	cycle_t frequency, hpt_start, hpt_end, hpt_count, hz;

	const int loops = HZ / 10;
	int log_2_loops = 0;
	int i;

	/*
	 * We want to calibrate for 0.1s, but to avoid a 64-bit
	 * division we round the number of loops up to the nearest
	 * power of 2.
	 */
	while (loops > 1 << log_2_loops)
		log_2_loops++;
	i = 1 << log_2_loops;

	/*
	 * Wait for a rising edge of the timer interrupt.
	 */
	while (mips_timer_state());
	while (!mips_timer_state());

	/*
	 * Now see how many high precision timer ticks happen
	 * during the calculated number of periods between timer
	 * interrupts.
	 */
	hpt_start = clocksource_mips.read();
	do {
		while (mips_timer_state());
		while (!mips_timer_state());
	} while (--i);
	hpt_end = clocksource_mips.read();

	hpt_count = (hpt_end - hpt_start) & clocksource_mips.mask;
	hz = HZ;
	frequency = hpt_count * hz;

	return frequency >> log_2_loops;
}

void __init clocksource_set_clock(struct clocksource *cs, unsigned int clock)
{
	u64 temp;
	u32 shift;

	/* Find a shift value */
	for (shift = 32; shift > 0; shift--) {
		temp = (u64) NSEC_PER_SEC << shift;
		do_div(temp, clock);
		if ((temp >> 32) == 0)
			break;
	}
	cs->shift = shift;
	cs->mult = (u32) temp;
}

void __cpuinit clockevent_set_clock(struct clock_event_device *cd,
	unsigned int clock)
{
	u64 temp;
	u32 shift;

	/* Find a shift value */
	for (shift = 32; shift > 0; shift--) {
		temp = (u64) clock << shift;
		do_div(temp, NSEC_PER_SEC);
		if ((temp >> 32) == 0)
			break;
	}
	cd->shift = shift;
	cd->mult = (u32) temp;
}

static void __init init_mips_clocksource(void)
{
	/* Calclate a somewhat reasonable rating value */
	clocksource_mips.rating = 200 + mips_hpt_frequency / 10000000;

	clocksource_set_clock(&clocksource_mips, mips_hpt_frequency);

	clocksource_register(&clocksource_mips);
}

void __init __weak plat_time_init(void)
{
}

/*
 * This function exists in order to cause an error due to a duplicate
 * definition if platform code should have its own implementation.  The hook
 * to use instead is plat_time_init.  plat_time_init does not receive the
 * irqaction pointer argument anymore.  This is because any function which
 * initializes an interrupt timer now takes care of its own request_irq rsp.
 * setup_irq calls and each clock_event_device should use its own
 * struct irqrequest.
 */
void __init plat_timer_setup(void)
{
	BUG();
}

void __init time_init(void)
{
	plat_time_init();

	if (cpu_has_counter && (mips_hpt_frequency || mips_timer_state)) {
		/* We know counter frequency.  Or we can get it.  */
		if (!mips_hpt_frequency)
			mips_hpt_frequency = calibrate_hpt();

		/* Report the high precision timer rate for a reference.  */
		printk("Using %u.%03u MHz high precision timer.\n",
		       ((mips_hpt_frequency + 500) / 1000) / 1000,
		       ((mips_hpt_frequency + 500) / 1000) % 1000);
		init_mips_clocksource();
	}

	mips_clockevent_init();
}
