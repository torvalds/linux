/*
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2012, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/localtimer.h>
#include <asm/sched_clock.h>

#include "common.h"

#define TIMER_MATCH_VAL         0x0000
#define TIMER_COUNT_VAL         0x0004
#define TIMER_ENABLE            0x0008
#define TIMER_ENABLE_CLR_ON_MATCH_EN    BIT(1)
#define TIMER_ENABLE_EN                 BIT(0)
#define TIMER_CLEAR             0x000C
#define DGT_CLK_CTL             0x0030
#define DGT_CLK_CTL_DIV_4	0x3

#define GPT_HZ 32768

#define MSM_DGT_SHIFT 5

static void __iomem *event_base;

static irqreturn_t msm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;
	/* Stop the timer tick */
	if (evt->mode == CLOCK_EVT_MODE_ONESHOT) {
		u32 ctrl = readl_relaxed(event_base + TIMER_ENABLE);
		ctrl &= ~TIMER_ENABLE_EN;
		writel_relaxed(ctrl, event_base + TIMER_ENABLE);
	}
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static int msm_timer_set_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	u32 ctrl = readl_relaxed(event_base + TIMER_ENABLE);

	writel_relaxed(0, event_base + TIMER_CLEAR);
	writel_relaxed(cycles, event_base + TIMER_MATCH_VAL);
	writel_relaxed(ctrl | TIMER_ENABLE_EN, event_base + TIMER_ENABLE);
	return 0;
}

static void msm_timer_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	u32 ctrl;

	ctrl = readl_relaxed(event_base + TIMER_ENABLE);
	ctrl &= ~(TIMER_ENABLE_EN | TIMER_ENABLE_CLR_ON_MATCH_EN);

	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Timer is enabled in set_next_event */
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		break;
	}
	writel_relaxed(ctrl, event_base + TIMER_ENABLE);
}

static struct clock_event_device msm_clockevent = {
	.name		= "gp_timer",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= msm_timer_set_next_event,
	.set_mode	= msm_timer_set_mode,
};

static union {
	struct clock_event_device *evt;
	struct clock_event_device __percpu **percpu_evt;
} msm_evt;

static void __iomem *source_base;

static notrace cycle_t msm_read_timer_count(struct clocksource *cs)
{
	return readl_relaxed(source_base + TIMER_COUNT_VAL);
}

static notrace cycle_t msm_read_timer_count_shift(struct clocksource *cs)
{
	/*
	 * Shift timer count down by a constant due to unreliable lower bits
	 * on some targets.
	 */
	return msm_read_timer_count(cs) >> MSM_DGT_SHIFT;
}

