// SPDX-License-Identifier: GPL-2.0
/*
 * Starfive Timer driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 *         Samin Guo <samin.guo@starfivetech.com>
 */

#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/module.h>
#include <linux/reset.h>
#include "timer-starfive.h"

#define CLOCK_SOURCE_RATE	200
#define VALID_BITS		32
#define DELAY_US		0
#define TIMEOUT_US		10000
#define CLOCKEVENT_RATING	300
#define MAX_TICKS		0xffffffff
#define MIN_TICKS		0xf

struct starfive_timer __initdata jh7110_starfive_timer = {
	.ctrl		= STF_TIMER_CTL,
	.load		= STF_TIMER_LOAD,
	.enable		= STF_TIMER_ENABLE,
	.reload		= STF_TIMER_RELOAD,
	.value		= STF_TIMER_VALUE,
	.intclr		= STF_TIMER_INT_CLR,
	.intmask	= STF_TIMER_INT_MASK,
	.timer_base	= {TIMER_BASE(0), TIMER_BASE(1), TIMER_BASE(2),
			TIMER_BASE(3), TIMER_BASE(4), TIMER_BASE(5),
			TIMER_BASE(6), TIMER_BASE(7)},
};

struct starfive_timer_misc_count sfcmisc = {
	.clk_count = 0,
	.flg_init_clk = false,
};

static inline struct starfive_clkevt *
to_starfive_clkevt(struct clock_event_device *evt)
{
	return container_of(evt, struct starfive_clkevt, evt);
}

static inline struct starfive_clkevt *
to_starfive_clksrc(struct clocksource *cs)
{
	return container_of(cs, struct starfive_clkevt, cs);
}

static inline void timer_set_mod(struct starfive_clkevt *clkevt, int mod)
{
	writel(mod, clkevt->ctrl);
}

/*
 * After disable timer, then enable, the timer will start
 * from the reload count value(0x08[31:0]).
 */
static inline void timer_int_enable(struct starfive_clkevt *clkevt)
{
	writel(INTMASK_ENABLE_DIS, clkevt->intmask);
}

static inline void timer_int_disable(struct starfive_clkevt *clkevt)
{
	writel(INTMASK_ENABLE, clkevt->intmask);
}

static inline void timer_int_clear(struct starfive_clkevt *clkevt)
{
	/* waiting interrupt can be to clearing */
	u32 value;
	int ret = 0;

	value = readl(clkevt->intclr);
	ret = readl_poll_timeout_atomic(clkevt->intclr, value,
			!(value & INT_STATUS_CLR_AVA), DELAY_US, TIMEOUT_US);
	if (!ret)
		writel(1, clkevt->intclr);
}

/*
 * The initial value to be loaded into the
 * counter and is also used as the reload value.
 */
static inline void timer_set_val(struct starfive_clkevt *clkevt, u32 val)
{
	writel(val, clkevt->load);
}

static inline u32 timer_get_val(struct starfive_clkevt *clkevt)
{
	return readl(clkevt->value);
}

static inline u32 timer_get_load_val(struct starfive_clkevt *clkevt)
{
	return readl(clkevt->load);
}

/*
 * Write RELOAD register to reload preset value to counter.
 * (Write 0 and write 1 are both ok)
 */
static inline void
timer_set_reload(struct starfive_clkevt *clkevt)
{
	writel(1, clkevt->reload);
}

static inline void timer_enable(struct starfive_clkevt *clkevt)
{
	writel(TIMER_ENA, clkevt->enable);
}

static inline void timer_disable(struct starfive_clkevt *clkevt)
{
	writel(TIMER_ENA_DIS, clkevt->enable);
}

static void timer_shutdown(struct starfive_clkevt *clkevt)
{
	timer_int_disable(clkevt);
	timer_disable(clkevt);
}

#ifdef CONFIG_PM_SLEEP

