/*
 * Allwinner SoCs hstimer driver.
 *
 * Copyright (C) 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define TIMER_IRQ_EN_REG		0x00
#define TIMER_IRQ_EN(val)			BIT(val)
#define TIMER_IRQ_ST_REG		0x04
#define TIMER_CTL_REG(val)		(0x20 * (val) + 0x10)
#define TIMER_CTL_ENABLE			BIT(0)
#define TIMER_CTL_RELOAD			BIT(1)
#define TIMER_CTL_CLK_PRES(val)			(((val) & 0x7) << 4)
#define TIMER_CTL_ONESHOT			BIT(7)
#define TIMER_INTVAL_LO_REG(val)	(0x20 * (val) + 0x14)
#define TIMER_INTVAL_HI_REG(val)	(0x20 * (val) + 0x18)
#define TIMER_CNTVAL_LO_REG(val)	(0x20 * (val) + 0x1c)
#define TIMER_CNTVAL_HI_REG(val)	(0x20 * (val) + 0x20)

#define TIMER_SYNC_TICKS	3

struct sun5i_timer {
	void __iomem		*base;
	struct clk		*clk;
	struct notifier_block	clk_rate_cb;
	u32			ticks_per_jiffy;
};

#define to_sun5i_timer(x) \
	container_of(x, struct sun5i_timer, clk_rate_cb)

struct sun5i_timer_clksrc {
	struct sun5i_timer	timer;
	struct clocksource	clksrc;
};

#define to_sun5i_timer_clksrc(x) \
	container_of(x, struct sun5i_timer_clksrc, clksrc)

struct sun5i_timer_clkevt {
	struct sun5i_timer		timer;
	struct clock_event_device	clkevt;
};

#define to_sun5i_timer_clkevt(x) \
	container_of(x, struct sun5i_timer_clkevt, clkevt)

/*
 * When we disable a timer, we need to wait at least for 2 cycles of
 * the timer source clock. We will use for that the clocksource timer
 * that is already setup and runs at the same frequency than the other
 * timers, and we never will be disabled.
 */
static void sun5i_clkevt_sync(struct sun5i_timer_clkevt *ce)
{
	u32 old = readl(ce->timer.base + TIMER_CNTVAL_LO_REG(1));

	while ((old - readl(ce->timer.base + TIMER_CNTVAL_LO_REG(1))) < TIMER_SYNC_TICKS)
		cpu_relax();
}

static void sun5i_clkevt_time_stop(struct sun5i_timer_clkevt *ce, u8 timer)
{
	u32 val = readl(ce->timer.base + TIMER_CTL_REG(timer));
	writel(val & ~TIMER_CTL_ENABLE, ce->timer.base + TIMER_CTL_REG(timer));

	sun5i_clkevt_sync(ce);
}

static void sun5i_clkevt_time_setup(struct sun5i_timer_clkevt *ce, u8 timer, u32 delay)
{
	writel(delay, ce->timer.base + TIMER_INTVAL_LO_REG(timer));
}

static void sun5i_clkevt_time_start(struct sun5i_timer_clkevt *ce, u8 timer, bool periodic)
{
	u32 val = readl(ce->timer.base + TIMER_CTL_REG(timer));

	if (periodic)
		val &= ~TIMER_CTL_ONESHOT;
	else
		val |= TIMER_CTL_ONESHOT;

	writel(val | TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       ce->timer.base + TIMER_CTL_REG(timer));
}

