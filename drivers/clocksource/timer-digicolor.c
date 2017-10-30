/*
 * Conexant Digicolor timer driver
 *
 * Author: Baruch Siach <baruch@tkos.co.il>
 *
 * Copyright (C) 2014 Paradox Innovation Ltd.
 *
 * Based on:
 *	Allwinner SoCs hstimer driver
 *
 * Copyright (C) 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/*
 * Conexant Digicolor SoCs have 8 configurable timers, named from "Timer A" to
 * "Timer H". Timer A is the only one with watchdog support, so it is dedicated
 * to the watchdog driver. This driver uses Timer B for sched_clock(), and
 * Timer C for clockevents.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/sched/clock.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

enum {
	TIMER_A,
	TIMER_B,
	TIMER_C,
	TIMER_D,
	TIMER_E,
	TIMER_F,
	TIMER_G,
	TIMER_H,
};

#define CONTROL(t)	((t)*8)
#define COUNT(t)	((t)*8 + 4)

#define CONTROL_DISABLE		0
#define CONTROL_ENABLE		BIT(0)
#define CONTROL_MODE(m)		((m) << 4)
#define CONTROL_MODE_ONESHOT	CONTROL_MODE(1)
#define CONTROL_MODE_PERIODIC	CONTROL_MODE(2)

struct digicolor_timer {
	struct clock_event_device ce;
	void __iomem *base;
	u32 ticks_per_jiffy;
	int timer_id; /* one of TIMER_* */
};

static struct digicolor_timer *dc_timer(struct clock_event_device *ce)
{
	return container_of(ce, struct digicolor_timer, ce);
}

static inline void dc_timer_disable(struct clock_event_device *ce)
{
	struct digicolor_timer *dt = dc_timer(ce);
	writeb(CONTROL_DISABLE, dt->base + CONTROL(dt->timer_id));
}

static inline void dc_timer_enable(struct clock_event_device *ce, u32 mode)
{
	struct digicolor_timer *dt = dc_timer(ce);
	writeb(CONTROL_ENABLE | mode, dt->base + CONTROL(dt->timer_id));
}

static inline void dc_timer_set_count(struct clock_event_device *ce,
				      unsigned long count)
{
	struct digicolor_timer *dt = dc_timer(ce);
	writel(count, dt->base + COUNT(dt->timer_id));
}

static int digicolor_clkevt_shutdown(struct clock_event_device *ce)
{
	dc_timer_disable(ce);
	return 0;
}

static int digicolor_clkevt_set_oneshot(struct clock_event_device *ce)
{
	dc_timer_disable(ce);
	dc_timer_enable(ce, CONTROL_MODE_ONESHOT);
	return 0;
}

static int digicolor_clkevt_set_periodic(struct clock_event_device *ce)
{
	struct digicolor_timer *dt = dc_timer(ce);

	dc_timer_disable(ce);
	dc_timer_set_count(ce, dt->ticks_per_jiffy);
	dc_timer_enable(ce, CONTROL_MODE_PERIODIC);
	return 0;
}

static int digicolor_clkevt_next_event(unsigned long evt,
				       struct clock_event_device *ce)
{
	dc_timer_disable(ce);
	dc_timer_set_count(ce, evt);
	dc_timer_enable(ce, CONTROL_MODE_ONESHOT);

	return 0;
}

static struct digicolor_timer dc_timer_dev = {
	.ce = {
		.name = "digicolor_tick",
		.rating = 340,
		.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
		.set_state_shutdown = digicolor_clkevt_shutdown,
		.set_state_periodic = digicolor_clkevt_set_periodic,
		.set_state_oneshot = digicolor_clkevt_set_oneshot,
		.tick_resume = digicolor_clkevt_shutdown,
		.set_next_event = digicolor_clkevt_next_event,
	},
	.timer_id = TIMER_C,
};

static irqreturn_t digicolor_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static u64 notrace digicolor_timer_sched_read(void)
{
	return ~readl(dc_timer_dev.base + COUNT(TIMER_B));
}

static int __init digicolor_timer_init(struct device_node *node)
{
	unsigned long rate;
	struct clk *clk;
	int ret, irq;

	/*
	 * timer registers are shared with the watchdog timer;
	 * don't map exclusively
	 */
	dc_timer_dev.base = of_iomap(node, 0);
	if (!dc_timer_dev.base) {
		pr_err("Can't map registers\n");
		return -ENXIO;
	}

	irq = irq_of_parse_and_map(node, dc_timer_dev.timer_id);
	if (irq <= 0) {
		pr_err("Can't parse IRQ\n");
		return -EINVAL;
	}

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("Can't get timer clock\n");
		return PTR_ERR(clk);
	}
	clk_prepare_enable(clk);
	rate = clk_get_rate(clk);
	dc_timer_dev.ticks_per_jiffy = DIV_ROUND_UP(rate, HZ);

	writeb(CONTROL_DISABLE, dc_timer_dev.base + CONTROL(TIMER_B));
	writel(UINT_MAX, dc_timer_dev.base + COUNT(TIMER_B));
	writeb(CONTROL_ENABLE, dc_timer_dev.base + CONTROL(TIMER_B));

	sched_clock_register(digicolor_timer_sched_read, 32, rate);
	clocksource_mmio_init(dc_timer_dev.base + COUNT(TIMER_B), node->name,
			      rate, 340, 32, clocksource_mmio_readl_down);

	ret = request_irq(irq, digicolor_timer_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL, "digicolor_timerC",
			  &dc_timer_dev.ce);
	if (ret) {
		pr_warn("request of timer irq %d failed (%d)\n", irq, ret);
		return ret;
	}

	dc_timer_dev.ce.cpumask = cpu_possible_mask;
	dc_timer_dev.ce.irq = irq;

	clockevents_config_and_register(&dc_timer_dev.ce, rate, 0, 0xffffffff);

	return 0;
}
TIMER_OF_DECLARE(conexant_digicolor, "cnxt,cx92755-timer",
		       digicolor_timer_init);
