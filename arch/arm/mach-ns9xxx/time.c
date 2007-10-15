/*
 * arch/arm/mach-ns9xxx/time.c
 *
 * Copyright (C) 2006 by Digi International Inc.
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

#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/clock.h>
#include <asm/arch-ns9xxx/irqs.h>
#include <asm/arch/system.h>
#include "generic.h"

#define TIMER_CLOCKSOURCE 0
#define TIMER_CLOCKEVENT 1
static u32 latch;

static cycle_t ns9xxx_clocksource_read(void)
{
	return __raw_readl(SYS_TR(TIMER_CLOCKSOURCE));
}

static struct clocksource ns9xxx_clocksource = {
	.name	= "ns9xxx-timer" __stringify(TIMER_CLOCKSOURCE),
	.rating	= 300,
	.read	= ns9xxx_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.shift	= 20,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void ns9xxx_clockevent_setmode(enum clock_event_mode mode,
		struct clock_event_device *clk)
{
	u32 tc = __raw_readl(SYS_TC(TIMER_CLOCKEVENT));

	switch(mode) {
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

static int ns9xxx_clockevent_setnextevent(unsigned long evt,
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

static struct clock_event_device ns9xxx_clockevent_device = {
	.name		= "ns9xxx-timer" __stringify(TIMER_CLOCKEVENT),
	.shift		= 20,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= ns9xxx_clockevent_setmode,
	.set_next_event	= ns9xxx_clockevent_setnextevent,
};

static irqreturn_t ns9xxx_clockevent_handler(int irq, void *dev_id)
{
	int timerno = irq - IRQ_TIMER0;
	u32 tc;

	struct clock_event_device *evt = &ns9xxx_clockevent_device;

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

static struct irqaction ns9xxx_clockevent_action = {
	.name		= "ns9xxx-timer" __stringify(TIMER_CLOCKEVENT),
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= ns9xxx_clockevent_handler,
};

static void __init ns9xxx_timer_init(void)
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

	ns9xxx_clocksource.mult = clocksource_hz2mult(ns9xxx_cpuclock(),
			ns9xxx_clocksource.shift);

	clocksource_register(&ns9xxx_clocksource);

	latch = SH_DIV(ns9xxx_cpuclock(), HZ, 0);

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

	ns9xxx_clockevent_device.mult = div_sc(ns9xxx_cpuclock(),
			NSEC_PER_SEC, ns9xxx_clockevent_device.shift);
	ns9xxx_clockevent_device.max_delta_ns =
		clockevent_delta2ns(-1, &ns9xxx_clockevent_device);
	ns9xxx_clockevent_device.min_delta_ns =
		clockevent_delta2ns(1, &ns9xxx_clockevent_device);

	ns9xxx_clockevent_device.cpumask = cpumask_of_cpu(0);
	clockevents_register_device(&ns9xxx_clockevent_device);

	setup_irq(IRQ_TIMER0 + TIMER_CLOCKEVENT, &ns9xxx_clockevent_action);
}

struct sys_timer ns9xxx_timer = {
	.init = ns9xxx_timer_init,
};
