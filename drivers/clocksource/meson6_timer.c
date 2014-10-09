/*
 * Amlogic Meson6 SoCs timer handling.
 *
 * Copyright (C) 2014 Carlo Caione <carlo@caione.org>
 *
 * Based on code from Amlogic, Inc
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
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

#define CED_ID			0
#define CSD_ID			4

#define TIMER_ISA_MUX		0
#define TIMER_ISA_VAL(t)	(((t) + 1) << 2)

#define TIMER_INPUT_BIT(t)	(2 * (t))
#define TIMER_ENABLE_BIT(t)	(16 + (t))
#define TIMER_PERIODIC_BIT(t)	(12 + (t))

#define TIMER_CED_INPUT_MASK	(3UL << TIMER_INPUT_BIT(CED_ID))
#define TIMER_CSD_INPUT_MASK	(7UL << TIMER_INPUT_BIT(CSD_ID))

#define TIMER_CED_UNIT_1US	0
#define TIMER_CSD_UNIT_1US	1

static void __iomem *timer_base;

static u64 notrace meson6_timer_sched_read(void)
{
	return (u64)readl(timer_base + TIMER_ISA_VAL(CSD_ID));
}

static void meson6_clkevt_time_stop(unsigned char timer)
{
	u32 val = readl(timer_base + TIMER_ISA_MUX);

	writel(val & ~TIMER_ENABLE_BIT(timer), timer_base + TIMER_ISA_MUX);
}

static void meson6_clkevt_time_setup(unsigned char timer, unsigned long delay)
{
	writel(delay, timer_base + TIMER_ISA_VAL(timer));
}

static void meson6_clkevt_time_start(unsigned char timer, bool periodic)
{
	u32 val = readl(timer_base + TIMER_ISA_MUX);

	if (periodic)
		val |= TIMER_PERIODIC_BIT(timer);
	else
		val &= ~TIMER_PERIODIC_BIT(timer);

	writel(val | TIMER_ENABLE_BIT(timer), timer_base + TIMER_ISA_MUX);
}

static void meson6_clkevt_mode(enum clock_event_mode mode,
			       struct clock_event_device *clk)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		meson6_clkevt_time_stop(CED_ID);
		meson6_clkevt_time_setup(CED_ID, USEC_PER_SEC/HZ - 1);
		meson6_clkevt_time_start(CED_ID, true);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		meson6_clkevt_time_stop(CED_ID);
		meson6_clkevt_time_start(CED_ID, false);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		meson6_clkevt_time_stop(CED_ID);
		break;
	}
}

static int meson6_clkevt_next_event(unsigned long evt,
				    struct clock_event_device *unused)
{
	meson6_clkevt_time_stop(CED_ID);
	meson6_clkevt_time_setup(CED_ID, evt);
	meson6_clkevt_time_start(CED_ID, false);

	return 0;
}

static struct clock_event_device meson6_clockevent = {
	.name		= "meson6_tick",
	.rating		= 400,
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= meson6_clkevt_mode,
	.set_next_event	= meson6_clkevt_next_event,
};

static irqreturn_t meson6_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction meson6_timer_irq = {
	.name		= "meson6_timer",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= meson6_timer_interrupt,
	.dev_id		= &meson6_clockevent,
};

static void __init meson6_timer_init(struct device_node *node)
{
	u32 val;
	int ret, irq;

	timer_base = of_io_request_and_map(node, 0, "meson6-timer");
	if (IS_ERR(timer_base))
		panic("Can't map registers");

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0)
		panic("Can't parse IRQ");

	/* Set 1us for timer E */
	val = readl(timer_base + TIMER_ISA_MUX);
	val &= ~TIMER_CSD_INPUT_MASK;
	val |= TIMER_CSD_UNIT_1US << TIMER_INPUT_BIT(CSD_ID);
	writel(val, timer_base + TIMER_ISA_MUX);

	sched_clock_register(meson6_timer_sched_read, 32, USEC_PER_SEC);
	clocksource_mmio_init(timer_base + TIMER_ISA_VAL(CSD_ID), node->name,
			      1000 * 1000, 300, 32, clocksource_mmio_readl_up);

	/* Timer A base 1us */
	val &= ~TIMER_CED_INPUT_MASK;
	val |= TIMER_CED_UNIT_1US << TIMER_INPUT_BIT(CED_ID);
	writel(val, timer_base + TIMER_ISA_MUX);

	/* Stop the timer A */
	meson6_clkevt_time_stop(CED_ID);

	ret = setup_irq(irq, &meson6_timer_irq);
	if (ret)
		pr_warn("failed to setup irq %d\n", irq);

	meson6_clockevent.cpumask = cpu_possible_mask;
	meson6_clockevent.irq = irq;

	clockevents_config_and_register(&meson6_clockevent, USEC_PER_SEC,
					1, 0xfffe);
}
CLOCKSOURCE_OF_DECLARE(meson6, "amlogic,meson6-timer",
		       meson6_timer_init);
