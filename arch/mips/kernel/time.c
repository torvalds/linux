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

#include <asm/bootinfo.h>
#include <asm/cache.h>
#include <asm/compiler.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/div64.h>
#include <asm/sections.h>
#include <asm/time.h>

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

/* how many counter cycles in a jiffy */
static unsigned long cycles_per_jiffy __read_mostly;

/* expirelo is the count value for next CPU timer interrupt */
static unsigned int expirelo;


/*
 * Null timer ack for systems not needing one (e.g. i8254).
 */
static void null_timer_ack(void) { /* nothing */ }

/*
 * Null high precision timer functions for systems lacking one.
 */
static cycle_t null_hpt_read(void)
{
	return 0;
}

/*
 * Timer ack for an R4k-compatible timer of a known frequency.
 */
static void c0_timer_ack(void)
{
	unsigned int count;

	/* Ack this timer interrupt and set the next one.  */
	expirelo += cycles_per_jiffy;
	write_c0_compare(expirelo);

	/* Check to see if we have missed any timer interrupts.  */
	while (((count = read_c0_count()) - expirelo) < 0x7fffffff) {
		/* missed_timer_count++; */
		expirelo = count + cycles_per_jiffy;
		write_c0_compare(expirelo);
	}
}

/*
 * High precision timer functions for a R4k-compatible timer.
 */
static cycle_t c0_hpt_read(void)
{
	return read_c0_count();
}

/* For use both as a high precision timer and an interrupt source.  */
static void __init c0_hpt_timer_init(void)
{
	expirelo = read_c0_count() + cycles_per_jiffy;
	write_c0_compare(expirelo);
}

int (*mips_timer_state)(void);
void (*mips_timer_ack)(void);

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

/*
 * High-level timer interrupt service routines.  This function
 * is set as irqaction->handler and is invoked through do_IRQ.
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);

	mips_timer_ack();

	/*
	 * call the generic timer interrupt handling
	 */
	do_timer(1);

	write_sequnlock(&xtime_lock);

	/*
	 * In UP mode, we call local_timer_interrupt() to do profiling
	 * and process accouting.
	 *
	 * In SMP mode, local_timer_interrupt() is invoked by appropriate
	 * low-level local timer interrupt handler.
	 */
	local_timer_interrupt(irq, dev_id);

	return IRQ_HANDLED;
}

int null_perf_irq(void)
{
	return 0;
}

EXPORT_SYMBOL(null_perf_irq);

int (*perf_irq)(void) = null_perf_irq;

EXPORT_SYMBOL(perf_irq);

/*
 * Timer interrupt
 */
int cp0_compare_irq;

/*
 * Performance counter IRQ or -1 if shared with timer
 */
int cp0_perfcount_irq;
EXPORT_SYMBOL_GPL(cp0_perfcount_irq);

/*
 * Possibly handle a performance counter interrupt.
 * Return true if the timer interrupt should not be checked
 */
static inline int handle_perf_irq (int r2)
{
	/*
	 * The performance counter overflow interrupt may be shared with the
	 * timer interrupt (cp0_perfcount_irq < 0). If it is and a
	 * performance counter has overflowed (perf_irq() == IRQ_HANDLED)
	 * and we can't reliably determine if a counter interrupt has also
	 * happened (!r2) then don't check for a timer interrupt.
	 */
	return (cp0_perfcount_irq < 0) &&
		perf_irq() == IRQ_HANDLED &&
		!r2;
}

void ll_timer_interrupt(int irq, void *dev_id)
{
	int cpu = smp_processor_id();

#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 *  In an SMTC system, one Count/Compare set exists per VPE.
	 *  Which TC within a VPE gets the interrupt is essentially
	 *  random - we only know that it shouldn't be one with
	 *  IXMT set. Whichever TC gets the interrupt needs to
	 *  send special interprocessor interrupts to the other
	 *  TCs to make sure that they schedule, etc.
	 *
	 *  That code is specific to the SMTC kernel, not to
	 *  the a particular platform, so it's invoked from
	 *  the general MIPS timer_interrupt routine.
	 */

	/*
	 * We could be here due to timer interrupt,
	 * perf counter overflow, or both.
	 */
	(void) handle_perf_irq(1);

	if (read_c0_cause() & (1 << 30)) {
		/*
		 * There are things we only want to do once per tick
		 * in an "MP" system.   One TC of each VPE will take
		 * the actual timer interrupt.  The others will get
		 * timer broadcast IPIs. We use whoever it is that takes
		 * the tick on VPE 0 to run the full timer_interrupt().
		 */
		if (cpu_data[cpu].vpe_id == 0) {
			timer_interrupt(irq, NULL);
		} else {
			write_c0_compare(read_c0_count() +
			                 (mips_hpt_frequency/HZ));
			local_timer_interrupt(irq, dev_id);
		}
		smtc_timer_broadcast(cpu_data[cpu].vpe_id);
	}
#else /* CONFIG_MIPS_MT_SMTC */
	int r2 = cpu_has_mips_r2;

	if (handle_perf_irq(r2))
		return;

	if (r2 && ((read_c0_cause() & (1 << 30)) == 0))
		return;

	if (cpu == 0) {
		/*
		 * CPU 0 handles the global timer interrupt job and process
		 * accounting resets count/compare registers to trigger next
		 * timer int.
		 */
		timer_interrupt(irq, NULL);
	} else {
		/* Everyone else needs to reset the timer int here as
		   ll_local_timer_interrupt doesn't */
		/*
		 * FIXME: need to cope with counter underflow.
		 * More support needs to be added to kernel/time for
		 * counter/timer interrupts on multiple CPU's
		 */
		write_c0_compare(read_c0_count() + (mips_hpt_frequency/HZ));

		/*
		 * Other CPUs should do profiling and process accounting
		 */
		local_timer_interrupt(irq, dev_id);
	}
#endif /* CONFIG_MIPS_MT_SMTC */
}

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

static struct irqaction timer_irqaction = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED | IRQF_PERCPU,
	.name = "timer",
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

			if (!mips_timer_state) {
				/* No external timer interrupt -- use R4k.  */
				mips_timer_ack = c0_timer_ack;
				/* Calculate cache parameters.  */
				cycles_per_jiffy =
					(mips_hpt_frequency + HZ / 2) / HZ;
				/*
				 * This sets up the high precision
				 * timer for the first interrupt.
				 */
				c0_hpt_timer_init();
			}
		}
		if (!mips_hpt_frequency)
			mips_hpt_frequency = calibrate_hpt();

		/* Report the high precision timer rate for a reference.  */
		printk("Using %u.%03u MHz high precision timer.\n",
		       ((mips_hpt_frequency + 500) / 1000) / 1000,
		       ((mips_hpt_frequency + 500) / 1000) % 1000);
	}

	if (!mips_timer_ack)
		/* No timer interrupt ack (e.g. i8254).  */
		mips_timer_ack = null_timer_ack;

	/*
	 * Call board specific timer interrupt setup.
	 *
	 * this pointer must be setup in machine setup routine.
	 *
	 * Even if a machine chooses to use a low-level timer interrupt,
	 * it still needs to setup the timer_irqaction.
	 * In that case, it might be better to set timer_irqaction.handler
	 * to be NULL function so that we are sure the high-level code
	 * is not invoked accidentally.
	 */
	plat_timer_setup(&timer_irqaction);

	init_mips_clocksource();
}
