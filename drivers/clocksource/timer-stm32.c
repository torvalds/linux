/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Inspired by time-efm32.c from Uwe Kleine-Koenig
 */

#include <linux/kernel.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/reset.h>

#define TIM_CR1		0x00
#define TIM_DIER	0x0c
#define TIM_SR		0x10
#define TIM_EGR		0x14
#define TIM_PSC		0x28
#define TIM_ARR		0x2c

#define TIM_CR1_CEN	BIT(0)
#define TIM_CR1_OPM	BIT(3)
#define TIM_CR1_ARPE	BIT(7)

#define TIM_DIER_UIE	BIT(0)

#define TIM_SR_UIF	BIT(0)

#define TIM_EGR_UG	BIT(0)

struct stm32_clock_event_ddata {
	struct clock_event_device evtdev;
	unsigned periodic_top;
	void __iomem *base;
};

static int stm32_clock_event_shutdown(struct clock_event_device *evtdev)
{
	struct stm32_clock_event_ddata *data =
		container_of(evtdev, struct stm32_clock_event_ddata, evtdev);
	void *base = data->base;

	writel_relaxed(0, base + TIM_CR1);
	return 0;
}

static int stm32_clock_event_set_periodic(struct clock_event_device *evtdev)
{
	struct stm32_clock_event_ddata *data =
		container_of(evtdev, struct stm32_clock_event_ddata, evtdev);
	void *base = data->base;

	writel_relaxed(data->periodic_top, base + TIM_ARR);
	writel_relaxed(TIM_CR1_ARPE | TIM_CR1_CEN, base + TIM_CR1);
	return 0;
}

static int stm32_clock_event_set_next_event(unsigned long evt,
					    struct clock_event_device *evtdev)
{
	struct stm32_clock_event_ddata *data =
		container_of(evtdev, struct stm32_clock_event_ddata, evtdev);

	writel_relaxed(evt, data->base + TIM_ARR);
	writel_relaxed(TIM_CR1_ARPE | TIM_CR1_OPM | TIM_CR1_CEN,
		       data->base + TIM_CR1);

	return 0;
}

static irqreturn_t stm32_clock_event_handler(int irq, void *dev_id)
{
	struct stm32_clock_event_ddata *data = dev_id;

	writel_relaxed(0, data->base + TIM_SR);

	data->evtdev.event_handler(&data->evtdev);

	return IRQ_HANDLED;
}

static struct stm32_clock_event_ddata clock_event_ddata = {
	.evtdev = {
		.name = "stm32 clockevent",
		.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
		.set_state_shutdown = stm32_clock_event_shutdown,
		.set_state_periodic = stm32_clock_event_set_periodic,
		.set_state_oneshot = stm32_clock_event_shutdown,
		.tick_resume = stm32_clock_event_shutdown,
		.set_next_event = stm32_clock_event_set_next_event,
		.rating = 200,
	},
};

static int __init stm32_clockevent_init(struct device_node *np)
{
	struct stm32_clock_event_ddata *data = &clock_event_ddata;
	struct clk *clk;
	struct reset_control *rstc;
	unsigned long rate, max_delta;
	int irq, ret, bits, prescaler = 1;

	data = kmemdup(&clock_event_ddata, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

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

	rstc = of_reset_control_get(np, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	data->base = of_iomap(np, 0);
	if (!data->base) {
		ret = -ENXIO;
		pr_err("failed to map registers for clockevent\n");
		goto err_iomap;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		ret = -EINVAL;
		pr_err("%pOF: failed to get irq.\n", np);
		goto err_get_irq;
	}

	/* Detect whether the timer is 16 or 32 bits */
	writel_relaxed(~0U, data->base + TIM_ARR);
	max_delta = readl_relaxed(data->base + TIM_ARR);
	if (max_delta == ~0U) {
		prescaler = 1;
		bits = 32;
	} else {
		prescaler = 1024;
		bits = 16;
	}
	writel_relaxed(0, data->base + TIM_ARR);

	writel_relaxed(prescaler - 1, data->base + TIM_PSC);
	writel_relaxed(TIM_EGR_UG, data->base + TIM_EGR);
	writel_relaxed(0, data->base + TIM_SR);
	writel_relaxed(TIM_DIER_UIE, data->base + TIM_DIER);

	data->periodic_top = DIV_ROUND_CLOSEST(rate, prescaler * HZ);

	clockevents_config_and_register(&data->evtdev,
					DIV_ROUND_CLOSEST(rate, prescaler),
					0x1, max_delta);

	ret = request_irq(irq, stm32_clock_event_handler, IRQF_TIMER,
			"stm32 clockevent", data);
	if (ret) {
		pr_err("%pOF: failed to request irq.\n", np);
		goto err_get_irq;
	}

	pr_info("%pOF: STM32 clockevent driver initialized (%d bits)\n",
			np, bits);

	return ret;

err_get_irq:
	iounmap(data->base);
err_iomap:
	clk_disable_unprepare(clk);
err_clk_enable:
	clk_put(clk);
err_clk_get:
	kfree(data);
	return ret;
}

TIMER_OF_DECLARE(stm32, "st,stm32-timer", stm32_clockevent_init);
