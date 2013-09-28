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
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>

#include <asm/mach/time.h>

#include "common.h"

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

#define MSM_DGT_SHIFT 5

static void __iomem *event_base;
static void __iomem *sts_base;

static irqreturn_t msm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
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

static struct clock_event_device __percpu *msm_evt;

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

static int msm_timer_irq;
static int msm_timer_has_ppi;

static int msm_local_timer_setup(struct clock_event_device *evt)
{
	int cpu = smp_processor_id();
	int err;

	evt->irq = msm_timer_irq;
	evt->name = "msm_timer";
	evt->features = CLOCK_EVT_FEAT_ONESHOT;
	evt->rating = 200;
	evt->set_mode = msm_timer_set_mode;
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

static void msm_local_timer_stop(struct clock_event_device *evt)
{
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	disable_percpu_irq(evt->irq);
}

static int msm_timer_cpu_notify(struct notifier_block *self,
					   unsigned long action, void *hcpu)
{
	/*
	 * Grab cpu pointer in each case to avoid spurious
	 * preemptible warnings
	 */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		msm_local_timer_setup(this_cpu_ptr(msm_evt));
		break;
	case CPU_DYING:
		msm_local_timer_stop(this_cpu_ptr(msm_evt));
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block msm_timer_cpu_nb = {
	.notifier_call = msm_timer_cpu_notify,
};

static notrace u32 msm_sched_clock_read(void)
{
	return msm_clocksource.read(&msm_clocksource);
}

static void __init msm_timer_init(u32 dgt_hz, int sched_bits, int irq,
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
		res = register_cpu_notifier(&msm_timer_cpu_nb);
		if (res) {
			free_percpu_irq(irq, msm_evt);
			goto err;
		}

		/* Immediately configure the timer on the boot CPU */
		msm_local_timer_setup(__this_cpu_ptr(msm_evt));
	}

err:
	writel_relaxed(TIMER_ENABLE_EN, source_base + TIMER_ENABLE);
	res = clocksource_register_hz(cs, dgt_hz);
	if (res)
		pr_err("clocksource_register failed\n");
	setup_sched_clock(msm_sched_clock_read, sched_bits, dgt_hz);
}

#ifdef CONFIG_OF
static void __init msm_dt_timer_init(struct device_node *np)
{
	u32 freq;
	int irq;
	struct resource res;
	u32 percpu_offset;
	void __iomem *base;
	void __iomem *cpu0_base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("Failed to map event base\n");
		return;
	}

	/* We use GPT0 for the clockevent */
	irq = irq_of_parse_and_map(np, 1);
	if (irq <= 0) {
		pr_err("Can't get irq\n");
		return;
	}

	/* We use CPU0's DGT for the clocksource */
	if (of_property_read_u32(np, "cpu-offset", &percpu_offset))
		percpu_offset = 0;

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("Failed to parse DGT resource\n");
		return;
	}

	cpu0_base = ioremap(res.start + percpu_offset, resource_size(&res));
	if (!cpu0_base) {
		pr_err("Failed to map source base\n");
		return;
	}

	if (of_property_read_u32(np, "clock-frequency", &freq)) {
		pr_err("Unknown frequency\n");
		return;
	}
	of_node_put(np);

	event_base = base + 0x4;
	sts_base = base + 0x88;
	source_base = cpu0_base + 0x24;
	freq /= 4;
	writel_relaxed(DGT_CLK_CTL_DIV_4, source_base + DGT_CLK_CTL);

	msm_timer_init(freq, 32, irq, !!percpu_offset);
}
CLOCKSOURCE_OF_DECLARE(kpss_timer, "qcom,kpss-timer", msm_dt_timer_init);
CLOCKSOURCE_OF_DECLARE(scss_timer, "qcom,scss-timer", msm_dt_timer_init);
#endif

static int __init msm_timer_map(phys_addr_t addr, u32 event, u32 source,
				u32 sts)
{
	void __iomem *base;

	base = ioremap(addr, SZ_256);
	if (!base) {
		pr_err("Failed to map timer base\n");
		return -ENOMEM;
	}
	event_base = base + event;
	source_base = base + source;
	if (sts)
		sts_base = base + sts;

	return 0;
}

void __init msm7x01_timer_init(void)
{
	struct clocksource *cs = &msm_clocksource;

	if (msm_timer_map(0xc0100000, 0x0, 0x10, 0x0))
		return;
	cs->read = msm_read_timer_count_shift;
	cs->mask = CLOCKSOURCE_MASK((32 - MSM_DGT_SHIFT));
	/* 600 KHz */
	msm_timer_init(19200000 >> MSM_DGT_SHIFT, 32 - MSM_DGT_SHIFT, 7,
			false);
}

void __init msm7x30_timer_init(void)
{
	if (msm_timer_map(0xc0100000, 0x4, 0x24, 0x80))
		return;
	msm_timer_init(24576000 / 4, 32, 1, false);
}

void __init qsd8x50_timer_init(void)
{
	if (msm_timer_map(0xAC100000, 0x0, 0x10, 0x34))
		return;
	msm_timer_init(19200000 / 4, 32, 7, false);
}
