// SPDX-License-Identifier: GPL-2.0+
/*
 * RDA8810PL SoC timer driver
 *
 * Copyright RDA Microelectronics Company Limited
 * Copyright (c) 2017 Andreas Färber
 * Copyright (c) 2018 Manivannan Sadhasivam
 *
 * RDA8810PL has two independent timers: OSTIMER (56 bit) and HWTIMER (64 bit).
 * Each timer provides optional interrupt support. In this driver, OSTIMER is
 * used for clockevents and HWTIMER is used for clocksource.
 */

#include <linux/init.h>
#include <linux/interrupt.h>

#include "timer-of.h"

#define RDA_OSTIMER_LOADVAL_L	0x000
#define RDA_OSTIMER_CTRL	0x004
#define RDA_HWTIMER_LOCKVAL_L	0x024
#define RDA_HWTIMER_LOCKVAL_H	0x028
#define RDA_TIMER_IRQ_MASK_SET	0x02c
#define RDA_TIMER_IRQ_MASK_CLR	0x030
#define RDA_TIMER_IRQ_CLR	0x034

#define RDA_OSTIMER_CTRL_ENABLE		BIT(24)
#define RDA_OSTIMER_CTRL_REPEAT		BIT(28)
#define RDA_OSTIMER_CTRL_LOAD		BIT(30)

#define RDA_TIMER_IRQ_MASK_OSTIMER	BIT(0)

#define RDA_TIMER_IRQ_CLR_OSTIMER	BIT(0)

static int rda_ostimer_start(void __iomem *base, bool periodic, u64 cycles)
{
	u32 ctrl, load_l;

	load_l = (u32)cycles;
	ctrl = ((cycles >> 32) & 0xffffff);
	ctrl |= RDA_OSTIMER_CTRL_LOAD | RDA_OSTIMER_CTRL_ENABLE;
	if (periodic)
		ctrl |= RDA_OSTIMER_CTRL_REPEAT;

	/* Enable ostimer interrupt first */
	writel_relaxed(RDA_TIMER_IRQ_MASK_OSTIMER,
		       base + RDA_TIMER_IRQ_MASK_SET);

	/* Write low 32 bits first, high 24 bits are with ctrl */
	writel_relaxed(load_l, base + RDA_OSTIMER_LOADVAL_L);
	writel_relaxed(ctrl, base + RDA_OSTIMER_CTRL);

	return 0;
}

static int rda_ostimer_stop(void __iomem *base)
{
	/* Disable ostimer interrupt first */
	writel_relaxed(RDA_TIMER_IRQ_MASK_OSTIMER,
		       base + RDA_TIMER_IRQ_MASK_CLR);

	writel_relaxed(0, base + RDA_OSTIMER_CTRL);

	return 0;
}

static int rda_ostimer_set_state_shutdown(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	rda_ostimer_stop(timer_of_base(to));

	return 0;
}

static int rda_ostimer_set_state_oneshot(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);

	rda_ostimer_stop(timer_of_base(to));

	return 0;
}

static int rda_ostimer_set_state_periodic(struct clock_event_device *evt)
{
	struct timer_of *to = to_timer_of(evt);
	unsigned long cycles_per_jiffy;

	rda_ostimer_stop(timer_of_base(to));

	cycles_per_jiffy = ((unsigned long long)NSEC_PER_SEC / HZ *
			     evt->mult) >> evt->shift;
	rda_ostimer_start(timer_of_base(to), true, cycles_per_jiffy);

	return 0;
}

static int rda_ostimer_tick_resume(struct clock_event_device *evt)
{
	return 0;
}

static int rda_ostimer_set_next_event(unsigned long evt,
				      struct clock_event_device *ev)
{
	struct timer_of *to = to_timer_of(ev);

	rda_ostimer_start(timer_of_base(to), false, evt);

	return 0;
}

static irqreturn_t rda_ostimer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	struct timer_of *to = to_timer_of(evt);

	/* clear timer int */
	writel_relaxed(RDA_TIMER_IRQ_CLR_OSTIMER,
		       timer_of_base(to) + RDA_TIMER_IRQ_CLR);

	if (evt->event_handler)
		evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of rda_ostimer_of = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE,

	.clkevt = {
		.name = "rda-ostimer",
		.rating = 250,
		.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
			    CLOCK_EVT_FEAT_DYNIRQ,
		.set_state_shutdown = rda_ostimer_set_state_shutdown,
		.set_state_oneshot = rda_ostimer_set_state_oneshot,
		.set_state_periodic = rda_ostimer_set_state_periodic,
		.tick_resume = rda_ostimer_tick_resume,
		.set_next_event	= rda_ostimer_set_next_event,
	},

	.of_base = {
		.name = "rda-timer",
		.index = 0,
	},

	.of_irq = {
		.name = "ostimer",
		.handler = rda_ostimer_interrupt,
		.flags = IRQF_TIMER,
	},
};

static u64 rda_hwtimer_read(struct clocksource *cs)
{
	void __iomem *base = timer_of_base(&rda_ostimer_of);
	u32 lo, hi;

	/* Always read low 32 bits first */
	do {
		lo = readl_relaxed(base + RDA_HWTIMER_LOCKVAL_L);
		hi = readl_relaxed(base + RDA_HWTIMER_LOCKVAL_H);
	} while (hi != readl_relaxed(base + RDA_HWTIMER_LOCKVAL_H));

	return ((u64)hi << 32) | lo;
}

static struct clocksource rda_hwtimer_clocksource = {
	.name           = "rda-timer",
	.rating         = 400,
	.read           = rda_hwtimer_read,
	.mask           = CLOCKSOURCE_MASK(64),
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init rda_timer_init(struct device_node *np)
{
	unsigned long rate = 2000000;
	int ret;

	ret = timer_of_init(np, &rda_ostimer_of);
	if (ret)
		return ret;

	clocksource_register_hz(&rda_hwtimer_clocksource, rate);

	clockevents_config_and_register(&rda_ostimer_of.clkevt, rate,
					0x2, UINT_MAX);

	return 0;
}

TIMER_OF_DECLARE(rda8810pl, "rda,8810pl-timer", rda_timer_init);
