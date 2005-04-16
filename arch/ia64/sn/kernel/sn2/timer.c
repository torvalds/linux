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

#include <asm/hw_irq.h>
#include <asm/system.h>

#include <asm/sn/leds.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/clksupport.h>

extern unsigned long sn_rtc_cycles_per_second;

static struct time_interpolator sn2_interpolator = {
	.drift = -1,
	.shift = 10,
	.mask = (1LL << 55) - 1,
	.source = TIME_SOURCE_MMIO64
};

void __init sn_timer_init(void)
{
	sn2_interpolator.frequency = sn_rtc_cycles_per_second;
	sn2_interpolator.addr = RTC_COUNTER_ADDR;
	register_time_interpolator(&sn2_interpolator);
}
