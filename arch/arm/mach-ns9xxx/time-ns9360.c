/*
 * arch/arm/mach-ns9xxx/time-ns9360.c
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/stringify.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include <mach/processor-ns9360.h>
#include <mach/regs-sys-ns9360.h>
#include <mach/irqs.h>
#include <mach/system.h>
#include "generic.h"

#define TIMER_CLOCKSOURCE 0
#define TIMER_CLOCKEVENT 1
static u32 latch;

static cycle_t ns9360_clocksource_read(struct clocksource *cs)
{
	return __raw_readl(SYS_TR(TIMER_CLOCKSOURCE));
}

static struct clocksource ns9360_clocksource = {
	.name	= "ns9360-timer" __stringify(TIMER_CLOCKSOURCE),
	.rating	= 300,
	.read	= ns9360_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void ns9360_clockevent_setmode(enum clock_event_mode mode,
		struct clock_event_device *clk)
{
	u32 tc = __raw_readl(SYS_TC(TIMER_CLOCKEVENT));

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writel(latch, SYS_TRC(TIMER_CLOCKEVENT));
		REGSET(tc, SYS_TCx, REN, EN);
		REGSET(tc, SYS_TCx, INTS, EN);
		REGSET(tc, SYS_TCx, TEN, EN);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		REGSET(tc, SYS_TCx, REN, DIS);
		REGSET(tc, SYS_TCx, INTS, EN);

		/* fall through */

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
	default:
		REGSET(tc, SYS_TCx, TEN, DIS);
		break;
	}

	__raw_writel(tc, SYS_TC(TIMER_CLOCKEVENT));
}

static int ns9360_clockevent_setnextevent(unsigned long evt,
		struct clock_event_device *clk)
{
	u32 tc = __raw_readl(SYS_TC(TIMER_CLOCKEVENT));

	if (REGGET(tc, SYS_TCx, TEN)) {
		REGSET(tc, SYS_TCx, TEN, DIS);
		__raw_writel(tc, SYS_TC(TIMER_CLOCKEVENT));
	}

	REGSET(tc, SYS_TCx, TEN, EN);

	__raw_writel(evt, SYS_TRC(TIMER_CLOCKEVENT));

	__raw_writel(tc, SYS_TC(TIMER_CLOCKEVENT));

	return 0;
}

static struct clock_event_device ns9360_clockevent_device = {
	.name		= "ns9360-timer" __stringify(TIMER_CLOCKEVENT),
	.shift		= 20,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= ns9360_clockevent_setmode,
	.set_next_event	= ns9360_clockevent_setnextevent,
};

static irqreturn_t ns9360_clockevent_handler(int irq, void *dev_id)
{
	int timerno = irq - IRQ_NS9360_TIMER0;
	u32 tc;

	struct clock_event_device *evt = &ns9360_clockevent_device;

	/* clear irq */
	tc = __raw_readl(SYS_TC(timerno));
	if (REGGET(tc, SYS_TCx, REN) == SYS_TCx_REN_DIS) {
		REGSET(tc, SYS_TCx, TEN, DIS);
		__raw_writel(tc, SYS_TC(timerno));
	}
	REGSET(tc, SYS_TCx, INTC, SET);
	__raw_writel(tc, SYS_TC(timerno));
	REGSET(tc, SYS_TCx, INTC, UNSET);
	__raw_writel(tc, SYS_TC(timerno));

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction ns9360_clockevent_action = {
	.name		= "ns9360-timer" __stringify(TIMER_CLOCKEVENT),
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= ns9360_clockevent_handler,
};

static void __init ns9360_timer_init(void)
{
	int tc;

	tc = __raw_readl(SYS_TC(TIMER_CLOCKSOURCE));
	if (REGGET(tc, SYS_TCx, TEN)) {
		REGSET(tc, SYS_TCx, TEN, DIS);
		__raw_writel(tc, SYS_TC(TIMER_CLOCKSOURCE));
	}

	__raw_writel(0, SYS_TRC(TIMER_CLOCKSOURCE));

	REGSET(tc, SYS_TCx, TEN, EN);
	REGSET(tc, SYS_TCx, TDBG, STOP);
	REGSET(tc, SYS_TCx, TLCS, CPU);
	REGSET(tc, SYS_TCx, TM, IEE);
	REGSET(tc, SYS_TCx, INTS, DIS);
	REGSET(tc, SYS_TCx, UDS, UP);
	REGSET(tc, SYS_TCx, TSZ, 32);
	REGSET(tc, SYS_TCx, REN, EN);

	__raw_writel(tc, SYS_TC(TIMER_CLOCKSOURCE));

	clocksource_register_hz(&ns9360_clocksource, ns9360_cpuclock());

	latch = SH_DIV(ns9360_cpuclock(), HZ, 0);

	tc = __raw_readl(SYS_TC(TIMER_CLOCKEVENT));
	REGSET(tc, SYS_TCx, TEN, DIS);
	REGSET(tc, SYS_TCx, TDBG, STOP);
	REGSET(tc, SYS_TCx, TLCS, CPU);
	REGSET(tc, SYS_TCx, TM, IEE);
	REGSET(tc, SYS_TCx, INTS, DIS);
	REGSET(tc, SYS_TCx, UDS, DOWN);
	REGSET(tc, SYS_TCx, TSZ, 32);
	REGSET(tc, SYS_TCx, REN, EN);
	__raw_writel(tc, SYS_TC(TIMER_CLOCKEVENT));

	ns9360_clockevent_device.mult = div_sc(ns9360_cpuclock(),
			NSEC_PER_SEC, ns9360_clockevent_device.shift);
	ns9360_clockevent_device.max_delta_ns =
		clockevent_delta2ns(-1, &ns9360_clockevent_device);
	ns9360_clockevent_device.min_delta_ns =
		clockevent_delta2ns(1, &ns9360_clockevent_device);

	ns9360_clockevent_device.cpumask = cpumask_of(0);
	clockevents_register_device(&ns9360_clockevent_device);

	setup_irq(IRQ_NS9360_TIMER0 + TIMER_CLOCKEVENT,
			&ns9360_clockevent_action);
}

struct sys_timer ns9360_timer = {
	.init = ns9360_timer_init,
};
