// SPDX-License-Identifier: GPL-2.0
/*
 * MStar timer driver
 *
 * Copyright (C) 2021 Daniel Palmer
 * Copyright (C) 2021 Romain Perier
 *
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#ifdef CONFIG_ARM
#include <linux/delay.h>
#endif

#include "timer-of.h"

#define TIMER_NAME "msc313e_timer"

#define MSC313E_REG_CTRL		0x00
#define MSC313E_REG_CTRL_TIMER_EN	BIT(0)
#define MSC313E_REG_CTRL_TIMER_TRIG	BIT(1)
#define MSC313E_REG_CTRL_TIMER_INT_EN	BIT(8)
#define MSC313E_REG_TIMER_MAX_LOW	0x08
#define MSC313E_REG_TIMER_MAX_HIGH	0x0c
#define MSC313E_REG_COUNTER_LOW		0x10
#define MSC313E_REG_COUNTER_HIGH	0x14
#define MSC313E_REG_TIMER_DIVIDE	0x18

#define MSC313E_CLK_DIVIDER		9
#define TIMER_SYNC_TICKS		3

#ifdef CONFIG_ARM
struct msc313e_delay {
	void __iomem *base;
	struct delay_timer delay;
};
static struct msc313e_delay msc313e_delay;
#endif

static void __iomem *msc313e_clksrc;

static void msc313e_timer_stop(void __iomem *base)
{
	writew(0, base + MSC313E_REG_CTRL);
}

static void msc313e_timer_start(void __iomem *base, bool periodic)
{
	u16 reg;

	reg = readw(base + MSC313E_REG_CTRL);
	if (periodic)
		reg |= MSC313E_REG_CTRL_TIMER_EN;
	else
		reg |= MSC313E_REG_CTRL_TIMER_TRIG;
	writew(reg | MSC313E_REG_CTRL_TIMER_INT_EN, base + MSC313E_REG_CTRL);
}

static void msc313e_timer_setup(void __iomem *base, unsigned long delay)
{
	unsigned long flags;

	local_irq_save(flags);
	writew(delay >> 16, base + MSC313E_REG_TIMER_MAX_HIGH);
	writew(delay & 0xffff, base + MSC313E_REG_TIMER_MAX_LOW);
	local_irq_restore(flags);
}

static unsigned long msc313e_timer_current_value(void __iomem *base)
{
	unsigned long flags;
	u16 l, h;

	local_irq_save(flags);
	l = readw(base + MSC313E_REG_COUNTER_LOW);
	h = readw(base + MSC313E_REG_COUNTER_HIGH);
	local_irq_restore(flags);

	return (((u32)h) << 16 | l);
}

static int msc313e_timer_clkevt_shutdown(struct clock_event_device *evt)
{
	struct timer_of *timer = to_timer_of(evt);

	msc313e_timer_stop(timer_of_base(timer));

	return 0;
}

static int msc313e_timer_clkevt_set_oneshot(struct clock_event_device *evt)
{
	struct timer_of *timer = to_timer_of(evt);

	msc313e_timer_stop(timer_of_base(timer));
	msc313e_timer_start(timer_of_base(timer), false);

	return 0;
}

static int msc313e_timer_clkevt_set_periodic(struct clock_event_device *evt)
{
	struct timer_of *timer = to_timer_of(evt);

	msc313e_timer_stop(timer_of_base(timer));
	msc313e_timer_setup(timer_of_base(timer), timer_of_period(timer));
	msc313e_timer_start(timer_of_base(timer), true);

	return 0;
}

static int msc313e_timer_clkevt_next_event(unsigned long evt, struct clock_event_device *clkevt)
{
	struct timer_of *timer = to_timer_of(clkevt);

	msc313e_timer_stop(timer_of_base(timer));
	msc313e_timer_setup(timer_of_base(timer), evt);
	msc313e_timer_start(timer_of_base(timer), false);

	return 0;
}

static irqreturn_t msc313e_timer_clkevt_irq(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static u64 msc313e_timer_clksrc_read(struct clocksource *cs)
{
	return msc313e_timer_current_value(msc313e_clksrc) & cs->mask;
}

#ifdef CONFIG_ARM
static unsigned long msc313e_read_delay_timer_read(void)
{
	return msc313e_timer_current_value(msc313e_delay.base);
}
#endif

static u64 msc313e_timer_sched_clock_read(void)
{
	return msc313e_timer_current_value(msc313e_clksrc);
}

static struct clock_event_device msc313e_clkevt = {
	.name = TIMER_NAME,
	.rating = 300,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown = msc313e_timer_clkevt_shutdown,
	.set_state_periodic = msc313e_timer_clkevt_set_periodic,
	.set_state_oneshot = msc313e_timer_clkevt_set_oneshot,
	.tick_resume = msc313e_timer_clkevt_shutdown,
	.set_next_event = msc313e_timer_clkevt_next_event,
};

static int __init msc313e_clkevt_init(struct device_node *np)
{
	int ret;
	struct timer_of *to;

	to = kzalloc(sizeof(*to), GFP_KERNEL);
	if (!to)
		return -ENOMEM;

	to->flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE;
	to->of_irq.handler = msc313e_timer_clkevt_irq;
	ret = timer_of_init(np, to);
	if (ret)
		return ret;

	if (of_device_is_compatible(np, "sstar,ssd20xd-timer")) {
		to->of_clk.rate = clk_get_rate(to->of_clk.clk) / MSC313E_CLK_DIVIDER;
		to->of_clk.period = DIV_ROUND_UP(to->of_clk.rate, HZ);
		writew(MSC313E_CLK_DIVIDER - 1, timer_of_base(to) + MSC313E_REG_TIMER_DIVIDE);
	}

	msc313e_clkevt.cpumask = cpu_possible_mask;
	msc313e_clkevt.irq = to->of_irq.irq;
	to->clkevt = msc313e_clkevt;

	clockevents_config_and_register(&to->clkevt, timer_of_rate(to),
					TIMER_SYNC_TICKS, 0xffffffff);
	return 0;
}

static int __init msc313e_clksrc_init(struct device_node *np)
{
	struct timer_of to = { 0 };
	int ret;
	u16 reg;

	to.flags = TIMER_OF_BASE | TIMER_OF_CLOCK;
	ret = timer_of_init(np, &to);
	if (ret)
		return ret;

	msc313e_clksrc = timer_of_base(&to);
	reg = readw(msc313e_clksrc + MSC313E_REG_CTRL);
	reg |= MSC313E_REG_CTRL_TIMER_EN;
	writew(reg, msc313e_clksrc + MSC313E_REG_CTRL);

#ifdef CONFIG_ARM
	msc313e_delay.base = timer_of_base(&to);
	msc313e_delay.delay.read_current_timer = msc313e_read_delay_timer_read;
	msc313e_delay.delay.freq = timer_of_rate(&to);

	register_current_timer_delay(&msc313e_delay.delay);
#endif

	sched_clock_register(msc313e_timer_sched_clock_read, 32, timer_of_rate(&to));
	return clocksource_mmio_init(timer_of_base(&to), TIMER_NAME, timer_of_rate(&to), 300, 32,
				     msc313e_timer_clksrc_read);
}

static int __init msc313e_timer_init(struct device_node *np)
{
	int ret = 0;
	static int num_called;

	switch (num_called) {
	case 0:
		ret = msc313e_clksrc_init(np);
		if (ret)
			return ret;
		break;

	default:
		ret = msc313e_clkevt_init(np);
		if (ret)
			return ret;
		break;
	}

	num_called++;

	return 0;
}

TIMER_OF_DECLARE(msc313, "mstar,msc313e-timer", msc313e_timer_init);
TIMER_OF_DECLARE(ssd20xd, "sstar,ssd20xd-timer", msc313e_timer_init);
