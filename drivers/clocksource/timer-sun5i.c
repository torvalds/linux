// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner SoCs hstimer driver.
 *
 * Copyright (C) 2013 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
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
#include <linux/platform_device.h>

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
	struct clocksource	clksrc;
	struct clock_event_device	clkevt;
};

#define nb_to_sun5i_timer(x) \
	container_of(x, struct sun5i_timer, clk_rate_cb)
#define clksrc_to_sun5i_timer(x) \
	container_of(x, struct sun5i_timer, clksrc)
#define clkevt_to_sun5i_timer(x) \
	container_of(x, struct sun5i_timer, clkevt)

/*
 * When we disable a timer, we need to wait at least for 2 cycles of
 * the timer source clock. We will use for that the clocksource timer
 * that is already setup and runs at the same frequency than the other
 * timers, and we never will be disabled.
 */
static void sun5i_clkevt_sync(struct sun5i_timer *ce)
{
	u32 old = readl(ce->base + TIMER_CNTVAL_LO_REG(1));

	while ((old - readl(ce->base + TIMER_CNTVAL_LO_REG(1))) < TIMER_SYNC_TICKS)
		cpu_relax();
}

static void sun5i_clkevt_time_stop(struct sun5i_timer *ce, u8 timer)
{
	u32 val = readl(ce->base + TIMER_CTL_REG(timer));
	writel(val & ~TIMER_CTL_ENABLE, ce->base + TIMER_CTL_REG(timer));

	sun5i_clkevt_sync(ce);
}

static void sun5i_clkevt_time_setup(struct sun5i_timer *ce, u8 timer, u32 delay)
{
	writel(delay, ce->base + TIMER_INTVAL_LO_REG(timer));
}

static void sun5i_clkevt_time_start(struct sun5i_timer *ce, u8 timer, bool periodic)
{
	u32 val = readl(ce->base + TIMER_CTL_REG(timer));

	if (periodic)
		val &= ~TIMER_CTL_ONESHOT;
	else
		val |= TIMER_CTL_ONESHOT;

	writel(val | TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       ce->base + TIMER_CTL_REG(timer));
}

