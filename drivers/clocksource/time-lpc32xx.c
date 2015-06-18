/*
 * Clocksource driver for NXP LPC32xx/18xx/43xx timer
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * Based on:
 * time-efm32 Copyright (C) 2013 Pengutronix
 * mach-lpc32xx/timer.c Copyright (C) 2009 - 2010 NXP Semiconductors
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#define LPC32XX_TIMER_IR		0x000
#define  LPC32XX_TIMER_IR_MR0INT	BIT(0)
#define LPC32XX_TIMER_TCR		0x004
#define  LPC32XX_TIMER_TCR_CEN		BIT(0)
#define  LPC32XX_TIMER_TCR_CRST		BIT(1)
#define LPC32XX_TIMER_TC		0x008
#define LPC32XX_TIMER_PR		0x00c
#define LPC32XX_TIMER_MCR		0x014
#define  LPC32XX_TIMER_MCR_MR0I		BIT(0)
#define  LPC32XX_TIMER_MCR_MR0R		BIT(1)
#define  LPC32XX_TIMER_MCR_MR0S		BIT(2)
#define LPC32XX_TIMER_MR0		0x018
#define LPC32XX_TIMER_CTCR		0x070

struct lpc32xx_clock_event_ddata {
	struct clock_event_device evtdev;
	void __iomem *base;
};

/* Needed for the sched clock */
static void __iomem *clocksource_timer_counter;

static u64 notrace lpc32xx_read_sched_clock(void)
{
	return readl(clocksource_timer_counter);
}

static int lpc32xx_clkevt_next_event(unsigned long delta,
				     struct clock_event_device *evtdev)
{
	struct lpc32xx_clock_event_ddata *ddata =
		container_of(evtdev, struct lpc32xx_clock_event_ddata, evtdev);

	/*
	 * Place timer in reset and program the delta in the prescale
	 * register (PR). When the prescale counter matches the value
	 * in PR the counter register is incremented and the compare
	 * match will trigger. After setup the timer is released from
	 * reset and enabled.
	 */
	writel_relaxed(LPC32XX_TIMER_TCR_CRST, ddata->base + LPC32XX_TIMER_TCR);
	writel_relaxed(delta, ddata->base + LPC32XX_TIMER_PR);
	writel_relaxed(LPC32XX_TIMER_TCR_CEN, ddata->base + LPC32XX_TIMER_TCR);

	return 0;
}

static int lpc32xx_clkevt_shutdown(struct clock_event_device *evtdev)
{
	struct lpc32xx_clock_event_ddata *ddata =
		container_of(evtdev, struct lpc32xx_clock_event_ddata, evtdev);

	/* Disable the timer */
	writel_relaxed(0, ddata->base + LPC32XX_TIMER_TCR);

	return 0;
}

static int lpc32xx_clkevt_oneshot(struct clock_event_device *evtdev)
{
	/*
	 * When using oneshot, we must also disable the timer
	 * to wait for the first call to set_next_event().
	 */
	return lpc32xx_clkevt_shutdown(evtdev);
}

static irqreturn_t lpc32xx_clock_event_handler(int irq, void *dev_id)
{
	struct lpc32xx_clock_event_ddata *ddata = dev_id;

	/* Clear match on channel 0 */
	writel_relaxed(LPC32XX_TIMER_IR_MR0INT, ddata->base + LPC32XX_TIMER_IR);

	ddata->evtdev.event_handler(&ddata->evtdev);

	return IRQ_HANDLED;
}

static struct lpc32xx_clock_event_ddata lpc32xx_clk_event_ddata = {
	.evtdev = {
		.name			= "lpc3220 clockevent",
		.features		= CLOCK_EVT_FEAT_ONESHOT,
		.rating			= 300,
		.set_next_event		= lpc32xx_clkevt_next_event,
		.set_state_shutdown	= lpc32xx_clkevt_shutdown,
		.set_state_oneshot	= lpc32xx_clkevt_oneshot,
	},
};

