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

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>
#include "timer-of.h"

#define TIMER_CLK_EVT           (1)
#define TIMER_CLK_SRC           (2)

#define TIMER_SYNC_TICKS        (3)

/* gpt */
#define GPT_IRQ_EN_REG          0x00
#define GPT_IRQ_ENABLE(val)     BIT((val) - 1)
#define GPT_IRQ_ACK_REG	        0x08
#define GPT_IRQ_ACK(val)        BIT((val) - 1)

#define GPT_CTRL_REG(val)       (0x10 * (val))
#define GPT_CTRL_OP(val)        (((val) & 0x3) << 4)
#define GPT_CTRL_OP_ONESHOT     (0)
#define GPT_CTRL_OP_REPEAT      (1)
#define GPT_CTRL_OP_FREERUN     (3)
#define GPT_CTRL_CLEAR          (2)
#define GPT_CTRL_ENABLE         (1)
#define GPT_CTRL_DISABLE        (0)

#define GPT_CLK_REG(val)        (0x04 + (0x10 * (val)))
#define GPT_CLK_SRC(val)        (((val) & 0x1) << 4)
#define GPT_CLK_SRC_SYS13M      (0)
#define GPT_CLK_SRC_RTC32K      (1)
#define GPT_CLK_DIV1            (0x0)
#define GPT_CLK_DIV2            (0x1)

#define GPT_CNT_REG(val)        (0x08 + (0x10 * (val)))
#define GPT_CMP_REG(val)        (0x0C + (0x10 * (val)))

static void __iomem *gpt_sched_reg __read_mostly;

static u64 notrace mtk_gpt_read_sched_clock(void)
{
	return readl_relaxed(gpt_sched_reg);
}

static void mtk_gpt_clkevt_time_stop(struct timer_of *to, u8 timer)
{
	u32 val;

	val = readl(timer_of_base(to) + GPT_CTRL_REG(timer));
	writel(val & ~GPT_CTRL_ENABLE, timer_of_base(to) +
	       GPT_CTRL_REG(timer));
}

static void mtk_gpt_clkevt_time_setup(struct timer_of *to,
				      unsigned long delay, u8 timer)
{
	writel(delay, timer_of_base(to) + GPT_CMP_REG(timer));
}

static void mtk_gpt_clkevt_time_start(struct timer_of *to,
				      bool periodic, u8 timer)
{
	u32 val;

	/* Acknowledge interrupt */
	writel(GPT_IRQ_ACK(timer), timer_of_base(to) + GPT_IRQ_ACK_REG);

	val = readl(timer_of_base(to) + GPT_CTRL_REG(timer));

	/* Clear 2 bit timer operation mode field */
	val &= ~GPT_CTRL_OP(0x3);

	if (periodic)
		val |= GPT_CTRL_OP(GPT_CTRL_OP_REPEAT);
	else
		val |= GPT_CTRL_OP(GPT_CTRL_OP_ONESHOT);

	writel(val | GPT_CTRL_ENABLE | GPT_CTRL_CLEAR,
	       timer_of_base(to) + GPT_CTRL_REG(timer));
}

static int mtk_gpt_clkevt_shutdown(struct clock_event_device *clk)
{
	mtk_gpt_clkevt_time_stop(to_timer_of(clk), TIMER_CLK_EVT);

	return 0;
}

static int mtk_gpt_clkevt_set_periodic(struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	mtk_gpt_clkevt_time_stop(to, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_setup(to, to->of_clk.period, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_start(to, true, TIMER_CLK_EVT);

	return 0;
}

static int mtk_gpt_clkevt_next_event(unsigned long event,
				     struct clock_event_device *clk)
{
	struct timer_of *to = to_timer_of(clk);

	mtk_gpt_clkevt_time_stop(to, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_setup(to, event, TIMER_CLK_EVT);
	mtk_gpt_clkevt_time_start(to, false, TIMER_CLK_EVT);

	return 0;
}

static irqreturn_t mtk_gpt_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	/* Acknowledge timer0 irq */
	writel(GPT_IRQ_ACK(TIMER_CLK_EVT), timer_of_base(to) + GPT_IRQ_ACK_REG);
	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static void
__init mtk_gpt_setup(struct timer_of *to, u8 timer, u8 option)
{
	writel(GPT_CTRL_CLEAR | GPT_CTRL_DISABLE,
	       timer_of_base(to) + GPT_CTRL_REG(timer));

	writel(GPT_CLK_SRC(GPT_CLK_SRC_SYS13M) | GPT_CLK_DIV1,
	       timer_of_base(to) + GPT_CLK_REG(timer));

	writel(0x0, timer_of_base(to) + GPT_CMP_REG(timer));

	writel(GPT_CTRL_OP(option) | GPT_CTRL_ENABLE,
	       timer_of_base(to) + GPT_CTRL_REG(timer));
}

static void mtk_gpt_enable_irq(struct timer_of *to, u8 timer)
{
	u32 val;

	/* Disable all interrupts */
	writel(0x0, timer_of_base(to) + GPT_IRQ_EN_REG);

	/* Acknowledge all spurious pending interrupts */
	writel(0x3f, timer_of_base(to) + GPT_IRQ_ACK_REG);

	val = readl(timer_of_base(to) + GPT_IRQ_EN_REG);
	writel(val | GPT_IRQ_ENABLE(timer),
	       timer_of_base(to) + GPT_IRQ_EN_REG);
}

static struct timer_of to = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,

	.clkevt = {
		.name = "mtk-clkevt",
		.rating = 300,
		.cpumask = cpu_possible_mask,
	},

	.of_irq = {
		.flags = IRQF_TIMER | IRQF_IRQPOLL,
	},
};

static int __init mtk_gpt_init(struct device_node *node)
{
	int ret;

	to.clkevt.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	to.clkevt.set_state_shutdown = mtk_gpt_clkevt_shutdown;
	to.clkevt.set_state_periodic = mtk_gpt_clkevt_set_periodic;
	to.clkevt.set_state_oneshot = mtk_gpt_clkevt_shutdown;
	to.clkevt.tick_resume = mtk_gpt_clkevt_shutdown;
	to.clkevt.set_next_event = mtk_gpt_clkevt_next_event;
	to.of_irq.handler = mtk_gpt_interrupt;

	ret = timer_of_init(node, &to);
	if (ret)
		goto err;

	/* Configure clock source */
	mtk_gpt_setup(&to, TIMER_CLK_SRC, GPT_CTRL_OP_FREERUN);
	clocksource_mmio_init(timer_of_base(&to) + GPT_CNT_REG(TIMER_CLK_SRC),
			      node->name, timer_of_rate(&to), 300, 32,
			      clocksource_mmio_readl_up);
	gpt_sched_reg = timer_of_base(&to) + GPT_CNT_REG(TIMER_CLK_SRC);
	sched_clock_register(mtk_gpt_read_sched_clock, 32, timer_of_rate(&to));

	/* Configure clock event */
	mtk_gpt_setup(&to, TIMER_CLK_EVT, GPT_CTRL_OP_REPEAT);
	clockevents_config_and_register(&to.clkevt, timer_of_rate(&to),
					TIMER_SYNC_TICKS, 0xffffffff);

	mtk_gpt_enable_irq(&to, TIMER_CLK_EVT);

	return 0;

err:
	timer_of_cleanup(&to);
	return ret;
}
TIMER_OF_DECLARE(mtk_mt6577, "mediatek,mt6577-timer", mtk_gpt_init);
