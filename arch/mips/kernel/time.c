/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (c) 2003, 2004  Maciej W. Rozycki
 *
 * Common time service routines for MIPS machines. See
 * Documentation/mips/time.README.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/clockchips.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/profile.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kallsyms.h>

#include <asm/bootinfo.h>
#include <asm/cache.h>
#include <asm/compiler.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/div64.h>
#include <asm/sections.h>
#include <asm/smtc_ipi.h>
#include <asm/time.h>

#include <irq.h>

/*
 * The integer part of the number of usecs per jiffy is taken from tick,
 * but the fractional part is not recorded, so we calculate it using the
 * initial value of HZ.  This aids systems where tick isn't really an
 * integer (e.g. for HZ = 128).
 */
#define USECS_PER_JIFFY		TICK_SIZE
#define USECS_PER_JIFFY_FRAC	((unsigned long)(u32)((1000000ULL << 32) / HZ))

#define TICK_SIZE	(tick_nsec / 1000)

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
 * Null high precision timer functions for systems lacking one.
 */
static cycle_t null_hpt_read(void)
{
	return 0;
}

/*
 * High precision timer functions for a R4k-compatible timer.
 */
static cycle_t c0_hpt_read(void)
{
	return read_c0_count();
}

int (*mips_timer_state)(void);

/*
 * local_timer_interrupt() does profiling and process accounting
 * on a per-CPU basis.
 *
 * In UP mode, it is invoked from the (global) timer_interrupt.
 *
 * In SMP mode, it might invoked by per-CPU timer interrupt, or
 * a broadcasted inter-processor interrupt which itself is triggered
 * by the global timer interrupt.
 */
void local_timer_interrupt(int irq, void *dev_id)
{
	profile_tick(CPU_PROFILING);
	update_process_times(user_mode(get_irq_regs()));
}

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
 * 3) plat_timer_setup() -
 *	a) (optional) over-write any choices made above by time_init().
 *	b) machine specific code should setup the timer irqaction.
 *	c) enable the timer interrupt
 */

unsigned int mips_hpt_frequency;

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

struct clocksource clocksource_mips = {
	.name		= "MIPS",
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init init_mips_clocksource(void)
{
	u64 temp;
	u32 shift;

	if (!mips_hpt_frequency || clocksource_mips.read == null_hpt_read)
		return;

	/* Calclate a somewhat reasonable rating value */
	clocksource_mips.rating = 200 + mips_hpt_frequency / 10000000;
	/* Find a shift value */
	for (shift = 32; shift > 0; shift--) {
		temp = (u64) NSEC_PER_SEC << shift;
		do_div(temp, mips_hpt_frequency);
		if ((temp >> 32) == 0)
			break;
	}
	clocksource_mips.shift = shift;
	clocksource_mips.mult = (u32)temp;

	clocksource_register(&clocksource_mips);
}

void __init __weak plat_time_init(void)
{
}

void __init __weak plat_timer_setup(struct irqaction *irq)
{
}

#ifdef CONFIG_MIPS_MT_SMTC
DEFINE_PER_CPU(struct clock_event_device, smtc_dummy_clockevent_device);

static void smtc_set_mode(enum clock_event_mode mode,
                          struct clock_event_device *evt)
{
}

static void mips_broadcast(cpumask_t mask)
{
	unsigned int cpu;

	for_each_cpu_mask(cpu, mask)
		smtc_send_ipi(cpu, SMTC_CLOCK_TICK, 0);
}

static void setup_smtc_dummy_clockevent_device(void)
{
	//uint64_t mips_freq = mips_hpt_^frequency;
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;

	cd = &per_cpu(smtc_dummy_clockevent_device, cpu);

	cd->name		= "SMTC";
	cd->features		= CLOCK_EVT_FEAT_DUMMY;

	/* Calculate the min / max delta */
	cd->mult	= 0; //div_sc((unsigned long) mips_freq, NSEC_PER_SEC, 32);
	cd->shift		= 0; //32;
	cd->max_delta_ns	= 0; //clockevent_delta2ns(0x7fffffff, cd);
	cd->min_delta_ns	= 0; //clockevent_delta2ns(0x30, cd);

	cd->rating		= 200;
	cd->irq			= 17; //-1;
//	if (cpu)
//		cd->cpumask	= CPU_MASK_ALL; // cpumask_of_cpu(cpu);
//	else
		cd->cpumask	= cpumask_of_cpu(cpu);

	cd->set_mode		= smtc_set_mode;

	cd->broadcast		= mips_broadcast;

	clockevents_register_device(cd);
}
#endif

void __init time_init(void)
{
	plat_time_init();

	/* Choose appropriate high precision timer routines.  */
	if (!cpu_has_counter && !clocksource_mips.read)
		/* No high precision timer -- sorry.  */
		clocksource_mips.read = null_hpt_read;
	else if (!mips_hpt_frequency && !mips_timer_state) {
		/* A high precision timer of unknown frequency.  */
		if (!clocksource_mips.read)
			/* No external high precision timer -- use R4k.  */
			clocksource_mips.read = c0_hpt_read;
	} else {
		/* We know counter frequency.  Or we can get it.  */
		if (!clocksource_mips.read) {
			/* No external high precision timer -- use R4k.  */
			clocksource_mips.read = c0_hpt_read;
		}
		if (!mips_hpt_frequency)
			mips_hpt_frequency = calibrate_hpt();

		/* Report the high precision timer rate for a reference.  */
		printk("Using %u.%03u MHz high precision timer.\n",
		       ((mips_hpt_frequency + 500) / 1000) / 1000,
		       ((mips_hpt_frequency + 500) / 1000) % 1000);
	}

	init_mips_clocksource();
	mips_clockevent_init();
}
