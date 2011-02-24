/*
 * linux/arch/arm/mach-w90x900/time.c
 *
 * Based on linux/arch/arm/plat-s3c24xx/time.c by Ben Dooks
 *
 * Copyright (c) 2009 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include <asm/mach-types.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#include <mach/map.h>
#include <mach/regs-timer.h>

#define RESETINT	0x1f
#define PERIOD		(0x01 << 27)
#define ONESHOT		(0x00 << 27)
#define COUNTEN		(0x01 << 30)
#define INTEN		(0x01 << 29)

#define TICKS_PER_SEC	100
#define PRESCALE	0x63 /* Divider = prescale + 1 */

#define	TDR_SHIFT	24
#define	TDR_MASK	((1 << TDR_SHIFT) - 1)

static unsigned int timer0_load;

static void nuc900_clockevent_setmode(enum clock_event_mode mode,
		struct clock_event_device *clk)
{
	unsigned int val;

	val = __raw_readl(REG_TCSR0);
	val &= ~(0x03 << 27);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writel(timer0_load, REG_TICR0);
		val |= (PERIOD | COUNTEN | INTEN | PRESCALE);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		val |= (ONESHOT | COUNTEN | INTEN | PRESCALE);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}

	__raw_writel(val, REG_TCSR0);
}

static int nuc900_clockevent_setnextevent(unsigned long evt,
		struct clock_event_device *clk)
{
	unsigned int val;

	__raw_writel(evt, REG_TICR0);

	val = __raw_readl(REG_TCSR0);
	val |= (COUNTEN | INTEN | PRESCALE);
	__raw_writel(val, REG_TCSR0);

	return 0;
}

static struct clock_event_device nuc900_clockevent_device = {
	.name		= "nuc900-timer0",
	.shift		= 32,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= nuc900_clockevent_setmode,
	.set_next_event	= nuc900_clockevent_setnextevent,
	.rating		= 300,
};

/*IRQ handler for the timer*/

static irqreturn_t nuc900_timer0_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &nuc900_clockevent_device;

	__raw_writel(0x01, REG_TISR); /* clear TIF0 */

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction nuc900_timer0_irq = {
	.name		= "nuc900-timer0",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= nuc900_timer0_interrupt,
};

static void __init nuc900_clockevents_init(void)
{
	unsigned int rate;
	struct clk *clk = clk_get(NULL, "timer0");

	BUG_ON(IS_ERR(clk));

	__raw_writel(0x00, REG_TCSR0);

	clk_enable(clk);
	rate = clk_get_rate(clk) / (PRESCALE + 1);

	timer0_load = (rate / TICKS_PER_SEC);

	__raw_writel(RESETINT, REG_TISR);
	setup_irq(IRQ_TIMER0, &nuc900_timer0_irq);

	nuc900_clockevent_device.mult = div_sc(rate, NSEC_PER_SEC,
					nuc900_clockevent_device.shift);
	nuc900_clockevent_device.max_delta_ns = clockevent_delta2ns(0xffffffff,
					&nuc900_clockevent_device);
	nuc900_clockevent_device.min_delta_ns = clockevent_delta2ns(0xf,
					&nuc900_clockevent_device);
	nuc900_clockevent_device.cpumask = cpumask_of(0);

	clockevents_register_device(&nuc900_clockevent_device);
}

static cycle_t nuc900_get_cycles(struct clocksource *cs)
{
	return (~__raw_readl(REG_TDR1)) & TDR_MASK;
}

static struct clocksource clocksource_nuc900 = {
	.name	= "nuc900-timer1",
	.rating	= 200,
	.read	= nuc900_get_cycles,
	.mask	= CLOCKSOURCE_MASK(TDR_SHIFT),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init nuc900_clocksource_init(void)
{
	unsigned int val;
	unsigned int rate;
	struct clk *clk = clk_get(NULL, "timer1");

	BUG_ON(IS_ERR(clk));

	__raw_writel(0x00, REG_TCSR1);

	clk_enable(clk);
	rate = clk_get_rate(clk) / (PRESCALE + 1);

	__raw_writel(0xffffffff, REG_TICR1);

	val = __raw_readl(REG_TCSR1);
	val |= (COUNTEN | PERIOD | PRESCALE);
	__raw_writel(val, REG_TCSR1);

	clocksource_register_hz(&clocksource_nuc900, rate);
}

static void __init nuc900_timer_init(void)
{
	nuc900_clocksource_init();
	nuc900_clockevents_init();
}

struct sys_timer nuc900_timer = {
	.init		= nuc900_timer_init,
};
