/*
 * Mediatek SoCs General-Purpose Timer handling.
 *
 * Copyright (C) 2014 Matthias Brugger
 *
 * Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#define GPT_IRQ_EN_REG		0x00
#define GPT_IRQ_ENABLE(val)	BIT((val) - 1)
#define GPT_IRQ_ACK_REG		0x08
#define GPT_IRQ_ACK(val)	BIT((val) - 1)

#define TIMER_CTRL_REG(val)	(0x10 * (val))
#define TIMER_CTRL_OP(val)	(((val) & 0x3) << 4)
#define TIMER_CTRL_OP_ONESHOT	(0)
#define TIMER_CTRL_OP_REPEAT	(1)
#define TIMER_CTRL_OP_FREERUN	(3)
#define TIMER_CTRL_CLEAR	(2)
#define TIMER_CTRL_ENABLE	(1)
#define TIMER_CTRL_DISABLE	(0)

#define TIMER_CLK_REG(val)	(0x04 + (0x10 * (val)))
#define TIMER_CLK_SRC(val)	(((val) & 0x1) << 4)
#define TIMER_CLK_SRC_SYS13M	(0)
#define TIMER_CLK_SRC_RTC32K	(1)
#define TIMER_CLK_DIV1		(0x0)
#define TIMER_CLK_DIV2		(0x1)

#define TIMER_CNT_REG(val)	(0x08 + (0x10 * (val)))
#define TIMER_CMP_REG(val)	(0x0C + (0x10 * (val)))

#define GPT_CLK_EVT	1
#define GPT_CLK_SRC	2

struct mtk_clock_event_device {
	void __iomem *gpt_base;
	u32 ticks_per_jiffy;
	struct clock_event_device dev;
};

static inline struct mtk_clock_event_device *to_mtk_clk(
				struct clock_event_device *c)
{
	return container_of(c, struct mtk_clock_event_device, dev);
}

static void mtk_clkevt_time_stop(struct mtk_clock_event_device *evt, u8 timer)
{
	u32 val;

	val = readl(evt->gpt_base + TIMER_CTRL_REG(timer));
	writel(val & ~TIMER_CTRL_ENABLE, evt->gpt_base +
			TIMER_CTRL_REG(timer));
}

static void mtk_clkevt_time_setup(struct mtk_clock_event_device *evt,
				unsigned long delay, u8 timer)
{
	writel(delay, evt->gpt_base + TIMER_CMP_REG(timer));
}

static void mtk_clkevt_time_start(struct mtk_clock_event_device *evt,
		bool periodic, u8 timer)
{
	u32 val;

	/* Acknowledge interrupt */
	writel(GPT_IRQ_ACK(timer), evt->gpt_base + GPT_IRQ_ACK_REG);

	val = readl(evt->gpt_base + TIMER_CTRL_REG(timer));

	/* Clear 2 bit timer operation mode field */
	val &= ~TIMER_CTRL_OP(0x3);

	if (periodic)
		val |= TIMER_CTRL_OP(TIMER_CTRL_OP_REPEAT);
	else
		val |= TIMER_CTRL_OP(TIMER_CTRL_OP_ONESHOT);

	writel(val | TIMER_CTRL_ENABLE | TIMER_CTRL_CLEAR,
	       evt->gpt_base + TIMER_CTRL_REG(timer));
}

static void mtk_clkevt_mode(enum clock_event_mode mode,
				struct clock_event_device *clk)
{
	struct mtk_clock_event_device *evt = to_mtk_clk(clk);

	mtk_clkevt_time_stop(evt, GPT_CLK_EVT);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		mtk_clkevt_time_setup(evt, evt->ticks_per_jiffy, GPT_CLK_EVT);
		mtk_clkevt_time_start(evt, true, GPT_CLK_EVT);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Timer is enabled in set_next_event */
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		/* No more interrupts will occur as source is disabled */
		break;
	}
}

static int mtk_clkevt_next_event(unsigned long event,
				   struct clock_event_device *clk)
{
	struct mtk_clock_event_device *evt = to_mtk_clk(clk);

	mtk_clkevt_time_stop(evt, GPT_CLK_EVT);
	mtk_clkevt_time_setup(evt, event, GPT_CLK_EVT);
	mtk_clkevt_time_start(evt, false, GPT_CLK_EVT);

	return 0;
}

