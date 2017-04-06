/*
 * MOXA ART SoCs timer handling.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/clocksource.h>
#include <linux/bitops.h>
#include <linux/slab.h>

#define TIMER1_BASE		0x00
#define TIMER2_BASE		0x10
#define TIMER3_BASE		0x20

#define REG_COUNT		0x0 /* writable */
#define REG_LOAD		0x4
#define REG_MATCH1		0x8
#define REG_MATCH2		0xC

#define TIMER_CR		0x30
#define TIMER_INTR_STATE	0x34
#define TIMER_INTR_MASK		0x38

/*
 * Moxart TIMER_CR flags:
 *
 * MOXART_CR_*_CLOCK	0: PCLK, 1: EXT1CLK
 * MOXART_CR_*_INT	overflow interrupt enable bit
 */
#define MOXART_CR_1_ENABLE	BIT(0)
#define MOXART_CR_1_CLOCK	BIT(1)
#define MOXART_CR_1_INT	BIT(2)
#define MOXART_CR_2_ENABLE	BIT(3)
#define MOXART_CR_2_CLOCK	BIT(4)
#define MOXART_CR_2_INT	BIT(5)
#define MOXART_CR_3_ENABLE	BIT(6)
#define MOXART_CR_3_CLOCK	BIT(7)
#define MOXART_CR_3_INT	BIT(8)
#define MOXART_CR_COUNT_UP	BIT(9)

#define MOXART_TIMER1_ENABLE	(MOXART_CR_2_ENABLE | MOXART_CR_1_ENABLE)
#define MOXART_TIMER1_DISABLE	(MOXART_CR_2_ENABLE)

/*
 * The ASpeed variant of the IP block has a different layout
 * for the control register
 */
#define ASPEED_CR_1_ENABLE	BIT(0)
#define ASPEED_CR_1_CLOCK	BIT(1)
#define ASPEED_CR_1_INT		BIT(2)
#define ASPEED_CR_2_ENABLE	BIT(4)
#define ASPEED_CR_2_CLOCK	BIT(5)
#define ASPEED_CR_2_INT		BIT(6)
#define ASPEED_CR_3_ENABLE	BIT(8)
#define ASPEED_CR_3_CLOCK	BIT(9)
#define ASPEED_CR_3_INT		BIT(10)

#define ASPEED_TIMER1_ENABLE   (ASPEED_CR_2_ENABLE | ASPEED_CR_1_ENABLE)
#define ASPEED_TIMER1_DISABLE  (ASPEED_CR_2_ENABLE)

struct moxart_timer {
	void __iomem *base;
	unsigned int t1_disable_val;
	unsigned int t1_enable_val;
	unsigned int count_per_tick;
	struct clock_event_device clkevt;
};

static inline struct moxart_timer *to_moxart(struct clock_event_device *evt)
{
	return container_of(evt, struct moxart_timer, clkevt);
}

static inline void moxart_disable(struct clock_event_device *evt)
{
	struct moxart_timer *timer = to_moxart(evt);

	writel(timer->t1_disable_val, timer->base + TIMER_CR);
}

static inline void moxart_enable(struct clock_event_device *evt)
{
	struct moxart_timer *timer = to_moxart(evt);

	writel(timer->t1_enable_val, timer->base + TIMER_CR);
}

static int moxart_shutdown(struct clock_event_device *evt)
{
	moxart_disable(evt);
	return 0;
}

static int moxart_set_oneshot(struct clock_event_device *evt)
{
	moxart_disable(evt);
	writel(~0, to_moxart(evt)->base + TIMER1_BASE + REG_LOAD);
	return 0;
}

static int moxart_set_periodic(struct clock_event_device *evt)
{
	struct moxart_timer *timer = to_moxart(evt);

	moxart_disable(evt);
	writel(timer->count_per_tick, timer->base + TIMER1_BASE + REG_LOAD);
	writel(0, timer->base + TIMER1_BASE + REG_MATCH1);
	moxart_enable(evt);
	return 0;
}

static int moxart_clkevt_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	struct moxart_timer *timer = to_moxart(evt);
	u32 u;

	moxart_disable(evt);

	u = readl(timer->base + TIMER1_BASE + REG_COUNT) - cycles;
	writel(u, timer->base + TIMER1_BASE + REG_MATCH1);

	moxart_enable(evt);

	return 0;
}

