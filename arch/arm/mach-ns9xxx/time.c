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

#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/clock.h>
#include <asm/arch-ns9xxx/irqs.h>
#include <asm/arch/system.h>
#include "generic.h"

#define TIMERCLOCKSELECT 64
#define TIMER_CLOCKSOURCE 1

static irqreturn_t
ns9xxx_timer_interrupt(int irq, void *dev_id)
{
	int timerno = irq - IRQ_TIMER0;
	u32 tc;

	write_seqlock(&xtime_lock);
	timer_tick();
	write_sequnlock(&xtime_lock);

	/* clear irq */
	tc = SYS_TC(timerno);
	if (REGGET(tc, SYS_TCx, REN) == SYS_TCx_REN_DIS) {
		REGSET(tc, SYS_TCx, TEN, DIS);
		SYS_TC(timerno) = tc;
	}
	REGSET(tc, SYS_TCx, INTC, SET);
	SYS_TC(timerno) = tc;
	REGSET(tc, SYS_TCx, INTC, UNSET);
	SYS_TC(timerno) = tc;

	return IRQ_HANDLED;
}

static struct irqaction ns9xxx_timer_irq = {
	.name = "NS9xxx Timer Tick",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = ns9xxx_timer_interrupt,
};

static cycle_t ns9xxx_clocksource_read(void)
{
	return SYS_TR(TIMER_CLOCKSOURCE);
}

static struct clocksource ns9xxx_clocksource = {
	.name	= "ns9xxx-timer" __stringify(TIMER_CLOCKSOURCE),
	.rating	= 300,
	.read	= ns9xxx_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.shift	= 20,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init ns9xxx_timer_init(void)
{
	int tc;

	/* disable timer */
	if ((tc = SYS_TC(0)) & SYS_TCx_TEN)
		SYS_TC(0) = tc & ~SYS_TCx_TEN;

	SYS_TRC(0) = SH_DIV(ns9xxx_cpuclock(), (TIMERCLOCKSELECT * HZ), 0);

	REGSET(tc, SYS_TCx, TEN, EN);
	REGSET(tc, SYS_TCx, TLCS, DIV64); /* This must match TIMERCLOCKSELECT */
	REGSET(tc, SYS_TCx, INTS, EN);
	REGSET(tc, SYS_TCx, UDS, DOWN);
	REGSET(tc, SYS_TCx, TDBG, STOP);
	REGSET(tc, SYS_TCx, TSZ, 32);
	REGSET(tc, SYS_TCx, REN, EN);
	SYS_TC(0) = tc;

	setup_irq(IRQ_TIMER0, &ns9xxx_timer_irq);

	tc = SYS_TC(TIMER_CLOCKSOURCE);
	if (REGGET(tc, SYS_TCx, TEN)) {
		REGSET(tc, SYS_TCx, TEN, DIS);
		SYS_TC(TIMER_CLOCKSOURCE) = tc;
	}

	SYS_TRC(TIMER_CLOCKSOURCE) = 0;

	REGSET(tc, SYS_TCx, TEN, EN);
	REGSET(tc, SYS_TCx, TDBG, STOP);
	REGSET(tc, SYS_TCx, TLCS, CPU);
	REGSET(tc, SYS_TCx, TM, IEE);
	REGSET(tc, SYS_TCx, INTS, DIS);
	REGSET(tc, SYS_TCx, UDS, UP);
	REGSET(tc, SYS_TCx, TSZ, 32);
	REGSET(tc, SYS_TCx, REN, EN);

	SYS_TC(TIMER_CLOCKSOURCE) = tc;

	ns9xxx_clocksource.mult = clocksource_hz2mult(ns9xxx_cpuclock(),
			ns9xxx_clocksource.shift);

	clocksource_register(&ns9xxx_clocksource);
}

struct sys_timer ns9xxx_timer = {
	.init = ns9xxx_timer_init,
};
