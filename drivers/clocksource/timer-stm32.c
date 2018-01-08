/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Inspired by time-efm32.c from Uwe Kleine-Koenig
 */

#include <linux/kernel.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "timer-of.h"

#define TIM_CR1		0x00
#define TIM_DIER	0x0c
#define TIM_SR		0x10
#define TIM_EGR		0x14
#define TIM_PSC		0x28
#define TIM_ARR		0x2c

#define TIM_CR1_CEN	BIT(0)
#define TIM_CR1_OPM	BIT(3)
#define TIM_CR1_ARPE	BIT(7)

#define TIM_DIER_UIE	BIT(0)

#define TIM_SR_UIF	BIT(0)

#define TIM_EGR_UG	BIT(0)

static int stm32_clock_event_shutdown(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	writel_relaxed(0, timer_of_base(to) + TIM_CR1);

	return 0;
}

static int stm32_clock_event_set_periodic(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	writel_relaxed(timer_of_period(to), timer_of_base(to) + TIM_ARR);
	writel_relaxed(TIM_CR1_ARPE | TIM_CR1_CEN, timer_of_base(to) + TIM_CR1);

	return 0;
}

static int stm32_clock_event_set_next_event(unsigned long evt,
					    struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	writel_relaxed(evt, timer_of_base(to) + TIM_ARR);
	writel_relaxed(TIM_CR1_ARPE | TIM_CR1_OPM | TIM_CR1_CEN,
		       timer_of_base(to) + TIM_CR1);

	return 0;
}

static irqreturn_t stm32_clock_event_handler(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	writel_relaxed(0, timer_of_base(to) + TIM_SR);

	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static void __init stm32_clockevent_init(struct timer_of *to)
{
	unsigned long max_delta;
	int prescaler;

	to->clkevt.name = to->np->full_name;
	to->clkevt.features = CLOCK_EVT_FEAT_PERIODIC;
	to->clkevt.set_state_shutdown = stm32_clock_event_shutdown;
	to->clkevt.set_state_periodic = stm32_clock_event_set_periodic;
	to->clkevt.set_state_oneshot = stm32_clock_event_shutdown;
	to->clkevt.tick_resume = stm32_clock_event_shutdown;
	to->clkevt.set_next_event = stm32_clock_event_set_next_event;

	/* Detect whether the timer is 16 or 32 bits */
	writel_relaxed(~0U, timer_of_base(to) + TIM_ARR);
	max_delta = readl_relaxed(timer_of_base(to) + TIM_ARR);
	if (max_delta == ~0U) {
		prescaler = 1;
		to->clkevt.rating = 250;
	} else {
		prescaler = 1024;
		to->clkevt.rating = 100;
	}
	writel_relaxed(0, timer_of_base(to) + TIM_ARR);

	writel_relaxed(prescaler - 1, timer_of_base(to) + TIM_PSC);
	writel_relaxed(TIM_EGR_UG, timer_of_base(to) + TIM_EGR);
	writel_relaxed(0, timer_of_base(to) + TIM_SR);
	writel_relaxed(TIM_DIER_UIE, timer_of_base(to) + TIM_DIER);

	/* Adjust rate and period given the prescaler value */
	to->of_clk.rate = DIV_ROUND_CLOSEST(to->of_clk.rate, prescaler);
	to->of_clk.period = DIV_ROUND_UP(to->of_clk.rate, HZ);

	clockevents_config_and_register(&to->clkevt,
					timer_of_rate(to), 0x1, max_delta);

	pr_info("%pOF: STM32 clockevent driver initialized (%d bits)\n",
		to->np, max_delta == UINT_MAX ? 32 : 16);
}

static int __init stm32_timer_init(struct device_node *node)
{
	struct reset_control *rstc;
	struct timer_of *to;
	int ret;

	to = kzalloc(sizeof(*to), GFP_KERNEL);
	if (!to)
		return -ENOMEM;

	to->flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE;
	to->of_irq.handler = stm32_clock_event_handler;

	ret = timer_of_init(node, to);
	if (ret)
		goto err;

	rstc = of_reset_control_get(node, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	stm32_clockevent_init(to);
	return 0;
err:
	kfree(to);
	return ret;
}

TIMER_OF_DECLARE(stm32, "st,stm32-timer", stm32_timer_init);