static void starfive_timer_suspend(struct clocksource *cs)
{
	struct starfive_clkevt *clkevt;
	struct clk *pclk;

	clkevt = to_starfive_clksrc(cs);

	clkevt->reload_val = timer_get_load_val(clkevt);
	timer_disable(clkevt);
	timer_int_disable(clkevt);

	clkevt->misc->clk_count--;

	if (clkevt->misc->clk_count < 1)
		pclk = of_clk_get_by_name(clkevt->device_node, "apb_clk");

	if (!clkevt->misc->flg_init_clk) {
		const char *name = NULL;

		of_property_read_string_index(clkevt->device_node,
				"clock-names", clkevt->index, &name);
		clkevt->clk = of_clk_get_by_name(clkevt->device_node, name);
		clk_prepare_enable(clkevt->clk);
		clk_disable_unprepare(clkevt->clk);

		if (clkevt->misc->clk_count < 1) {
			clk_prepare_enable(pclk);
			clk_disable_unprepare(pclk);
		}
	} else {
		clk_disable_unprepare(clkevt->clk);

		if (clkevt->misc->clk_count < 1)
			clk_disable_unprepare(pclk);
	}
}

static void starfive_timer_resume(struct clocksource *cs)
{
	struct starfive_clkevt *clkevt;

	clkevt = to_starfive_clksrc(cs);

	clkevt->misc->flg_init_clk = true;

	if (clkevt->misc->clk_count < 1) {
		struct clk *pclk;

		pclk = of_clk_get_by_name(clkevt->device_node, "apb_clk");
		clk_prepare_enable(pclk);
	}
	clk_prepare_enable(clkevt->clk);
	clkevt->misc->clk_count++;

	timer_set_val(clkevt, clkevt->reload_val);
	timer_set_reload(clkevt);
	timer_int_enable(clkevt);
	timer_enable(clkevt);
}

#endif /*CONIFG PM SLEEP*/

static int starfive_timer_shutdown(struct clock_event_device *evt)
{
	struct starfive_clkevt *clkevt;

	clkevt = to_starfive_clkevt(evt);
	timer_shutdown(clkevt);

	return 0;
}

static int
starfive_get_clock_rate(struct starfive_clkevt *clkevt, struct device_node *np)
{
	int ret;
	u32 rate;

	if (clkevt->clk) {
		clkevt->rate = clk_get_rate(clkevt->clk);
		if (clkevt->rate > 0) {
			pr_debug("clk_get_rate clkevt->rate: %lld\n",
				clkevt->rate);
			return 0;
		}
	}

	/* Next we try to get clock-frequency from dts.*/
	ret = of_property_read_u32(np, "clock-frequency", &rate);
	if (!ret) {
		pr_debug("Timer: try get clock-frequency:%d MHz\n", rate);
		clkevt->rate = (u64)rate;
		return 0;
	}
	pr_err("Timer: get rate failed, need clock-frequency define in dts.\n");

	return -ENOENT;
}

static u64 starfive_clocksource_mmio_readl_down(struct clocksource *c)
{
	return ~(u64)readl_relaxed(to_starfive_clksrc(c)->value) & c->mask;
}

static int starfive_clocksource_init(struct starfive_clkevt *clkevt,
				const char *name, struct device_node *np)
{

	if (VALID_BITS > 64 || VALID_BITS < 16)
		return -EINVAL;

	clkevt->cs.name = name;
	clkevt->cs.rating = CLOCK_SOURCE_RATE;
	clkevt->cs.read = starfive_clocksource_mmio_readl_down;
	clkevt->cs.mask = CLOCKSOURCE_MASK(VALID_BITS);
	clkevt->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	clkevt->cs.suspend = starfive_timer_suspend;
	clkevt->cs.resume = starfive_timer_resume;

	timer_set_mod(clkevt, MOD_CONTIN);
	timer_set_val(clkevt, MAX_TICKS);  /* val = rate --> 1s */
	timer_int_disable(clkevt);
	timer_int_clear(clkevt);
	timer_int_enable(clkevt);
	timer_enable(clkevt);

	clocksource_register_hz(&clkevt->cs, clkevt->rate);

	return 0;
}

/*
 * IRQ handler for the timer
 */