static irqreturn_t mtk_timer_interrupt(int irq, void *dev_id)
{
	struct mtk_clock_event_device *evt = dev_id;

	/* Acknowledge timer0 irq */
	writel(GPT_IRQ_ACK(GPT_CLK_EVT), evt->gpt_base + GPT_IRQ_ACK_REG);
	evt->dev.event_handler(&evt->dev);

	return IRQ_HANDLED;
}

static void mtk_timer_global_reset(struct mtk_clock_event_device *evt)
{
	/* Disable all interrupts */
	writel(0x0, evt->gpt_base + GPT_IRQ_EN_REG);
	/* Acknowledge all interrupts */
	writel(0x3f, evt->gpt_base + GPT_IRQ_ACK_REG);
}

static void
mtk_timer_setup(struct mtk_clock_event_device *evt, u8 timer, u8 option)
{
	writel(TIMER_CTRL_CLEAR | TIMER_CTRL_DISABLE,
		evt->gpt_base + TIMER_CTRL_REG(timer));

	writel(TIMER_CLK_SRC(TIMER_CLK_SRC_SYS13M) | TIMER_CLK_DIV1,
			evt->gpt_base + TIMER_CLK_REG(timer));

	writel(0x0, evt->gpt_base + TIMER_CMP_REG(timer));

	writel(TIMER_CTRL_OP(option) | TIMER_CTRL_ENABLE,
			evt->gpt_base + TIMER_CTRL_REG(timer));
}

static void mtk_timer_enable_irq(struct mtk_clock_event_device *evt, u8 timer)
{
	u32 val;

	val = readl(evt->gpt_base + GPT_IRQ_EN_REG);
	writel(val | GPT_IRQ_ENABLE(timer),
			evt->gpt_base + GPT_IRQ_EN_REG);
}

static void __init mtk_timer_init(struct device_node *node)
{
	struct mtk_clock_event_device *evt;
	struct resource res;
	unsigned long rate = 0;
	struct clk *clk;

	evt = kzalloc(sizeof(*evt), GFP_KERNEL);
	if (!evt) {
		pr_warn("Can't allocate mtk clock event driver struct");
		return;
	}

	evt->dev.name = "mtk_tick";
	evt->dev.rating = 300;
	evt->dev.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	evt->dev.set_mode = mtk_clkevt_mode;
	evt->dev.set_next_event = mtk_clkevt_next_event;
	evt->dev.cpumask = cpu_possible_mask;

	evt->gpt_base = of_io_request_and_map(node, 0, "mtk-timer");
	if (IS_ERR(evt->gpt_base)) {
		pr_warn("Can't get resource\n");
		return;
	}

	evt->dev.irq = irq_of_parse_and_map(node, 0);
	if (evt->dev.irq <= 0) {
		pr_warn("Can't parse IRQ");
		goto err_mem;
	}

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_warn("Can't get timer clock");
		goto err_irq;
	}

	if (clk_prepare_enable(clk)) {
		pr_warn("Can't prepare clock");
		goto err_clk_put;
	}
	rate = clk_get_rate(clk);

	if (request_irq(evt->dev.irq, mtk_timer_interrupt,
			IRQF_TIMER | IRQF_IRQPOLL, "mtk_timer", evt)) {
		pr_warn("failed to setup irq %d\n", evt->dev.irq);
		goto err_clk_disable;
	}

	evt->ticks_per_jiffy = DIV_ROUND_UP(rate, HZ);

	mtk_timer_global_reset(evt);

	/* Configure clock source */
	mtk_timer_setup(evt, GPT_CLK_SRC, TIMER_CTRL_OP_FREERUN);
	clocksource_mmio_init(evt->gpt_base + TIMER_CNT_REG(GPT_CLK_SRC),
			node->name, rate, 300, 32, clocksource_mmio_readl_up);

	/* Configure clock event */
	mtk_timer_setup(evt, GPT_CLK_EVT, TIMER_CTRL_OP_REPEAT);
	mtk_timer_enable_irq(evt, GPT_CLK_EVT);

	clockevents_config_and_register(&evt->dev, rate, 0x3,
					0xffffffff);
	return;

err_clk_disable:
	clk_disable_unprepare(clk);
err_clk_put:
	clk_put(clk);
err_irq:
	irq_dispose_mapping(evt->dev.irq);
err_mem:
	iounmap(evt->gpt_base);
	of_address_to_resource(node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
}
CLOCKSOURCE_OF_DECLARE(mtk_mt6577, "mediatek,mt6577-timer", mtk_timer_init);
