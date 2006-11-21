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
#include <linux/clocksource.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/param.h>
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

/*
 * By default we provide the null RTC ops
 */
static unsigned long null_rtc_get_time(void)
{
	return mktime(2000, 1, 1, 0, 0, 0);
}

static int null_rtc_set_time(unsigned long sec)
{
	return 0;
}

unsigned long (*rtc_mips_get_time)(void) = null_rtc_get_time;
int (*rtc_mips_set_time)(unsigned long) = null_rtc_set_time;
int (*rtc_mips_set_mmss)(unsigned long);


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
static unsigned int null_hpt_read(void)
{
	return 0;
}

static void __init null_hpt_init(void)
{
	/* nothing */
}


/*
 * Timer ack for an R4k-compatible timer of a known frequency.
 */
static void c0_timer_ack(void)
{
	unsigned int count;

#ifndef CONFIG_SOC_PNX8550	/* pnx8550 resets to zero */
	/* Ack this timer interrupt and set the next one.  */
	expirelo += cycles_per_jiffy;
#endif
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
static unsigned int c0_hpt_read(void)
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
unsigned int (*mips_hpt_read)(void);
void (*mips_hpt_init)(void) __initdata = null_hpt_init;
unsigned int mips_hpt_mask = 0xffffffff;

/* last time when xtime and rtc are sync'ed up */
static long last_rtc_update;

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
irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);

	mips_timer_ack();

	/*
	 * call the generic timer interrupt handling
	 */
	do_timer(1);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. rtc_mips_set_time() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (rtc_mips_set_mmss(xtime.tv_sec) == 0) {
			last_rtc_update = xtime.tv_sec;
		} else {
			/* do it again in 60 s */
			last_rtc_update = xtime.tv_sec - 600;
		}
	}

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

int (*perf_irq)(void) = null_perf_irq;

EXPORT_SYMBOL(null_perf_irq);
EXPORT_SYMBOL(perf_irq);

asmlinkage void ll_timer_interrupt(int irq)
{
	int r2 = cpu_has_mips_r2;

	irq_enter();
	kstat_this_cpu.irqs[irq]++;

	/*
	 * Suckage alert:
	 * Before R2 of the architecture there was no way to see if a
	 * performance counter interrupt was pending, so we have to run the
	 * performance counter interrupt handler anyway.
	 */
	if (!r2 || (read_c0_cause() & (1 << 26)))
		if (perf_irq())
			goto out;

	/* we keep interrupt disabled all the time */
	if (!r2 || (read_c0_cause() & (1 << 30)))
		timer_interrupt(irq, NULL);

out:
	irq_exit();
}

asmlinkage void ll_local_timer_interrupt(int irq)
{
	irq_enter();
	if (smp_processor_id() != 0)
		kstat_this_cpu.irqs[irq]++;

	/* we keep interrupt disabled all the time */
	local_timer_interrupt(irq, NULL);

	irq_exit();
}

/*
 * time_init() - it does the following things.
 *
 * 1) board_time_init() -
 * 	a) (optional) set up RTC routines,
 *      b) (optional) calibrate and set the mips_hpt_frequency
 *	    (only needed if you intended to use cpu counter as timer interrupt
 *	     source)
 * 2) setup xtime based on rtc_mips_get_time().
 * 3) calculate a couple of cached variables for later usage
 * 4) plat_timer_setup() -
 *	a) (optional) over-write any choices made above by time_init().
 *	b) machine specific code should setup the timer irqaction.
 *	c) enable the timer interrupt
 */

void (*board_time_init)(void);

unsigned int mips_hpt_frequency;

static struct irqaction timer_irqaction = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED,
	.name = "timer",
};

