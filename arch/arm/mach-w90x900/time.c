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
#include "regs-timer.h"

#include "nuc9xx.h"

#define RESETINT	0x1f
#define PERIOD		(0x01 << 27)
#define ONESHOT		(0x00 << 27)
#define COUNTEN		(0x01 << 30)
#define INTEN		(0x01 << 29)

#define TICKS_PER_SEC	100
#define PRESCALE	0x63 /* Divider = prescale + 1 */

#define	TDR_SHIFT	24

static unsigned int timer0_load;

static int nuc900_clockevent_shutdown(struct clock_event_device *evt)
{
	unsigned int val = __raw_readl(REG_TCSR0) & ~(0x03 << 27);

	__raw_writel(val, REG_TCSR0);
	return 0;
}

static int nuc900_clockevent_set_oneshot(struct clock_event_device *evt)
{
	unsigned int val = __raw_readl(REG_TCSR0) & ~(0x03 << 27);

	val |= (ONESHOT | COUNTEN | INTEN | PRESCALE);

	__raw_writel(val, REG_TCSR0);
	return 0;
}

static int nuc900_clockevent_set_periodic(struct clock_event_device *evt)
{
	unsigned int val = __raw_readl(REG_TCSR0) & ~(0x03 << 27);

	__raw_writel(timer0_load, REG_TICR0);
	val |= (PERIOD | COUNTEN | INTEN | PRESCALE);
	__raw_writel(val, REG_TCSR0);
	return 0;
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
	.name			= "nuc900-timer0",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown	= nuc900_clockevent_shutdown,
	.set_state_periodic	= nuc900_clockevent_set_periodic,
	.set_state_oneshot	= nuc900_clockevent_set_oneshot,
	.tick_resume		= nuc900_clockevent_shutdown,
	.set_next_event		= nuc900_clockevent_setnextevent,
	.rating			= 300,
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
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
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

	nuc900_clockevent_device.cpumask = cpumask_of(0);

	clockevents_config_and_register(&nuc900_clockevent_device, rate,
					0xf, 0xffffffff);
}

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

	clocksource_mmio_init(REG_TDR1, "nuc900-timer1", rate, 200,
		TDR_SHIFT, clocksource_mmio_readl_down);
}

void __init nuc900_timer_init(void)
{
	nuc900_clocksource_init();
	nuc900_clockevents_init();
}