static int sun5i_clkevt_shutdown(struct clock_event_device *clkevt)
{
	struct sun5i_timer_clkevt *ce = to_sun5i_timer_clkevt(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	return 0;
}

static int sun5i_clkevt_set_oneshot(struct clock_event_device *clkevt)
{
	struct sun5i_timer_clkevt *ce = to_sun5i_timer_clkevt(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	sun5i_clkevt_time_start(ce, 0, false);
	return 0;
}

static int sun5i_clkevt_set_periodic(struct clock_event_device *clkevt)
{
	struct sun5i_timer_clkevt *ce = to_sun5i_timer_clkevt(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	sun5i_clkevt_time_setup(ce, 0, ce->timer.ticks_per_jiffy);
	sun5i_clkevt_time_start(ce, 0, true);
	return 0;
}

static int sun5i_clkevt_next_event(unsigned long evt,
				   struct clock_event_device *clkevt)
{
	struct sun5i_timer_clkevt *ce = to_sun5i_timer_clkevt(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	sun5i_clkevt_time_setup(ce, 0, evt - TIMER_SYNC_TICKS);
	sun5i_clkevt_time_start(ce, 0, false);

	return 0;
}

static irqreturn_t sun5i_timer_interrupt(int irq, void *dev_id)
{
	struct sun5i_timer_clkevt *ce = (struct sun5i_timer_clkevt *)dev_id;

	writel(0x1, ce->timer.base + TIMER_IRQ_ST_REG);
	ce->clkevt.event_handler(&ce->clkevt);

	return IRQ_HANDLED;
}

static u64 sun5i_clksrc_read(struct clocksource *clksrc)
{
	struct sun5i_timer_clksrc *cs = to_sun5i_timer_clksrc(clksrc);

	return ~readl(cs->timer.base + TIMER_CNTVAL_LO_REG(1));
}

static int sun5i_rate_cb_clksrc(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct sun5i_timer *timer = to_sun5i_timer(nb);
	struct sun5i_timer_clksrc *cs = container_of(timer, struct sun5i_timer_clksrc, timer);

	switch (event) {
	case PRE_RATE_CHANGE:
		clocksource_unregister(&cs->clksrc);
		break;

	case POST_RATE_CHANGE:
		clocksource_register_hz(&cs->clksrc, ndata->new_rate);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static int __init sun5i_setup_clocksource(struct device_node *node,
					  void __iomem *base,
					  struct clk *clk, int irq)
{
	struct sun5i_timer_clksrc *cs;
	unsigned long rate;
	int ret;

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("Couldn't enable parent clock\n");
		goto err_free;
	}

	rate = clk_get_rate(clk);
	if (!rate) {
		pr_err("Couldn't get parent clock rate\n");
		ret = -EINVAL;
		goto err_disable_clk;
	}

	cs->timer.base = base;
	cs->timer.clk = clk;
	cs->timer.clk_rate_cb.notifier_call = sun5i_rate_cb_clksrc;
	cs->timer.clk_rate_cb.next = NULL;

	ret = clk_notifier_register(clk, &cs->timer.clk_rate_cb);
	if (ret) {
		pr_err("Unable to register clock notifier.\n");
		goto err_disable_clk;
	}

	writel(~0, base + TIMER_INTVAL_LO_REG(1));
	writel(TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       base + TIMER_CTL_REG(1));

	cs->clksrc.name = node->name;
	cs->clksrc.rating = 340;
	cs->clksrc.read = sun5i_clksrc_read;
	cs->clksrc.mask = CLOCKSOURCE_MASK(32);
	cs->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	ret = clocksource_register_hz(&cs->clksrc, rate);
	if (ret) {
		pr_err("Couldn't register clock source.\n");
		goto err_remove_notifier;
	}

	return 0;

err_remove_notifier:
	clk_notifier_unregister(clk, &cs->timer.clk_rate_cb);
err_disable_clk:
	clk_disable_unprepare(clk);
err_free:
	kfree(cs);
	return ret;
}

static int sun5i_rate_cb_clkevt(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct sun5i_timer *timer = to_sun5i_timer(nb);
	struct sun5i_timer_clkevt *ce = container_of(timer, struct sun5i_timer_clkevt, timer);

	if (event == POST_RATE_CHANGE) {
		clockevents_update_freq(&ce->clkevt, ndata->new_rate);
		ce->timer.ticks_per_jiffy = DIV_ROUND_UP(ndata->new_rate, HZ);
	}

	return NOTIFY_DONE;
}

static int __init sun5i_setup_clockevent(struct device_node *node, void __iomem *base,
					 struct clk *clk, int irq)
{
	struct sun5i_timer_clkevt *ce;
	unsigned long rate;
	int ret;
	u32 val;

	ce = kzalloc(sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("Couldn't enable parent clock\n");
		goto err_free;
	}

	rate = clk_get_rate(clk);
	if (!rate) {
		pr_err("Couldn't get parent clock rate\n");
		ret = -EINVAL;
		goto err_disable_clk;
	}

	ce->timer.base = base;
	ce->timer.ticks_per_jiffy = DIV_ROUND_UP(rate, HZ);
	ce->timer.clk = clk;
	ce->timer.clk_rate_cb.notifier_call = sun5i_rate_cb_clkevt;
	ce->timer.clk_rate_cb.next = NULL;

	ret = clk_notifier_register(clk, &ce->timer.clk_rate_cb);
	if (ret) {
		pr_err("Unable to register clock notifier.\n");
		goto err_disable_clk;
	}

	ce->clkevt.name = node->name;
	ce->clkevt.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	ce->clkevt.set_next_event = sun5i_clkevt_next_event;
	ce->clkevt.set_state_shutdown = sun5i_clkevt_shutdown;
	ce->clkevt.set_state_periodic = sun5i_clkevt_set_periodic;
	ce->clkevt.set_state_oneshot = sun5i_clkevt_set_oneshot;
	ce->clkevt.tick_resume = sun5i_clkevt_shutdown;
	ce->clkevt.rating = 340;
	ce->clkevt.irq = irq;
	ce->clkevt.cpumask = cpu_possible_mask;

	/* Enable timer0 interrupt */
	val = readl(base + TIMER_IRQ_EN_REG);
	writel(val | TIMER_IRQ_EN(0), base + TIMER_IRQ_EN_REG);

	clockevents_config_and_register(&ce->clkevt, rate,
					TIMER_SYNC_TICKS, 0xffffffff);

	ret = request_irq(irq, sun5i_timer_interrupt, IRQF_TIMER | IRQF_IRQPOLL,
			  "sun5i_timer0", ce);
	if (ret) {
		pr_err("Unable to register interrupt\n");
		goto err_remove_notifier;
	}

	return 0;

err_remove_notifier:
	clk_notifier_unregister(clk, &ce->timer.clk_rate_cb);
err_disable_clk:
	clk_disable_unprepare(clk);
err_free:
	kfree(ce);
	return ret;
}

static int __init sun5i_timer_init(struct device_node *node)
{
	struct reset_control *rstc;
	void __iomem *timer_base;
	struct clk *clk;
	int irq, ret;

	timer_base = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(timer_base)) {
		pr_err("Can't map registers\n");
		return PTR_ERR(timer_base);
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		pr_err("Can't parse IRQ\n");
		return -EINVAL;
	}

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("Can't get timer clock\n");
		return PTR_ERR(clk);
	}

	rstc = of_reset_control_get(node, NULL);
	if (!IS_ERR(rstc))
		reset_control_deassert(rstc);

	ret = sun5i_setup_clocksource(node, timer_base, clk, irq);
	if (ret)
		return ret;

	return sun5i_setup_clockevent(node, timer_base, clk, irq);
}
TIMER_OF_DECLARE(sun5i_a13, "allwinner,sun5i-a13-hstimer",
			   sun5i_timer_init);
TIMER_OF_DECLARE(sun7i_a20, "allwinner,sun7i-a20-hstimer",
			   sun5i_timer_init);