static unsigned int __init calibrate_hpt(void)
{
	u64 frequency;
	u32 hpt_start, hpt_end, hpt_count, hz;

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
	hpt_start = mips_hpt_read();
	do {
		while (mips_timer_state());
		while (!mips_timer_state());
	} while (--i);
	hpt_end = mips_hpt_read();

	hpt_count = (hpt_end - hpt_start) & mips_hpt_mask;
	hz = HZ;
	frequency = (u64)hpt_count * (u64)hz;

	return frequency >> log_2_loops;
}

static cycle_t read_mips_hpt(void)
{
	return (cycle_t)mips_hpt_read();
}

static struct clocksource clocksource_mips = {
	.name		= "MIPS",
	.read		= read_mips_hpt,
	.is_continuous	= 1,
};

static void __init init_mips_clocksource(void)
{
	u64 temp;
	u32 shift;

	if (!mips_hpt_frequency || mips_hpt_read == null_hpt_read)
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
	clocksource_mips.mask = mips_hpt_mask;

	clocksource_register(&clocksource_mips);
}

void __init time_init(void)
{
	if (board_time_init)
		board_time_init();

	if (!rtc_mips_set_mmss)
		rtc_mips_set_mmss = rtc_mips_set_time;

	xtime.tv_sec = rtc_mips_get_time();
	xtime.tv_nsec = 0;

	set_normalized_timespec(&wall_to_monotonic,
	                        -xtime.tv_sec, -xtime.tv_nsec);

	/* Choose appropriate high precision timer routines.  */
	if (!cpu_has_counter && !mips_hpt_read)
		/* No high precision timer -- sorry.  */
		mips_hpt_read = null_hpt_read;
	else if (!mips_hpt_frequency && !mips_timer_state) {
		/* A high precision timer of unknown frequency.  */
		if (!mips_hpt_read)
			/* No external high precision timer -- use R4k.  */
			mips_hpt_read = c0_hpt_read;
	} else {
		/* We know counter frequency.  Or we can get it.  */
		if (!mips_hpt_read) {
			/* No external high precision timer -- use R4k.  */
			mips_hpt_read = c0_hpt_read;

			if (!mips_timer_state) {
				/* No external timer interrupt -- use R4k.  */
				mips_hpt_init = c0_hpt_timer_init;
				mips_timer_ack = c0_timer_ack;
			}
		}
		if (!mips_hpt_frequency)
			mips_hpt_frequency = calibrate_hpt();

		/* Calculate cache parameters.  */
		cycles_per_jiffy = (mips_hpt_frequency + HZ / 2) / HZ;

		/* Report the high precision timer rate for a reference.  */
		printk("Using %u.%03u MHz high precision timer.\n",
		       ((mips_hpt_frequency + 500) / 1000) / 1000,
		       ((mips_hpt_frequency + 500) / 1000) % 1000);
	}

	if (!mips_timer_ack)
		/* No timer interrupt ack (e.g. i8254).  */
		mips_timer_ack = null_timer_ack;

	/* This sets up the high precision timer for the first interrupt.  */
	mips_hpt_init();

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

#define FEBRUARY		2
#define STARTOFTIME		1970
#define SECDAY			86400L
#define SECYR			(SECDAY * 365)
#define leapyear(y)		((!((y) % 4) && ((y) % 100)) || !((y) % 400))
#define days_in_year(y)		(leapyear(y) ? 366 : 365)
#define days_in_month(m)	(month_days[(m) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

void to_tm(unsigned long tim, struct rtc_time *tm)
{
	long hms, day, gday;
	int i;

	gday = day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i - 1;		/* tm_mon starts from 0 to 11 */

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	tm->tm_wday = (gday + 4) % 7;	/* 1970/1/1 was Thursday */
}

EXPORT_SYMBOL(rtc_lock);
EXPORT_SYMBOL(to_tm);
EXPORT_SYMBOL(rtc_mips_set_time);
EXPORT_SYMBOL(rtc_mips_get_time);

unsigned long long sched_clock(void)
{
	return (unsigned long long)jiffies*(1000000000/HZ);
}