static irqreturn_t starfive_timer_interrupt(int irq, void *priv)
{
	struct clock_event_device *evt = (struct clock_event_device  *)priv;
	struct starfive_clkevt *clkevt = to_starfive_clkevt(evt);

	timer_int_clear(clkevt);
	if (evt->event_handler)
		evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int starfive_timer_set_periodic(struct clock_event_device *evt)
{
	struct starfive_clkevt *clkevt;

	clkevt = to_starfive_clkevt(evt);

	timer_disable(clkevt);
	timer_set_mod(clkevt, MOD_CONTIN);
	timer_set_val(clkevt, clkevt->periodic);
	timer_int_disable(clkevt);
	timer_int_clear(clkevt);
	timer_int_enable(clkevt);
	timer_enable(clkevt);

	return 0;
}

static int starfive_timer_set_oneshot(struct clock_event_device *evt)
{
	struct starfive_clkevt *clkevt;

	clkevt = to_starfive_clkevt(evt);

	timer_disable(clkevt);
	timer_set_mod(clkevt, MOD_SINGLE);
	timer_set_val(clkevt, MAX_TICKS);
	timer_int_disable(clkevt);
	timer_int_clear(clkevt);
	timer_int_enable(clkevt);
	timer_enable(clkevt);

	return 0;
}

static int starfive_timer_set_next_event(unsigned long next,
					struct clock_event_device *evt)
{
	struct starfive_clkevt *clkevt;

	clkevt = to_starfive_clkevt(evt);

	timer_disable(clkevt);
	timer_set_mod(clkevt, MOD_SINGLE);
	timer_set_val(clkevt, next);
	timer_enable(clkevt);

	return 0;
}

static void starfive_set_clockevent(struct clock_event_device *evt)
{
	evt->features	= CLOCK_EVT_FEAT_PERIODIC |
			CLOCK_EVT_FEAT_ONESHOT |
			CLOCK_EVT_FEAT_DYNIRQ;
	evt->set_state_shutdown	= starfive_timer_shutdown;
	evt->set_state_periodic	= starfive_timer_set_periodic;
	evt->set_state_oneshot	= starfive_timer_set_oneshot;
	evt->set_state_oneshot_stopped = starfive_timer_shutdown;
	evt->set_next_event	= starfive_timer_set_next_event;
	evt->rating		= CLOCKEVENT_RATING;
}

static int starfive_clockevents_register(struct starfive_clkevt *clkevt, unsigned int irq,
				struct device_node *np, const char *name)
{
	int ret = 0;

	ret = starfive_get_clock_rate(clkevt, np);
	if (ret)
		return -EINVAL;

	clkevt->periodic = DIV_ROUND_CLOSEST(clkevt->rate, HZ);

	starfive_set_clockevent(&clkevt->evt);
	clkevt->evt.name = name;
	clkevt->evt.irq = irq;
	clkevt->evt.cpumask = cpu_possible_mask;

	ret = request_irq(irq, starfive_timer_interrupt,
			IRQF_TIMER | IRQF_IRQPOLL, name, &clkevt->evt);
	if (ret)
		pr_err("%s: request_irq failed\n", name);

	clockevents_config_and_register(&clkevt->evt, clkevt->rate,
			MIN_TICKS, MAX_TICKS);

	return ret;
}

static void __init starfive_clkevt_init(struct starfive_timer *timer,
					struct starfive_clkevt *clkevt,
					void __iomem *base, int index)
{
	void __iomem *timer_base;

	timer_base = base + timer->timer_base[index];
	clkevt->base	= timer_base;
	clkevt->ctrl	= timer_base + timer->ctrl;
	clkevt->load	= timer_base + timer->load;
	clkevt->enable	= timer_base + timer->enable;
	clkevt->reload	= timer_base + timer->reload;
	clkevt->value	= timer_base + timer->value;
	clkevt->intclr	= timer_base + timer->intclr;
	clkevt->intmask	= timer_base + timer->intmask;
}

static int __init do_starfive_timer_of_init(struct device_node *np,
					struct starfive_timer *timer)
{
	int index, count, irq, ret = -EINVAL;
	const char *name = NULL;
	struct clk *clk;
	struct clk *pclk;
	struct reset_control *prst;
	struct reset_control *rst;
	struct starfive_clkevt *clkevt[4];
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base)
		return -ENXIO;

	if (!of_device_is_available(np)) {
		ret = -EINVAL;
		goto err;
	}

	pclk = of_clk_get_by_name(np, "apb_clk");
	if (!IS_ERR(pclk))
		if (clk_prepare_enable(pclk))
			pr_warn("pclk for %pOFn is present,"
				"but could not be activated\n", np);

	prst = of_reset_control_get(np, "apb_rst");
	if (!IS_ERR(prst)) {
		reset_control_assert(prst);
		reset_control_deassert(prst);
	}

	count = of_irq_count(np);
	if (count > NR_TIMERS || count <= 0) {
		ret = -EINVAL;
		goto count_err;
	}

	for (index = 0; index < count; index++) {
		/* one of timer is wdog-timer, skip...*/
		of_property_read_string_index(np, "clock-names", index, &name);
		if (strncmp(name, "timer", strlen("timer")))
			continue;

		clkevt[index] = kzalloc(sizeof(*clkevt[index]), GFP_KERNEL);
		if (!clkevt[index]) {
			ret = -ENOMEM;
			goto clkevt_err;
		}
		clkevt[index]->device_node = np;
		clkevt[index]->index = index;
		clkevt[index]->misc = &sfcmisc;

		starfive_clkevt_init(timer, clkevt[index], base, index);

		/* Ensure timers are disabled */
		timer_disable(clkevt[index]);

		clk = of_clk_get_by_name(np, name);
		if (!IS_ERR(clk)) {
			clkevt[index]->clk = clk;

			if (clk_prepare_enable(clk)) {
				pr_warn("clk for %pOFn is present,"
					"but could not be activated\n", np);
			}
		}

		clkevt[index]->misc->clk_count++;

		rst = of_reset_control_get(np, name);
		if (!IS_ERR(rst)) {
			reset_control_assert(rst);
			reset_control_deassert(rst);
		}

		irq = irq_of_parse_and_map(np, index);
		if (irq < 0) {
			ret = -EINVAL;
			goto irq_err;
		}

		snprintf(clkevt[index]->name, sizeof(clkevt[index]->name), "%s.ch%d",
					np->full_name, index);

		ret = starfive_clockevents_register(clkevt[index], irq, np, clkevt[index]->name);
		if (ret) {
			pr_err("%s: init clockevents failed.\n", clkevt[index]->name);
			goto register_err;
		}
		clkevt[index]->irq = irq;

		ret = starfive_clocksource_init(clkevt[index], clkevt[index]->name, np);
		if (ret)
			goto init_err;
	}
	if (!IS_ERR(pclk))
		clk_put(pclk);

	return 0;

init_err:
register_err:
	free_irq(clkevt[index]->irq, &clkevt[index]->evt);
irq_err:
	if (!rst) {
		reset_control_assert(rst);
		reset_control_put(rst);
	}
	if (!clkevt[index]->clk) {
		clk_disable_unprepare(clkevt[index]->clk);
		clk_put(clkevt[index]->clk);
	}
	kfree(clkevt[index]);
clkevt_err:
count_err:
	if (!IS_ERR(pclk)) {
		if (!index)
			clk_disable_unprepare(pclk);
		clk_put(pclk);
	}
err:
	iounmap(base);
	return ret;
}

static int __init starfive_timer_of_init(struct device_node *np)
{
	return do_starfive_timer_of_init(np, &jh7110_starfive_timer);
}
TIMER_OF_DECLARE(starfive_timer, "starfive,jh7110-timers", starfive_timer_of_init);

MODULE_AUTHOR("xingyu.wu <xingyu.wu@starfivetech.com>");
MODULE_AUTHOR("samin.guo <samin.guo@starfivetech.com>");
MODULE_DESCRIPTION("StarFive Timer Device Driver");
MODULE_LICENSE("GPL v2");
