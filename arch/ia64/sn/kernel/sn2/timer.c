/*
 * linux/arch/ia64/sn/kernel/sn2/timer.c
 *
 * Copyright (C) 2003 Silicon Graphics, Inc.
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger <davidm@hpl.hp.com>: updated for new timer-interpolation infrastructure
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>

#include <asm/hw_irq.h>
#include <asm/system.h>
#include <asm/timex.h>

#include <asm/sn/leds.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/clksupport.h>

extern unsigned long sn_rtc_cycles_per_second;

static cycle_t read_sn2(struct clocksource *cs)
{
	return (cycle_t)readq(RTC_COUNTER_ADDR);
}

static struct clocksource clocksource_sn2 = {
        .name           = "sn2_rtc",
        .rating         = 450,
        .read           = read_sn2,
        .mask           = (1LL << 55) - 1,
        .flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

/*
 * sn udelay uses the RTC instead of the ITC because the ITC is not
 * synchronized across all CPUs, and the thread may migrate to another CPU
 * if preemption is enabled.
 */
static void
ia64_sn_udelay (unsigned long usecs)
{
	unsigned long start = rtc_time();
	unsigned long end = start +
			usecs * sn_rtc_cycles_per_second / 1000000;

	while (time_before((unsigned long)rtc_time(), end))
		cpu_relax();
}

void __init sn_timer_init(void)
{
	clocksource_sn2.archdata.fsys_mmio = RTC_COUNTER_ADDR;
	clocksource_register_hz(&clocksource_sn2, sn_rtc_cycles_per_second);

	ia64_udelay = &ia64_sn_udelay;
}