static int sun5i_clkevt_shutdown(struct clock_event_device *clkevt)
{
	struct sun5i_timer *ce = clkevt_to_sun5i_timer(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	return 0;
}

static int sun5i_clkevt_set_oneshot(struct clock_event_device *clkevt)
{
	struct sun5i_timer *ce = clkevt_to_sun5i_timer(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	sun5i_clkevt_time_start(ce, 0, false);
	return 0;
}

static int sun5i_clkevt_set_periodic(struct clock_event_device *clkevt)
{
	struct sun5i_timer *ce = clkevt_to_sun5i_timer(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	sun5i_clkevt_time_setup(ce, 0, ce->ticks_per_jiffy);
	sun5i_clkevt_time_start(ce, 0, true);
	return 0;
}

static int sun5i_clkevt_next_event(unsigned long evt,
				   struct clock_event_device *clkevt)
{
	struct sun5i_timer *ce = clkevt_to_sun5i_timer(clkevt);

	sun5i_clkevt_time_stop(ce, 0);
	sun5i_clkevt_time_setup(ce, 0, evt - TIMER_SYNC_TICKS);
	sun5i_clkevt_time_start(ce, 0, false);

	return 0;
}

static irqreturn_t sun5i_timer_interrupt(int irq, void *dev_id)
{
	struct sun5i_timer *ce = dev_id;

	writel(0x1, ce->base + TIMER_IRQ_ST_REG);
	ce->clkevt.event_handler(&ce->clkevt);

	return IRQ_HANDLED;
}

static u64 sun5i_clksrc_read(struct clocksource *clksrc)
{
	struct sun5i_timer *cs = clksrc_to_sun5i_timer(clksrc);

	return ~readl(cs->base + TIMER_CNTVAL_LO_REG(1));
}

static int sun5i_rate_cb(struct notifier_block *nb,
			 unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct sun5i_timer *cs = nb_to_sun5i_timer(nb);

	switch (event) {
	case PRE_RATE_CHANGE:
		clocksource_unregister(&cs->clksrc);
		break;

	case POST_RATE_CHANGE:
		clocksource_register_hz(&cs->clksrc, ndata->new_rate);
		clockevents_update_freq(&cs->clkevt, ndata->new_rate);
		cs->ticks_per_jiffy = DIV_ROUND_UP(ndata->new_rate, HZ);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

static int sun5i_setup_clocksource(struct platform_device *pdev,
				   unsigned long rate)
{
	struct sun5i_timer *cs = platform_get_drvdata(pdev);
	void __iomem *base = cs->base;
	int ret;

	writel(~0, base + TIMER_INTVAL_LO_REG(1));
	writel(TIMER_CTL_ENABLE | TIMER_CTL_RELOAD,
	       base + TIMER_CTL_REG(1));

	cs->clksrc.name = pdev->dev.of_node->name;
	cs->clksrc.rating = 340;
	cs->clksrc.read = sun5i_clksrc_read;
	cs->clksrc.mask = CLOCKSOURCE_MASK(32);
	cs->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	ret = clocksource_register_hz(&cs->clksrc, rate);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register clock source.\n");
		return ret;
	}

	return 0;
}

static int sun5i_setup_clockevent(struct platform_device *pdev,
				  unsigned long rate, int irq)
{
	struct device *dev = &pdev->dev;
	struct sun5i_timer *ce = platform_get_drvdata(pdev);
	void __iomem *base = ce->base;
	int ret;
	u32 val;

	ce->clkevt.name = dev->of_node->name;
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

	ret = devm_request_irq(dev, irq, sun5i_timer_interrupt,
			       IRQF_TIMER | IRQF_IRQPOLL,
			       "sun5i_timer0", ce);
	if (ret) {
		dev_err(dev, "Unable to register interrupt\n");
		return ret;
	}

	return 0;
}

static int sun5i_timer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sun5i_timer *st;
	struct reset_control *rstc;
	void __iomem *timer_base;
	struct clk *clk;
	unsigned long rate;
	int irq, ret;

	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	platform_set_drvdata(pdev, st);

	timer_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(timer_base)) {
		dev_err(dev, "Can't map registers\n");
		return PTR_ERR(timer_base);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Can't get IRQ\n");
		return irq;
	}

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "Can't get timer clock\n");
		return PTR_ERR(clk);
	}

	rate = clk_get_rate(clk);
	if (!rate) {
		dev_err(dev, "Couldn't get parent clock rate\n");
		return -EINVAL;
	}

	st->base = timer_base;
	st->ticks_per_jiffy = DIV_ROUND_UP(rate, HZ);
	st->clk = clk;
	st->clk_rate_cb.notifier_call = sun5i_rate_cb;
	st->clk_rate_cb.next = NULL;

	ret = devm_clk_notifier_register(dev, clk, &st->clk_rate_cb);
	if (ret) {
		dev_err(dev, "Unable to register clock notifier.\n");
		return ret;
	}

	rstc = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (rstc)
		reset_control_deassert(rstc);

	ret = sun5i_setup_clocksource(pdev, rate);
	if (ret)
		return ret;

	ret = sun5i_setup_clockevent(pdev, rate, irq);
	if (ret)
		goto err_unreg_clocksource;

	return 0;

err_unreg_clocksource:
	clocksource_unregister(&st->clksrc);
	return ret;
}

static void sun5i_timer_remove(struct platform_device *pdev)
{
	struct sun5i_timer *st = platform_get_drvdata(pdev);

	clocksource_unregister(&st->clksrc);
}

static const struct of_device_id sun5i_timer_of_match[] = {
	{ .compatible = "allwinner,sun5i-a13-hstimer" },
	{ .compatible = "allwinner,sun7i-a20-hstimer" },
	{},
};
MODULE_DEVICE_TABLE(of, sun5i_timer_of_match);

static struct platform_driver sun5i_timer_driver = {
	.probe		= sun5i_timer_probe,
	.remove_new	= sun5i_timer_remove,
	.driver	= {
		.name	= "sun5i-timer",
		.of_match_table = sun5i_timer_of_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(sun5i_timer_driver);
