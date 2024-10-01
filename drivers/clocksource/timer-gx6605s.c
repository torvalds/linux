// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched_clock.h>

#include "timer-of.h"

#define CLKSRC_OFFSET	0x40

#define TIMER_STATUS	0x00
#define TIMER_VALUE	0x04
#define TIMER_CONTRL	0x10
#define TIMER_CONFIG	0x20
#define TIMER_DIV	0x24
#define TIMER_INI	0x28

#define GX6605S_STATUS_CLR	BIT(0)
#define GX6605S_CONTRL_RST	BIT(0)
#define GX6605S_CONTRL_START	BIT(1)
#define GX6605S_CONFIG_EN	BIT(0)
#define GX6605S_CONFIG_IRQ_EN	BIT(1)

static irqreturn_t gx6605s_timer_interrupt(int irq, void *dev)
{
	struct clock_event_device *ce = dev;
	void __iomem *base = timer_of_base(to_timer_of(ce));

	writel_relaxed(GX6605S_STATUS_CLR, base + TIMER_STATUS);
	writel_relaxed(0, base + TIMER_INI);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

static int gx6605s_timer_set_oneshot(struct clock_event_device *ce)
{
	void __iomem *base = timer_of_base(to_timer_of(ce));

	/* reset and stop counter */
	writel_relaxed(GX6605S_CONTRL_RST, base + TIMER_CONTRL);

	/* enable with irq and start */
	writel_relaxed(GX6605S_CONFIG_EN | GX6605S_CONFIG_IRQ_EN,
		       base + TIMER_CONFIG);

	return 0;
}

static int gx6605s_timer_set_next_event(unsigned long delta,
					struct clock_event_device *ce)
{
	void __iomem *base = timer_of_base(to_timer_of(ce));

	/* use reset to pause timer */
	writel_relaxed(GX6605S_CONTRL_RST, base + TIMER_CONTRL);

	/* config next timeout value */
	writel_relaxed(ULONG_MAX - delta, base + TIMER_INI);
	writel_relaxed(GX6605S_CONTRL_START, base + TIMER_CONTRL);

	return 0;
}

static int gx6605s_timer_shutdown(struct clock_event_device *ce)
{
	void __iomem *base = timer_of_base(to_timer_of(ce));

	writel_relaxed(0, base + TIMER_CONTRL);
	writel_relaxed(0, base + TIMER_CONFIG);

	return 0;
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,
	.clkevt = {
		.rating			= 300,
		.features		= CLOCK_EVT_FEAT_DYNIRQ |
					  CLOCK_EVT_FEAT_ONESHOT,
		.set_state_shutdown	= gx6605s_timer_shutdown,
		.set_state_oneshot	= gx6605s_timer_set_oneshot,
		.set_next_event		= gx6605s_timer_set_next_event,
		.cpumask		= cpu_possible_mask,
	},
	.of_irq = {
		.handler		= gx6605s_timer_interrupt,
		.flags			= IRQF_TIMER | IRQF_IRQPOLL,
	},
};

static u64 notrace gx6605s_sched_clock_read(void)
{
	void __iomem *base;

	base = timer_of_base(&to) + CLKSRC_OFFSET;

	return (u64)readl_relaxed(base + TIMER_VALUE);
}

static void gx6605s_clkevt_init(void __iomem *base)
{
	writel_relaxed(0, base + TIMER_DIV);
	writel_relaxed(0, base + TIMER_CONFIG);

	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to), 2,
					ULONG_MAX);
}

static int gx6605s_clksrc_init(void __iomem *base)
{
	writel_relaxed(0, base + TIMER_DIV);
	writel_relaxed(0, base + TIMER_INI);

	writel_relaxed(GX6605S_CONTRL_RST, base + TIMER_CONTRL);

	writel_relaxed(GX6605S_CONFIG_EN, base + TIMER_CONFIG);

	writel_relaxed(GX6605S_CONTRL_START, base + TIMER_CONTRL);

	sched_clock_register(gx6605s_sched_clock_read, 32, timer_of_rate(&to));

	return clocksource_mmio_init(base + TIMER_VALUE, "gx6605s",
			timer_of_rate(&to), 200, 32, clocksource_mmio_readl_up);
}

static int __init gx6605s_timer_init(struct device_node *np)
{
	int ret;

	/*
	 * The timer driver is for nationalchip gx6605s SOC and there are two
	 * same timer in gx6605s. We use one for clkevt and another for clksrc.
	 *
	 * The timer is mmio map to access, so we need give mmio address in dts.
	 *
	 * It provides a 32bit countup timer and interrupt will be caused by
	 * count-overflow.
	 * So we need set-next-event by ULONG_MAX - delta in TIMER_INI reg.
	 *
	 * The counter at 0x0  offset is clock event.
	 * The counter at 0x40 offset is clock source.
	 * They are the same in hardware, just different used by driver.
	 */
	ret = timer_of_init(np, &to);
	if (ret)
		return ret;

	gx6605s_clkevt_init(timer_of_base(&to));

	return gx6605s_clksrc_init(timer_of_base(&to) + CLKSRC_OFFSET);
}
TIMER_OF_DECLARE(csky_gx6605s_timer, "csky,gx6605s-timer", gx6605s_timer_init);