static struct clocksource msm_clocksource = {
	.name	= "dg_timer",
	.rating	= 300,
	.read	= msm_read_timer_count,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_LOCAL_TIMERS
static int __cpuinit msm_local_timer_setup(struct clock_event_device *evt)
{
	/* Use existing clock_event for cpu 0 */
	if (!smp_processor_id())
		return 0;

	writel_relaxed(0, event_base + TIMER_ENABLE);
	writel_relaxed(0, event_base + TIMER_CLEAR);
	writel_relaxed(~0, event_base + TIMER_MATCH_VAL);
	evt->irq = msm_clockevent.irq;
	evt->name = "local_timer";
	evt->features = msm_clockevent.features;
	evt->rating = msm_clockevent.rating;
	evt->set_mode = msm_timer_set_mode;
	evt->set_next_event = msm_timer_set_next_event;
	evt->shift = msm_clockevent.shift;
	evt->mult = div_sc(GPT_HZ, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(0xf0000000, evt);
	evt->min_delta_ns = clockevent_delta2ns(4, evt);

	*__this_cpu_ptr(msm_evt.percpu_evt) = evt;
	clockevents_register_device(evt);
	enable_percpu_irq(evt->irq, IRQ_TYPE_EDGE_RISING);
	return 0;
}

static void msm_local_timer_stop(struct clock_event_device *evt)
{
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	disable_percpu_irq(evt->irq);
}

static struct local_timer_ops msm_local_timer_ops __cpuinitdata = {
	.setup	= msm_local_timer_setup,
	.stop	= msm_local_timer_stop,
};
#endif /* CONFIG_LOCAL_TIMERS */

static notrace u32 msm_sched_clock_read(void)
{
	return msm_clocksource.read(&msm_clocksource);
}

static void __init msm_timer_init(u32 dgt_hz, int sched_bits, int irq,
				  bool percpu)
{
	struct clock_event_device *ce = &msm_clockevent;
	struct clocksource *cs = &msm_clocksource;
	int res;

	writel_relaxed(0, event_base + TIMER_ENABLE);
	writel_relaxed(0, event_base + TIMER_CLEAR);
	writel_relaxed(~0, event_base + TIMER_MATCH_VAL);
	ce->cpumask = cpumask_of(0);
	ce->irq = irq;

	clockevents_config_and_register(ce, GPT_HZ, 4, 0xffffffff);
	if (percpu) {
		msm_evt.percpu_evt = alloc_percpu(struct clock_event_device *);
		if (!msm_evt.percpu_evt) {
			pr_err("memory allocation failed for %s\n", ce->name);
			goto err;
		}
		*__this_cpu_ptr(msm_evt.percpu_evt) = ce;
		res = request_percpu_irq(ce->irq, msm_timer_interrupt,
					 ce->name, msm_evt.percpu_evt);
		if (!res) {
			enable_percpu_irq(ce->irq, IRQ_TYPE_EDGE_RISING);
#ifdef CONFIG_LOCAL_TIMERS
			local_timer_register(&msm_local_timer_ops);
#endif
		}
	} else {
		msm_evt.evt = ce;
		res = request_irq(ce->irq, msm_timer_interrupt,
				  IRQF_TIMER | IRQF_NOBALANCING |
				  IRQF_TRIGGER_RISING, ce->name, &msm_evt.evt);
	}

	if (res)
		pr_err("request_irq failed for %s\n", ce->name);
err:
	writel_relaxed(TIMER_ENABLE_EN, source_base + TIMER_ENABLE);
	res = clocksource_register_hz(cs, dgt_hz);
	if (res)
		pr_err("clocksource_register failed\n");
	setup_sched_clock(msm_sched_clock_read, sched_bits, dgt_hz);
}

#ifdef CONFIG_OF
static const struct of_device_id msm_dgt_match[] __initconst = {
	{ .compatible = "qcom,msm-dgt" },
	{ },
};

static const struct of_device_id msm_gpt_match[] __initconst = {
	{ .compatible = "qcom,msm-gpt" },
	{ },
};

static void __init msm_dt_timer_init(void)
{
	struct device_node *np;
	u32 freq;
	int irq;
	struct resource res;
	u32 percpu_offset;
	void __iomem *dgt_clk_ctl;

	np = of_find_matching_node(NULL, msm_gpt_match);
	if (!np) {
		pr_err("Can't find GPT DT node\n");
		return;
	}

	event_base = of_iomap(np, 0);
	if (!event_base) {
		pr_err("Failed to map event base\n");
		return;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Can't get irq\n");
		return;
	}
	of_node_put(np);

	np = of_find_matching_node(NULL, msm_dgt_match);
	if (!np) {
		pr_err("Can't find DGT DT node\n");
		return;
	}

	if (of_property_read_u32(np, "cpu-offset", &percpu_offset))
		percpu_offset = 0;

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("Failed to parse DGT resource\n");
		return;
	}

	source_base = ioremap(res.start + percpu_offset, resource_size(&res));
	if (!source_base) {
		pr_err("Failed to map source base\n");
		return;
	}

	if (!of_address_to_resource(np, 1, &res)) {
		dgt_clk_ctl = ioremap(res.start + percpu_offset,
				      resource_size(&res));
		if (!dgt_clk_ctl) {
			pr_err("Failed to map DGT control base\n");
			return;
		}
		writel_relaxed(DGT_CLK_CTL_DIV_4, dgt_clk_ctl);
		iounmap(dgt_clk_ctl);
	}

	if (of_property_read_u32(np, "clock-frequency", &freq)) {
		pr_err("Unknown frequency\n");
		return;
	}
	of_node_put(np);

	msm_timer_init(freq, 32, irq, !!percpu_offset);
}

struct sys_timer msm_dt_timer = {
	.init = msm_dt_timer_init
};
#endif

static int __init msm_timer_map(phys_addr_t event, phys_addr_t source)
{
	event_base = ioremap(event, SZ_64);
	if (!event_base) {
		pr_err("Failed to map event base\n");
		return 1;
	}
	source_base = ioremap(source, SZ_64);
	if (!source_base) {
		pr_err("Failed to map source base\n");
		return 1;
	}
	return 0;
}

static void __init msm7x01_timer_init(void)
{
	struct clocksource *cs = &msm_clocksource;

	if (msm_timer_map(0xc0100000, 0xc0100010))
		return;
	cs->read = msm_read_timer_count_shift;
	cs->mask = CLOCKSOURCE_MASK((32 - MSM_DGT_SHIFT));
	/* 600 KHz */
	msm_timer_init(19200000 >> MSM_DGT_SHIFT, 32 - MSM_DGT_SHIFT, 7,
			false);
}

struct sys_timer msm7x01_timer = {
	.init = msm7x01_timer_init
};

static void __init msm7x30_timer_init(void)
{
	if (msm_timer_map(0xc0100004, 0xc0100024))
		return;
	msm_timer_init(24576000 / 4, 32, 1, false);
}

struct sys_timer msm7x30_timer = {
	.init = msm7x30_timer_init
};

static void __init msm8960_timer_init(void)
{
	if (msm_timer_map(0x0200A004, 0x0208A024))
		return;
	writel_relaxed(DGT_CLK_CTL_DIV_4, event_base + DGT_CLK_CTL);
	msm_timer_init(27000000 / 4, 32, 17, true);
}

struct sys_timer msm8960_timer = {
	.init = msm8960_timer_init
};

static void __init qsd8x50_timer_init(void)
{
	if (msm_timer_map(0xAC100000, 0xAC100010))
		return;
	msm_timer_init(19200000 / 4, 32, 7, false);
}

struct sys_timer qsd8x50_timer = {
	.init = qsd8x50_timer_init
};
