/*
 * Copyright (C) 2013 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>

#define TIMERn_CTRL			0x00
#define TIMERn_CTRL_PRESC(val)			(((val) & 0xf) << 24)
#define TIMERn_CTRL_PRESC_1024			TIMERn_CTRL_PRESC(10)
#define TIMERn_CTRL_CLKSEL(val)			(((val) & 0x3) << 16)
#define TIMERn_CTRL_CLKSEL_PRESCHFPERCLK	TIMERn_CTRL_CLKSEL(0)
#define TIMERn_CTRL_OSMEN			0x00000010
#define TIMERn_CTRL_MODE(val)			(((val) & 0x3) <<  0)
#define TIMERn_CTRL_MODE_UP			TIMERn_CTRL_MODE(0)
#define TIMERn_CTRL_MODE_DOWN			TIMERn_CTRL_MODE(1)

#define TIMERn_CMD			0x04
#define TIMERn_CMD_START			0x00000001
#define TIMERn_CMD_STOP				0x00000002

#define TIMERn_IEN			0x0c
#define TIMERn_IF			0x10
#define TIMERn_IFS			0x14
#define TIMERn_IFC			0x18
#define TIMERn_IRQ_UF				0x00000002

#define TIMERn_TOP			0x1c
#define TIMERn_CNT			0x24

struct efm32_clock_event_ddata {
	struct clock_event_device evtdev;
	void __iomem *base;
	unsigned periodic_top;
};

static void efm32_clock_event_set_mode(enum clock_event_mode mode,
				       struct clock_event_device *evtdev)
{
	struct efm32_clock_event_ddata *ddata =
		container_of(evtdev, struct efm32_clock_event_ddata, evtdev);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		writel_relaxed(TIMERn_CMD_STOP, ddata->base + TIMERn_CMD);
		writel_relaxed(ddata->periodic_top, ddata->base + TIMERn_TOP);
		writel_relaxed(TIMERn_CTRL_PRESC_1024 |
			       TIMERn_CTRL_CLKSEL_PRESCHFPERCLK |
			       TIMERn_CTRL_MODE_DOWN,
			       ddata->base + TIMERn_CTRL);
		writel_relaxed(TIMERn_CMD_START, ddata->base + TIMERn_CMD);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		writel_relaxed(TIMERn_CMD_STOP, ddata->base + TIMERn_CMD);
		writel_relaxed(TIMERn_CTRL_PRESC_1024 |
			       TIMERn_CTRL_CLKSEL_PRESCHFPERCLK |
			       TIMERn_CTRL_OSMEN |
			       TIMERn_CTRL_MODE_DOWN,
			       ddata->base + TIMERn_CTRL);
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		writel_relaxed(TIMERn_CMD_STOP, ddata->base + TIMERn_CMD);
		break;

	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int efm32_clock_event_set_next_event(unsigned long evt,
					    struct clock_event_device *evtdev)
{
	struct efm32_clock_event_ddata *ddata =
		container_of(evtdev, struct efm32_clock_event_ddata, evtdev);

	writel_relaxed(TIMERn_CMD_STOP, ddata->base + TIMERn_CMD);
	writel_relaxed(evt, ddata->base + TIMERn_CNT);
	writel_relaxed(TIMERn_CMD_START, ddata->base + TIMERn_CMD);

	return 0;
}

static irqreturn_t efm32_clock_event_handler(int irq, void *dev_id)
{
	struct efm32_clock_event_ddata *ddata = dev_id;

	writel_relaxed(TIMERn_IRQ_UF, ddata->base + TIMERn_IFC);

	ddata->evtdev.event_handler(&ddata->evtdev);

	return IRQ_HANDLED;
}

static struct efm32_clock_event_ddata clock_event_ddata = {
	.evtdev = {
		.name = "efm32 clockevent",
		.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_MODE_PERIODIC,
		.set_mode = efm32_clock_event_set_mode,
		.set_next_event = efm32_clock_event_set_next_event,
		.rating = 200,
	},
};

static struct irqaction efm32_clock_event_irq = {
	.name = "efm32 clockevent",
	.flags = IRQF_TIMER,
	.handler = efm32_clock_event_handler,
	.dev_id = &clock_event_ddata,
};

static int __init efm32_clocksource_init(struct device_node *np)
{
	struct clk *clk;
	void __iomem *base;
	unsigned long rate;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		pr_err("failed to get clock for clocksource (%d)\n", ret);
		goto err_clk_get;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("failed to enable timer clock for clocksource (%d)\n",
		       ret);
		goto err_clk_enable;
	}
	rate = clk_get_rate(clk);

	base = of_iomap(np, 0);
	if (!base) {
		ret = -EADDRNOTAVAIL;
		pr_err("failed to map registers for clocksource\n");
		goto err_iomap;
	}

	writel_relaxed(TIMERn_CTRL_PRESC_1024 |
		       TIMERn_CTRL_CLKSEL_PRESCHFPERCLK |
		       TIMERn_CTRL_MODE_UP, base + TIMERn_CTRL);
	writel_relaxed(TIMERn_CMD_START, base + TIMERn_CMD);

	ret = clocksource_mmio_init(base + TIMERn_CNT, "efm32 timer",
				    DIV_ROUND_CLOSEST(rate, 1024), 200, 16,
				    clocksource_mmio_readl_up);
	if (ret) {
		pr_err("failed to init clocksource (%d)\n", ret);
		goto err_clocksource_init;
	}

	return 0;

err_clocksource_init:

	iounmap(base);
err_iomap:

	clk_disable_unprepare(clk);
err_clk_enable:

	clk_put(clk);
err_clk_get:

	return ret;
}

static int __init efm32_clockevent_init(struct device_node *np)
{
	struct clk *clk;
	void __iomem *base;
	unsigned long rate;
	int irq;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		pr_err("failed to get clock for clockevent (%d)\n", ret);
		goto err_clk_get;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("failed to enable timer clock for clockevent (%d)\n",
		       ret);
		goto err_clk_enable;
	}
	rate = clk_get_rate(clk);

	base = of_iomap(np, 0);
	if (!base) {
		ret = -EADDRNOTAVAIL;
		pr_err("failed to map registers for clockevent\n");
		goto err_iomap;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		ret = -ENOENT;
		pr_err("failed to get irq for clockevent\n");
		goto err_get_irq;
	}

	writel_relaxed(TIMERn_IRQ_UF, base + TIMERn_IEN);

	clock_event_ddata.base = base;
	clock_event_ddata.periodic_top = DIV_ROUND_CLOSEST(rate, 1024 * HZ);

	setup_irq(irq, &efm32_clock_event_irq);

	clockevents_config_and_register(&clock_event_ddata.evtdev,
					DIV_ROUND_CLOSEST(rate, 1024),
					0xf, 0xffff);

	return 0;

err_get_irq:

	iounmap(base);
err_iomap:

	clk_disable_unprepare(clk);
err_clk_enable:

	clk_put(clk);
err_clk_get:

	return ret;
}

/*
 * This function asserts that we have exactly one clocksource and one
 * clock_event_device in the end.
 */
static void __init efm32_timer_init(struct device_node *np)
{
	static int has_clocksource, has_clockevent;
	int ret;

	if (!has_clocksource) {
		ret = efm32_clocksource_init(np);
		if (!ret) {
			has_clocksource = 1;
			return;
		}
	}

	if (!has_clockevent) {
		ret = efm32_clockevent_init(np);
		if (!ret) {
			has_clockevent = 1;
			return;
		}
	}
}
CLOCKSOURCE_OF_DECLARE(efm32, "efm32,timer", efm32_timer_init);