static irqreturn_t moxart_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static int __init moxart_timer_init(struct device_node *node)
{
	int ret, irq;
	unsigned long pclk;
	struct clk *clk;
	struct moxart_timer *timer;

	timer = kzalloc(sizeof(*timer), GFP_KERNEL);
	if (!timer)
		return -ENOMEM;

	timer->base = of_iomap(node, 0);
	if (!timer->base) {
		pr_err("%s: of_iomap failed\n", node->full_name);
		ret = -ENXIO;
		goto out_free;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		pr_err("%s: irq_of_parse_and_map failed\n", node->full_name);
		ret = -EINVAL;
		goto out_unmap;
	}

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk))  {
		pr_err("%s: of_clk_get failed\n", node->full_name);
		ret = PTR_ERR(clk);
		goto out_unmap;
	}

	pclk = clk_get_rate(clk);

	if (of_device_is_compatible(node, "moxa,moxart-timer")) {
		timer->t1_enable_val = MOXART_TIMER1_ENABLE;
		timer->t1_disable_val = MOXART_TIMER1_DISABLE;
	} else if (of_device_is_compatible(node, "aspeed,ast2400-timer")) {
		timer->t1_enable_val = ASPEED_TIMER1_ENABLE;
		timer->t1_disable_val = ASPEED_TIMER1_DISABLE;
	} else {
		pr_err("%s: unknown platform\n", node->full_name);
		ret = -EINVAL;
		goto out_unmap;
	}

	timer->count_per_tick = DIV_ROUND_CLOSEST(pclk, HZ);

	timer->clkevt.name = node->name;
	timer->clkevt.rating = 200;
	timer->clkevt.features = CLOCK_EVT_FEAT_PERIODIC |
					CLOCK_EVT_FEAT_ONESHOT;
	timer->clkevt.set_state_shutdown = moxart_shutdown;
	timer->clkevt.set_state_periodic = moxart_set_periodic;
	timer->clkevt.set_state_oneshot = moxart_set_oneshot;
	timer->clkevt.tick_resume = moxart_set_oneshot;
	timer->clkevt.set_next_event = moxart_clkevt_next_event;
	timer->clkevt.cpumask = cpumask_of(0);
	timer->clkevt.irq = irq;

	ret = clocksource_mmio_init(timer->base + TIMER2_BASE + REG_COUNT,
				    "moxart_timer", pclk, 200, 32,
				    clocksource_mmio_readl_down);
	if (ret) {
		pr_err("%s: clocksource_mmio_init failed\n", node->full_name);
		goto out_unmap;
	}

	ret = request_irq(irq, moxart_timer_interrupt, IRQF_TIMER,
			  node->name, &timer->clkevt);
	if (ret) {
		pr_err("%s: setup_irq failed\n", node->full_name);
		goto out_unmap;
	}

	/* Clear match registers */
	writel(0, timer->base + TIMER1_BASE + REG_MATCH1);
	writel(0, timer->base + TIMER1_BASE + REG_MATCH2);
	writel(0, timer->base + TIMER2_BASE + REG_MATCH1);
	writel(0, timer->base + TIMER2_BASE + REG_MATCH2);

	/*
	 * Start timer 2 rolling as our main wall clock source, keep timer 1
	 * disabled
	 */
	writel(0, timer->base + TIMER_CR);
	writel(~0, timer->base + TIMER2_BASE + REG_LOAD);
	writel(timer->t1_disable_val, timer->base + TIMER_CR);

	/*
	 * documentation is not publicly available:
	 * min_delta / max_delta obtained by trial-and-error,
	 * max_delta 0xfffffffe should be ok because count
	 * register size is u32
	 */
	clockevents_config_and_register(&timer->clkevt, pclk, 0x4, 0xfffffffe);

	return 0;

out_unmap:
	iounmap(timer->base);
out_free:
	kfree(timer);
	return ret;
}
CLOCKSOURCE_OF_DECLARE(moxart, "moxa,moxart-timer", moxart_timer_init);
CLOCKSOURCE_OF_DECLARE(aspeed, "aspeed,ast2400-timer", moxart_timer_init);
