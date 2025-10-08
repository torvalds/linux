// SPDX-License-Identifier: GPL-2.0
/*
 * ip30-timer.c: Clocksource/clockevent support for the
 *               HEART chip in SGI Octane (IP30) systems.
 *
 * Copyright (C) 2004-2007 Stanislaw Skowronek <skylark@unaligned.org>
 * Copyright (C) 2009 Johannes Dickgreber <tanzy@gmx.de>
 * Copyright (C) 2011 Joshua Kinard <linux@kumba.dev>
 */

#include <linux/clocksource.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/sched_clock.h>

#include <asm/time.h>
#include <asm/cevt-r4k.h>
#include <asm/sgi/heart.h>

static u64 ip30_heart_counter_read(struct clocksource *cs)
{
	return heart_read(&heart_regs->count);
}

struct clocksource ip30_heart_clocksource = {
	.name	= "HEART",
	.rating	= 400,
	.read	= ip30_heart_counter_read,
	.mask	= CLOCKSOURCE_MASK(52),
	.flags	= (CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_VALID_FOR_HRES),
};

static u64 notrace ip30_heart_read_sched_clock(void)
{
	return heart_read(&heart_regs->count);
}

static void __init ip30_heart_clocksource_init(void)
{
	struct clocksource *cs = &ip30_heart_clocksource;

	clocksource_register_hz(cs, HEART_CYCLES_PER_SEC);

	sched_clock_register(ip30_heart_read_sched_clock, 52,
			     HEART_CYCLES_PER_SEC);
}

void __init plat_time_init(void)
{
	int irq = get_c0_compare_int();

	cp0_timer_irq_installed = 1;
	c0_compare_irqaction.percpu_dev_id = &mips_clockevent_device;
	c0_compare_irqaction.flags &= ~IRQF_SHARED;
	irq_set_handler(irq, handle_percpu_devid_irq);
	irq_set_percpu_devid(irq);
	setup_percpu_irq(irq, &c0_compare_irqaction);
	enable_percpu_irq(irq, IRQ_TYPE_NONE);

	ip30_heart_clocksource_init();
}
