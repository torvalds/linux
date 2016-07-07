/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

/*
 * Each pit takes 0x10 Bytes register space
 */
#define PITMCR		0x00
#define PIT0_OFFSET	0x100
#define PITn_OFFSET(n)	(PIT0_OFFSET + 0x10 * (n))
#define PITLDVAL	0x00
#define PITCVAL		0x04
#define PITTCTRL	0x08
#define PITTFLG		0x0c

#define PITMCR_MDIS	(0x1 << 1)

#define PITTCTRL_TEN	(0x1 << 0)
#define PITTCTRL_TIE	(0x1 << 1)
#define PITCTRL_CHN	(0x1 << 2)

#define PITTFLG_TIF	0x1

static void __iomem *clksrc_base;
static void __iomem *clkevt_base;
static unsigned long cycle_per_jiffy;

static inline void pit_timer_enable(void)
{
	__raw_writel(PITTCTRL_TEN | PITTCTRL_TIE, clkevt_base + PITTCTRL);
}

static inline void pit_timer_disable(void)
{
	__raw_writel(0, clkevt_base + PITTCTRL);
}

static inline void pit_irq_acknowledge(void)
{
	__raw_writel(PITTFLG_TIF, clkevt_base + PITTFLG);
}

static u64 notrace pit_read_sched_clock(void)
{
	return ~__raw_readl(clksrc_base + PITCVAL);
}

static int __init pit_clocksource_init(unsigned long rate)
{
	/* set the max load value and start the clock source counter */
	__raw_writel(0, clksrc_base + PITTCTRL);
	__raw_writel(~0UL, clksrc_base + PITLDVAL);
	__raw_writel(PITTCTRL_TEN, clksrc_base + PITTCTRL);

	sched_clock_register(pit_read_sched_clock, 32, rate);
	return clocksource_mmio_init(clksrc_base + PITCVAL, "vf-pit", rate,
			300, 32, clocksource_mmio_readl_down);
}

static int pit_set_next_event(unsigned long delta,
				struct clock_event_device *unused)
{
	/*
	 * set a new value to PITLDVAL register will not restart the timer,
	 * to abort the current cycle and start a timer period with the new
	 * value, the timer must be disabled and enabled again.
	 * and the PITLAVAL should be set to delta minus one according to pit
	 * hardware requirement.
	 */
	pit_timer_disable();
	__raw_writel(delta - 1, clkevt_base + PITLDVAL);
	pit_timer_enable();

	return 0;
}

static int pit_shutdown(struct clock_event_device *evt)
{
	pit_timer_disable();
	return 0;
}

static int pit_set_periodic(struct clock_event_device *evt)
{
	pit_set_next_event(cycle_per_jiffy, evt);
	return 0;
}

static irqreturn_t pit_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	pit_irq_acknowledge();

	/*
	 * pit hardware doesn't support oneshot, it will generate an interrupt
	 * and reload the counter value from PITLDVAL when PITCVAL reach zero,
	 * and start the counter again. So software need to disable the timer
	 * to stop the counter loop in ONESHOT mode.
	 */
	if (likely(clockevent_state_oneshot(evt)))
		pit_timer_disable();

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct clock_event_device clockevent_pit = {
	.name		= "VF pit timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown = pit_shutdown,
	.set_state_periodic = pit_set_periodic,
	.set_next_event	= pit_set_next_event,
	.rating		= 300,
};

static struct irqaction pit_timer_irq = {
	.name		= "VF pit timer",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= pit_timer_interrupt,
	.dev_id		= &clockevent_pit,
};

static int __init pit_clockevent_init(unsigned long rate, int irq)
{
	__raw_writel(0, clkevt_base + PITTCTRL);
	__raw_writel(PITTFLG_TIF, clkevt_base + PITTFLG);

	BUG_ON(setup_irq(irq, &pit_timer_irq));

	clockevent_pit.cpumask = cpumask_of(0);
	clockevent_pit.irq = irq;
	/*
	 * The value for the LDVAL register trigger is calculated as:
	 * LDVAL trigger = (period / clock period) - 1
	 * The pit is a 32-bit down count timer, when the conter value
	 * reaches 0, it will generate an interrupt, thus the minimal
	 * LDVAL trigger value is 1. And then the min_delta is
	 * minimal LDVAL trigger value + 1, and the max_delta is full 32-bit.
	 */
	clockevents_config_and_register(&clockevent_pit, rate, 2, 0xffffffff);

	return 0;
}

static int __init pit_timer_init(struct device_node *np)
{
	struct clk *pit_clk;
	void __iomem *timer_base;
	unsigned long clk_rate;
	int irq, ret;

	timer_base = of_iomap(np, 0);
	if (!timer_base) {
		pr_err("Failed to iomap");
		return -ENXIO;
	}

	/*
	 * PIT0 and PIT1 can be chained to build a 64-bit timer,
	 * so choose PIT2 as clocksource, PIT3 as clockevent device,
	 * and leave PIT0 and PIT1 unused for anyone else who needs them.
	 */
	clksrc_base = timer_base + PITn_OFFSET(2);
	clkevt_base = timer_base + PITn_OFFSET(3);

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0)
		return -EINVAL;

	pit_clk = of_clk_get(np, 0);
	if (IS_ERR(pit_clk))
		return PTR_ERR(pit_clk);

	ret = clk_prepare_enable(pit_clk);
	if (ret)
		return ret;

	clk_rate = clk_get_rate(pit_clk);
	cycle_per_jiffy = clk_rate / (HZ);

	/* enable the pit module */
	__raw_writel(~PITMCR_MDIS, timer_base + PITMCR);

	ret = pit_clocksource_init(clk_rate);
	if (ret)
		return ret;

	return pit_clockevent_init(clk_rate, irq);
}
CLOCKSOURCE_OF_DECLARE(vf610, "fsl,vf610-pit", pit_timer_init);