static int __init lpc32xx_clocksource_init(struct device_node *np)
{
	void __iomem *base;
	unsigned long rate;
	struct clk *clk;
	int ret;

	clk = of_clk_get_by_name(np, "timerclk");
	if (IS_ERR(clk)) {
		pr_err("clock get failed (%lu)\n", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("clock enable failed (%d)\n", ret);
		goto err_clk_enable;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("unable to map registers\n");
		ret = -EADDRNOTAVAIL;
		goto err_iomap;
	}

	/*
	 * Disable and reset timer then set it to free running timer
	 * mode (CTCR) with no prescaler (PR) or match operations (MCR).
	 * After setup the timer is released from reset and enabled.
	 */
	writel_relaxed(LPC32XX_TIMER_TCR_CRST, base + LPC32XX_TIMER_TCR);
	writel_relaxed(0, base + LPC32XX_TIMER_PR);
	writel_relaxed(0, base + LPC32XX_TIMER_MCR);
	writel_relaxed(0, base + LPC32XX_TIMER_CTCR);
	writel_relaxed(LPC32XX_TIMER_TCR_CEN, base + LPC32XX_TIMER_TCR);

	rate = clk_get_rate(clk);
	ret = clocksource_mmio_init(base + LPC32XX_TIMER_TC, "lpc3220 timer",
				    rate, 300, 32, clocksource_mmio_readl_up);
	if (ret) {
		pr_err("failed to init clocksource (%d)\n", ret);
		goto err_clocksource_init;
	}

	clocksource_timer_counter = base + LPC32XX_TIMER_TC;
	sched_clock_register(lpc32xx_read_sched_clock, 32, rate);

	return 0;

err_clocksource_init:
	iounmap(base);
err_iomap:
	clk_disable_unprepare(clk);
err_clk_enable:
	clk_put(clk);
	return ret;
}

static int __init lpc32xx_clockevent_init(struct device_node *np)
{
	void __iomem *base;
	unsigned long rate;
	struct clk *clk;
	int ret, irq;

	clk = of_clk_get_by_name(np, "timerclk");
	if (IS_ERR(clk)) {
		pr_err("clock get failed (%lu)\n", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("clock enable failed (%d)\n", ret);
		goto err_clk_enable;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("unable to map registers\n");
		ret = -EADDRNOTAVAIL;
		goto err_iomap;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		pr_err("get irq failed\n");
		ret = -ENOENT;
		goto err_irq;
	}

	/*
	 * Disable timer and clear any pending interrupt (IR) on match
	 * channel 0 (MR0). Configure a compare match value of 1 on MR0
	 * and enable interrupt, reset on match and stop on match (MCR).
	 */
	writel_relaxed(0, base + LPC32XX_TIMER_TCR);
	writel_relaxed(0, base + LPC32XX_TIMER_CTCR);
	writel_relaxed(LPC32XX_TIMER_IR_MR0INT, base + LPC32XX_TIMER_IR);
	writel_relaxed(1, base + LPC32XX_TIMER_MR0);
	writel_relaxed(LPC32XX_TIMER_MCR_MR0I | LPC32XX_TIMER_MCR_MR0R |
		       LPC32XX_TIMER_MCR_MR0S, base + LPC32XX_TIMER_MCR);

	rate = clk_get_rate(clk);
	lpc32xx_clk_event_ddata.base = base;
	clockevents_config_and_register(&lpc32xx_clk_event_ddata.evtdev,
					rate, 1, -1);

	ret = request_irq(irq, lpc32xx_clock_event_handler,
			  IRQF_TIMER | IRQF_IRQPOLL, "lpc3220 clockevent",
			  &lpc32xx_clk_event_ddata);
	if (ret) {
		pr_err("request irq failed\n");
		goto err_irq;
	}

	return 0;

err_irq:
	iounmap(base);
err_iomap:
	clk_disable_unprepare(clk);
err_clk_enable:
	clk_put(clk);
	return ret;
}

/*
 * This function asserts that we have exactly one clocksource and one
 * clock_event_device in the end.
 */
static void __init lpc32xx_timer_init(struct device_node *np)
{
	static int has_clocksource, has_clockevent;
	int ret;

	if (!has_clocksource) {
		ret = lpc32xx_clocksource_init(np);
		if (!ret) {
			has_clocksource = 1;
			return;
		}
	}

	if (!has_clockevent) {
		ret = lpc32xx_clockevent_init(np);
		if (!ret) {
			has_clockevent = 1;
			return;
		}
	}
}
CLOCKSOURCE_OF_DECLARE(lpc32xx_timer, "nxp,lpc3220-timer", lpc32xx_timer_init);
