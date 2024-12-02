// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2012,2014, The Linux Foundation. All rights reserved.
 */

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <asm/delay.h>

#define TIMER_MATCH_VAL			0x0000
#define TIMER_COUNT_VAL			0x0004
#define TIMER_ENABLE			0x0008
#define TIMER_ENABLE_CLR_ON_MATCH_EN	BIT(1)
#define TIMER_ENABLE_EN			BIT(0)
#define TIMER_CLEAR			0x000C
#define DGT_CLK_CTL			0x10
#define DGT_CLK_CTL_DIV_4		0x3
#define TIMER_STS_GPT0_CLR_PEND		BIT(10)

#define GPT_HZ 32768

static void __iomem *event_base;
static void __iomem *sts_base;

static irqreturn_t msm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	/* Stop the timer tick */
	if (clockevent_state_oneshot(evt)) {
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

	ctrl &= ~TIMER_ENABLE_EN;
	writel_relaxed(ctrl, event_base + TIMER_ENABLE);

	writel_relaxed(ctrl, event_base + TIMER_CLEAR);
	writel_relaxed(cycles, event_base + TIMER_MATCH_VAL);

	if (sts_base)
		while (readl_relaxed(sts_base) & TIMER_STS_GPT0_CLR_PEND)
			cpu_relax();

	writel_relaxed(ctrl | TIMER_ENABLE_EN, event_base + TIMER_ENABLE);
	return 0;
}

static int msm_timer_shutdown(struct clock_event_device *evt)
{
	u32 ctrl;

	ctrl = readl_relaxed(event_base + TIMER_ENABLE);
	ctrl &= ~(TIMER_ENABLE_EN | TIMER_ENABLE_CLR_ON_MATCH_EN);
	writel_relaxed(ctrl, event_base + TIMER_ENABLE);
	return 0;
}

static struct clock_event_device __percpu *msm_evt;

static void __iomem *source_base;

static notrace u64 msm_read_timer_count(struct clocksource *cs)
{
	return readl_relaxed(source_base + TIMER_COUNT_VAL);
}

static struct clocksource msm_clocksource = {
	.name	= "dg_timer",
	.rating	= 300,
	.read	= msm_read_timer_count,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int msm_timer_irq;
static int msm_timer_has_ppi;

static int msm_local_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *evt = per_cpu_ptr(msm_evt, cpu);
	int err;

	evt->irq = msm_timer_irq;
	evt->name = "msm_timer";
	evt->features = CLOCK_EVT_FEAT_ONESHOT;
	evt->rating = 200;
	evt->set_state_shutdown = msm_timer_shutdown;
	evt->set_state_oneshot = msm_timer_shutdown;
	evt->tick_resume = msm_timer_shutdown;
	evt->set_next_event = msm_timer_set_next_event;
	evt->cpumask = cpumask_of(cpu);

	clockevents_config_and_register(evt, GPT_HZ, 4, 0xffffffff);

	if (msm_timer_has_ppi) {
		enable_percpu_irq(evt->irq, IRQ_TYPE_EDGE_RISING);
	} else {
		err = request_irq(evt->irq, msm_timer_interrupt,
				IRQF_TIMER | IRQF_NOBALANCING |
				IRQF_TRIGGER_RISING, "gp_timer", evt);
		if (err)
			pr_err("request_irq failed\n");
	}

	return 0;
}

static int msm_local_timer_dying_cpu(unsigned int cpu)
{
	struct clock_event_device *evt = per_cpu_ptr(msm_evt, cpu);

	disable_percpu_irq(evt->irq);
	return 0;
}

static u64 notrace msm_sched_clock_read(void)
{
	return msm_clocksource.read(&msm_clocksource);
}

static unsigned long msm_read_current_timer(void)
{
	return msm_clocksource.read(&msm_clocksource);
}

static struct delay_timer msm_delay_timer = {
	.read_current_timer = msm_read_current_timer,
};

static int __init msm_timer_init(u32 dgt_hz, int sched_bits, int irq,
				  bool percpu)
{
	struct clocksource *cs = &msm_clocksource;
	int res = 0;

	msm_timer_irq = irq;
	msm_timer_has_ppi = percpu;

	msm_evt = alloc_percpu(struct clock_event_device);
	if (!msm_evt) {
		pr_err("memory allocation failed for clockevents\n");
		goto err;
	}

	if (percpu)
		res = request_percpu_irq(irq, msm_timer_interrupt,
					 "gp_timer", msm_evt);

	if (res) {
		pr_err("request_percpu_irq failed\n");
	} else {
		/* Install and invoke hotplug callbacks */
		res = cpuhp_setup_state(CPUHP_AP_QCOM_TIMER_STARTING,
					"clockevents/qcom/timer:starting",
					msm_local_timer_starting_cpu,
					msm_local_timer_dying_cpu);
		if (res) {
			free_percpu_irq(irq, msm_evt);
			goto err;
		}
	}

err:
	writel_relaxed(TIMER_ENABLE_EN, source_base + TIMER_ENABLE);
	res = clocksource_register_hz(cs, dgt_hz);
	if (res)
		pr_err("clocksource_register failed\n");
	sched_clock_register(msm_sched_clock_read, sched_bits, dgt_hz);
	msm_delay_timer.freq = dgt_hz;
	register_current_timer_delay(&msm_delay_timer);

	return res;
}

static int __init msm_dt_timer_init(struct device_node *np)
{
	u32 freq;
	int irq, ret;
	struct resource res;
	u32 percpu_offset;
	void __iomem *base;
	void __iomem *cpu0_base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("Failed to map event base\n");
		return -ENXIO;
	}

	/* We use GPT0 for the clockevent */
	irq = irq_of_parse_and_map(np, 1);
	if (irq <= 0) {
		pr_err("Can't get irq\n");
		return -EINVAL;
	}

	/* We use CPU0's DGT for the clocksource */
	if (of_property_read_u32(np, "cpu-offset", &percpu_offset))
		percpu_offset = 0;

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		pr_err("Failed to parse DGT resource\n");
		return ret;
	}

	cpu0_base = ioremap(res.start + percpu_offset, resource_size(&res));
	if (!cpu0_base) {
		pr_err("Failed to map source base\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "clock-frequency", &freq)) {
		iounmap(cpu0_base);
		pr_err("Unknown frequency\n");
		return -EINVAL;
	}

	event_base = base + 0x4;
	sts_base = base + 0x88;
	source_base = cpu0_base + 0x24;
	freq /= 4;
	writel_relaxed(DGT_CLK_CTL_DIV_4, source_base + DGT_CLK_CTL);

	ret = msm_timer_init(freq, 32, irq, !!percpu_offset);
	if (ret)
		iounmap(cpu0_base);

	return ret;
}
TIMER_OF_DECLARE(kpss_timer, "qcom,kpss-timer", msm_dt_timer_init);
TIMER_OF_DECLARE(scss_timer, "qcom,scss-timer", msm_dt_timer_init);
