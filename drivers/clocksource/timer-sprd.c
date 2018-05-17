// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 */

#include <linux/init.h>
#include <linux/interrupt.h>

#include "timer-of.h"

#define TIMER_NAME		"sprd_timer"

#define TIMER_LOAD_LO		0x0
#define TIMER_LOAD_HI		0x4
#define TIMER_VALUE_LO		0x8
#define TIMER_VALUE_HI		0xc

#define TIMER_CTL		0x10
#define TIMER_CTL_PERIOD_MODE	BIT(0)
#define TIMER_CTL_ENABLE	BIT(1)
#define TIMER_CTL_64BIT_WIDTH	BIT(16)

#define TIMER_INT		0x14
#define TIMER_INT_EN		BIT(0)
#define TIMER_INT_RAW_STS	BIT(1)
#define TIMER_INT_MASK_STS	BIT(2)
#define TIMER_INT_CLR		BIT(3)

#define TIMER_VALUE_SHDW_LO	0x18
#define TIMER_VALUE_SHDW_HI	0x1c

#define TIMER_VALUE_LO_MASK	GENMASK(31, 0)

static void sprd_timer_enable(void __iomem *base, u32 flag)
{
	u32 val = readl_relaxed(base + TIMER_CTL);

	val |= TIMER_CTL_ENABLE;
	if (flag & TIMER_CTL_64BIT_WIDTH)
		val |= TIMER_CTL_64BIT_WIDTH;
	else
		val &= ~TIMER_CTL_64BIT_WIDTH;

	if (flag & TIMER_CTL_PERIOD_MODE)
		val |= TIMER_CTL_PERIOD_MODE;
	else
		val &= ~TIMER_CTL_PERIOD_MODE;

	writel_relaxed(val, base + TIMER_CTL);
}

static void sprd_timer_disable(void __iomem *base)
{
	u32 val = readl_relaxed(base + TIMER_CTL);

	val &= ~TIMER_CTL_ENABLE;
	writel_relaxed(val, base + TIMER_CTL);
}

static void sprd_timer_update_counter(void __iomem *base, unsigned long cycles)
{
	writel_relaxed(cycles & TIMER_VALUE_LO_MASK, base + TIMER_LOAD_LO);
	writel_relaxed(0, base + TIMER_LOAD_HI);
}

static void sprd_timer_enable_interrupt(void __iomem *base)
{
	writel_relaxed(TIMER_INT_EN, base + TIMER_INT);
}

static void sprd_timer_clear_interrupt(void __iomem *base)
{
	u32 val = readl_relaxed(base + TIMER_INT);

	val |= TIMER_INT_CLR;
	writel_relaxed(val, base + TIMER_INT);
}

static int sprd_timer_set_next_event(unsigned long cycles,
				     struct clock_event_device *ce)
{
	struct timer_of *to = to_timer_of(ce);

	sprd_timer_disable(timer_of_base(to));
	sprd_timer_update_counter(timer_of_base(to), cycles);
	sprd_timer_enable(timer_of_base(to), 0);

	return 0;
}

static int sprd_timer_set_periodic(struct clock_event_device *ce)
{
	struct timer_of *to = to_timer_of(ce);

	sprd_timer_disable(timer_of_base(to));
	sprd_timer_update_counter(timer_of_base(to), timer_of_period(to));
	sprd_timer_enable(timer_of_base(to), TIMER_CTL_PERIOD_MODE);

	return 0;
}

static int sprd_timer_shutdown(struct clock_event_device *ce)
{
	struct timer_of *to = to_timer_of(ce);

	sprd_timer_disable(timer_of_base(to));
	return 0;
}

static irqreturn_t sprd_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(ce);

	sprd_timer_clear_interrupt(timer_of_base(to));

	if (clockevent_state_oneshot(ce))
		sprd_timer_disable(timer_of_base(to));

	ce->event_handler(ce);
	return IRQ_HANDLED;
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,

	.clkevt = {
		.name = TIMER_NAME,
		.rating = 300,
		.features = CLOCK_EVT_FEAT_DYNIRQ | CLOCK_EVT_FEAT_PERIODIC |
			CLOCK_EVT_FEAT_ONESHOT,
		.set_state_shutdown = sprd_timer_shutdown,
		.set_state_periodic = sprd_timer_set_periodic,
		.set_next_event = sprd_timer_set_next_event,
		.cpumask = cpu_possible_mask,
	},

	.of_irq = {
		.handler = sprd_timer_interrupt,
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
	},
};

static int __init sprd_timer_init(struct device_node *np)
{
	int ret;

	ret = timer_of_init(np, &to);
	if (ret)
		return ret;

	sprd_timer_enable_interrupt(timer_of_base(&to));
	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					1, UINT_MAX);

	return 0;
}

TIMER_OF_DECLARE(sc9860_timer, "sprd,sc9860-timer", sprd_timer_init);
