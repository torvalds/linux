// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
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
#define PIT_CH(n)       (PIT0_OFFSET + 0x10 * (n))
#define PITLDVAL	0x00
#define PITCVAL		0x04
#define PITTCTRL	0x08
#define PITTFLG		0x0c

#define PITMCR_MDIS	(0x1 << 1)

#define PITTCTRL_TEN	(0x1 << 0)
#define PITTCTRL_TIE	(0x1 << 1)
#define PITCTRL_CHN	(0x1 << 2)

#define PITTFLG_TIF	0x1

struct pit_timer {
	void __iomem *clksrc_base;
	void __iomem *clkevt_base;
	unsigned long cycle_per_jiffy;
	struct clock_event_device ced;
	struct clocksource cs;
};

static void __iomem *clksrc_base;

static inline struct pit_timer *ced_to_pit(struct clock_event_device *ced)
{
	return container_of(ced, struct pit_timer, ced);
}

static inline void pit_timer_enable(struct pit_timer *pit)
{
	writel(PITTCTRL_TEN | PITTCTRL_TIE, pit->clkevt_base + PITTCTRL);
}

static inline void pit_timer_disable(struct pit_timer *pit)
{
	writel(0, pit->clkevt_base + PITTCTRL);
}

static inline void pit_irq_acknowledge(struct pit_timer *pit)
{
	writel(PITTFLG_TIF, pit->clkevt_base + PITTFLG);
}

static u64 notrace pit_read_sched_clock(void)
{
	return ~readl(clksrc_base + PITCVAL);
}

static int __init pit_clocksource_init(struct pit_timer *pit, void __iomem *base,
				       unsigned long rate)
{
	/*
	 * The channels 0 and 1 can be chained to build a 64-bit
	 * timer. Let's use the channel 2 as a clocksource and leave
	 * the channels 0 and 1 unused for anyone else who needs them
	 */
	pit->clksrc_base = base + PIT_CH(2);

	/* set the max load value and start the clock source counter */
	writel(0, pit->clksrc_base + PITTCTRL);
	writel(~0, pit->clksrc_base + PITLDVAL);
	writel(PITTCTRL_TEN, pit->clksrc_base + PITTCTRL);

	clksrc_base = pit->clksrc_base;

	sched_clock_register(pit_read_sched_clock, 32, rate);

	return clocksource_mmio_init(pit->clksrc_base + PITCVAL, "vf-pit", rate,
				     300, 32, clocksource_mmio_readl_down);
}

static int pit_set_next_event(unsigned long delta, struct clock_event_device *ced)
{
	struct pit_timer *pit = ced_to_pit(ced);

	/*
	 * set a new value to PITLDVAL register will not restart the timer,
	 * to abort the current cycle and start a timer period with the new
	 * value, the timer must be disabled and enabled again.
	 * and the PITLAVAL should be set to delta minus one according to pit
	 * hardware requirement.
	 */
	pit_timer_disable(pit);
	writel(delta - 1, pit->clkevt_base + PITLDVAL);
	pit_timer_enable(pit);

	return 0;
}

static int pit_shutdown(struct clock_event_device *ced)
{
	struct pit_timer *pit = ced_to_pit(ced);

	pit_timer_disable(pit);

	return 0;
}

static int pit_set_periodic(struct clock_event_device *ced)
{
	struct pit_timer *pit = ced_to_pit(ced);

	pit_set_next_event(pit->cycle_per_jiffy, ced);

	return 0;
}

static irqreturn_t pit_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ced = dev_id;
	struct pit_timer *pit = ced_to_pit(ced);

	pit_irq_acknowledge(pit);

	/*
	 * pit hardware doesn't support oneshot, it will generate an interrupt
	 * and reload the counter value from PITLDVAL when PITCVAL reach zero,
	 * and start the counter again. So software need to disable the timer
	 * to stop the counter loop in ONESHOT mode.
	 */
	if (likely(clockevent_state_oneshot(ced)))
		pit_timer_disable(pit);

	ced->event_handler(ced);

	return IRQ_HANDLED;
}

static int __init pit_clockevent_init(struct pit_timer *pit, void __iomem *base,
				      unsigned long rate, int irq, unsigned int cpu)
{
	/*
	 * The channels 0 and 1 can be chained to build a 64-bit
	 * timer. Let's use the channel 3 as a clockevent and leave
	 * the channels 0 and 1 unused for anyone else who needs them
	 */
	pit->clkevt_base = base + PIT_CH(3);
	pit->cycle_per_jiffy = rate / (HZ);

	writel(0, pit->clkevt_base + PITTCTRL);

	writel(PITTFLG_TIF, pit->clkevt_base + PITTFLG);

	BUG_ON(request_irq(irq, pit_timer_interrupt, IRQF_TIMER | IRQF_IRQPOLL,
			   "VF pit timer", &pit->ced));

	pit->ced.cpumask = cpumask_of(cpu);
	pit->ced.irq = irq;

	pit->ced.name = "VF pit timer";
	pit->ced.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	pit->ced.set_state_shutdown = pit_shutdown;
	pit->ced.set_state_periodic = pit_set_periodic;
	pit->ced.set_next_event	= pit_set_next_event;
	pit->ced.rating	= 300;

	/*
	 * The value for the LDVAL register trigger is calculated as:
	 * LDVAL trigger = (period / clock period) - 1
	 * The pit is a 32-bit down count timer, when the counter value
	 * reaches 0, it will generate an interrupt, thus the minimal
	 * LDVAL trigger value is 1. And then the min_delta is
	 * minimal LDVAL trigger value + 1, and the max_delta is full 32-bit.
	 */
	clockevents_config_and_register(&pit->ced, rate, 2, 0xffffffff);

	return 0;
}

static int __init pit_timer_init(struct device_node *np)
{
	struct pit_timer *pit;
	struct clk *pit_clk;
	void __iomem *timer_base;
	unsigned long clk_rate;
	int irq, ret;

	pit = kzalloc(sizeof(*pit), GFP_KERNEL);
	if (!pit)
		return -ENOMEM;

	ret = -ENXIO;
	timer_base = of_iomap(np, 0);
	if (!timer_base) {
		pr_err("Failed to iomap\n");
		goto out_kfree;
	}

	ret = -EINVAL;
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Failed to irq_of_parse_and_map\n");
		goto out_iounmap;
	}

	pit_clk = of_clk_get(np, 0);
	if (IS_ERR(pit_clk)) {
		ret = PTR_ERR(pit_clk);
		goto out_iounmap;
	}

	ret = clk_prepare_enable(pit_clk);
	if (ret)
		goto out_clk_put;

	clk_rate = clk_get_rate(pit_clk);

	/* enable the pit module */
	writel(~PITMCR_MDIS, timer_base + PITMCR);

	ret = pit_clocksource_init(pit, timer_base, clk_rate);
	if (ret)
		goto out_disable_unprepare;

	ret = pit_clockevent_init(pit, timer_base, clk_rate, irq, 0);
	if (ret)
		goto out_pit_clocksource_unregister;

	return 0;

out_pit_clocksource_unregister:
	clocksource_unregister(&pit->cs);
out_disable_unprepare:
	clk_disable_unprepare(pit_clk);
out_clk_put:
	clk_put(pit_clk);
out_iounmap:
	iounmap(timer_base);
out_kfree:
	kfree(pit);

	return ret;
}
TIMER_OF_DECLARE(vf610, "fsl,vf610-pit", pit_timer_init);
