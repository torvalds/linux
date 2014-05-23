/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (c) 2003, 2004  Maciej W. Rozycki
 *
 * Common time service routines for MIPS machines.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
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
#include <linux/export.h>

#include <asm/cpu-features.h>
#include <asm/cpu-type.h>
#include <asm/div64.h>
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

int __weak rtc_mips_set_mmss(unsigned long nowtime)
{
	return rtc_mips_set_time(nowtime);
}

int update_persistent_clock(struct timespec now)
{
	return rtc_mips_set_mmss(now.tv_sec);
}

static int null_perf_irq(void)
{
	return 0;
}

int (*perf_irq)(void) = null_perf_irq;

EXPORT_SYMBOL(perf_irq);

/*
 * time_init() - it does the following things.
 *
 * 1) plat_time_init() -
 *	a) (optional) set up RTC routines,
 *	b) (optional) calibrate and set the mips_hpt_frequency
 *	    (only needed if you intended to use cpu counter as timer interrupt
 *	     source)
 * 2) calculate a couple of cached variables for later usage
 */

unsigned int mips_hpt_frequency;

/*
 * This function exists in order to cause an error due to a duplicate
 * definition if platform code should have its own implementation.  The hook
 * to use instead is plat_time_init.  plat_time_init does not receive the
 * irqaction pointer argument anymore.	This is because any function which
 * initializes an interrupt timer now takes care of its own request_irq rsp.
 * setup_irq calls and each clock_event_device should use its own
 * struct irqrequest.
 */
void __init plat_timer_setup(void)
{
	BUG();
}

static __init int cpu_has_mfc0_count_bug(void)
{
	switch (current_cpu_type()) {
	case CPU_R4000PC:
	case CPU_R4000SC:
	case CPU_R4000MC:
		/*
		 * V3.0 is documented as suffering from the mfc0 from count bug.
		 * Afaik this is the last version of the R4000.	 Later versions
		 * were marketed as R4400.
		 */
		return 1;

	case CPU_R4400PC:
	case CPU_R4400SC:
	case CPU_R4400MC:
		/*
		 * The published errata for the R4400 up to 3.0 say the CPU
		 * has the mfc0 from count bug.
		 */
		if ((current_cpu_data.processor_id & 0xff) <= 0x30)
			return 1;

		/*
		 * we assume newer revisions are ok
		 */
		return 0;
	}

	return 0;
}

void __init time_init(void)
{
	plat_time_init();

	/*
	 * The use of the R4k timer as a clock event takes precedence;
	 * if reading the Count register might interfere with the timer
	 * interrupt, then we don't use the timer as a clock source.
	 * We may still use the timer as a clock source though if the
	 * timer interrupt isn't reliable; the interference doesn't
	 * matter then, because we don't use the interrupt.
	 */
	if (mips_clockevent_init() != 0 || !cpu_has_mfc0_count_bug())
		init_mips_clocksource();
}
