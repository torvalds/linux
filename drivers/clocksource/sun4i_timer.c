/*
 * Allwinner A1X SoCs timer handling.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
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

#define TIMER_IRQ_EN_REG	0x00
#define TIMER_IRQ_EN(val)		(1 << val)
#define TIMER_IRQ_ST_REG	0x04
#define TIMER_CTL_REG(val)	(0x10 * val + 0x10)
#define TIMER_CTL_ENABLE		(1 << 0)
#define TIMER_CTL_AUTORELOAD		(1 << 1)
#define TIMER_CTL_ONESHOT		(1 << 7)
#define TIMER_INTVAL_REG(val)	(0x10 * val + 0x14)
#define TIMER_CNTVAL_REG(val)	(0x10 * val + 0x18)

#define TIMER_SCAL		16

static void __iomem *timer_base;

static void sun4i_clkevt_mode(enum clock_event_mode mode,
			      struct clock_event_device *clk)
{
	u32 u = readl(timer_base + TIMER_CTL_REG(0));

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		u &= ~(TIMER_CTL_ONESHOT);
		writel(u | TIMER_CTL_ENABLE, timer_base + TIMER_CTL_REG(0));
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		writel(u | TIMER_CTL_ONESHOT, timer_base + TIMER_CTL_REG(0));
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		writel(u & ~(TIMER_CTL_ENABLE), timer_base + TIMER_CTL_REG(0));
		break;
	}
}

static int sun4i_clkevt_next_event(unsigned long evt,
				   struct clock_event_device *unused)
{
	u32 u = readl(timer_base + TIMER_CTL_REG(0));
	writel(evt, timer_base + TIMER_CNTVAL_REG(0));
	writel(u | TIMER_CTL_ENABLE | TIMER_CTL_AUTORELOAD,
	       timer_base + TIMER_CTL_REG(0));

	return 0;
}

static struct clock_event_device sun4i_clockevent = {
	.name = "sun4i_tick",
	.rating = 300,
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = sun4i_clkevt_mode,
	.set_next_event = sun4i_clkevt_next_event,
};


static irqreturn_t sun4i_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;

	writel(0x1, timer_base + TIMER_IRQ_ST_REG);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction sun4i_timer_irq = {
	.name = "sun4i_timer0",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = sun4i_timer_interrupt,
	.dev_id = &sun4i_clockevent,
};

static void __init sun4i_timer_init(struct device_node *node)
{
	unsigned long rate = 0;
	struct clk *clk;
	int ret, irq;
	u32 val;

	timer_base = of_iomap(node, 0);
	if (!timer_base)
		panic("Can't map registers");

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0)
		panic("Can't parse IRQ");

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk))
		panic("Can't get timer clock");

	rate = clk_get_rate(clk);

	writel(rate / (TIMER_SCAL * HZ),
	       timer_base + TIMER_INTVAL_REG(0));

	/* set clock source to HOSC, 16 pre-division */
	val = readl(timer_base + TIMER_CTL_REG(0));
	val &= ~(0x07 << 4);
	val &= ~(0x03 << 2);
	val |= (4 << 4) | (1 << 2);
	writel(val, timer_base + TIMER_CTL_REG(0));

	/* set mode to auto reload */
	val = readl(timer_base + TIMER_CTL_REG(0));
	writel(val | TIMER_CTL_AUTORELOAD, timer_base + TIMER_CTL_REG(0));

	ret = setup_irq(irq, &sun4i_timer_irq);
	if (ret)
		pr_warn("failed to setup irq %d\n", irq);

	/* Enable timer0 interrupt */
	val = readl(timer_base + TIMER_IRQ_EN_REG);
	writel(val | TIMER_IRQ_EN(0), timer_base + TIMER_IRQ_EN_REG);

	sun4i_clockevent.cpumask = cpumask_of(0);

	clockevents_config_and_register(&sun4i_clockevent, rate / TIMER_SCAL,
					0x1, 0xff);
}
CLOCKSOURCE_OF_DECLARE(sun4i, "allwinner,sun4i-timer",
		       sun4i_timer_init);
