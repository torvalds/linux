/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) "mips-gic-timer: " fmt

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/time.h>
#include <asm/mips-cps.h>

static DEFINE_PER_CPU(struct clock_event_device, gic_clockevent_device);
static int gic_timer_irq;
static unsigned int gic_frequency;

static u64 notrace gic_read_count(void)
{
	unsigned int hi, hi2, lo;

	if (mips_cm_is64)
		return read_gic_counter();

	do {
		hi = read_gic_counter_32h();
		lo = read_gic_counter_32l();
		hi2 = read_gic_counter_32h();
	} while (hi2 != hi);

	return (((u64) hi) << 32) + lo;
}

static int gic_next_event(unsigned long delta, struct clock_event_device *evt)
{
	int cpu = cpumask_first(evt->cpumask);
	u64 cnt;
	int res;

	cnt = gic_read_count();
	cnt += (u64)delta;
	if (cpu == raw_smp_processor_id()) {
		write_gic_vl_compare(cnt);
	} else {
		write_gic_vl_other(mips_cm_vp_id(cpu));
		write_gic_vo_compare(cnt);
	}
	res = ((int)(gic_read_count() - cnt) >= 0) ? -ETIME : 0;
	return res;
}

static irqreturn_t gic_compare_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = dev_id;

	write_gic_vl_compare(read_gic_vl_compare());
	cd->event_handler(cd);
	return IRQ_HANDLED;
}

static struct irqaction gic_compare_irqaction = {
	.handler = gic_compare_interrupt,
	.percpu_dev_id = &gic_clockevent_device,
	.flags = IRQF_PERCPU | IRQF_TIMER,
	.name = "timer",
};

static void gic_clockevent_cpu_init(unsigned int cpu,
				    struct clock_event_device *cd)
{
	cd->name		= "MIPS GIC";
	cd->features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_C3STOP;

	cd->rating		= 350;
	cd->irq			= gic_timer_irq;
	cd->cpumask		= cpumask_of(cpu);
	cd->set_next_event	= gic_next_event;

	clockevents_config_and_register(cd, gic_frequency, 0x300, 0x7fffffff);

	enable_percpu_irq(gic_timer_irq, IRQ_TYPE_NONE);
}

static void gic_clockevent_cpu_exit(struct clock_event_device *cd)
{
	disable_percpu_irq(gic_timer_irq);
}

static void gic_update_frequency(void *data)
{
	unsigned long rate = (unsigned long)data;

	clockevents_update_freq(this_cpu_ptr(&gic_clockevent_device), rate);
}

static int gic_starting_cpu(unsigned int cpu)
{
	gic_clockevent_cpu_init(cpu, this_cpu_ptr(&gic_clockevent_device));
	return 0;
}

static int gic_clk_notifier(struct notifier_block *nb, unsigned long action,
			    void *data)
{
	struct clk_notifier_data *cnd = data;

	if (action == POST_RATE_CHANGE)
		on_each_cpu(gic_update_frequency, (void *)cnd->new_rate, 1);

	return NOTIFY_OK;
}

static int gic_dying_cpu(unsigned int cpu)
{
	gic_clockevent_cpu_exit(this_cpu_ptr(&gic_clockevent_device));
	return 0;
}

static struct notifier_block gic_clk_nb = {
	.notifier_call = gic_clk_notifier,
};

static int gic_clockevent_init(void)
{
	int ret;

	if (!gic_frequency)
		return -ENXIO;

	ret = setup_percpu_irq(gic_timer_irq, &gic_compare_irqaction);
	if (ret < 0) {
		pr_err("IRQ %d setup failed (%d)\n", gic_timer_irq, ret);
		return ret;
	}

	cpuhp_setup_state(CPUHP_AP_MIPS_GIC_TIMER_STARTING,
			  "clockevents/mips/gic/timer:starting",
			  gic_starting_cpu, gic_dying_cpu);
	return 0;
}

static u64 gic_hpt_read(struct clocksource *cs)
{
	return gic_read_count();
}

static struct clocksource gic_clocksource = {
	.name			= "GIC",
	.read			= gic_hpt_read,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
	.vdso_clock_mode	= VDSO_CLOCKMODE_GIC,
};

static int __init __gic_clocksource_init(void)
{
	unsigned int count_width;
	int ret;

	/* Set clocksource mask. */
	count_width = read_gic_config() & GIC_CONFIG_COUNTBITS;
	count_width >>= __ffs(GIC_CONFIG_COUNTBITS);
	count_width *= 4;
	count_width += 32;
	gic_clocksource.mask = CLOCKSOURCE_MASK(count_width);

	/* Calculate a somewhat reasonable rating value. */
	gic_clocksource.rating = 200 + gic_frequency / 10000000;

	ret = clocksource_register_hz(&gic_clocksource, gic_frequency);
	if (ret < 0)
		pr_warn("Unable to register clocksource\n");

	return ret;
}

static int __init gic_clocksource_of_init(struct device_node *node)
{
	struct clk *clk;
	int ret;

	if (!mips_gic_present() || !node->parent ||
	    !of_device_is_compatible(node->parent, "mti,gic")) {
		pr_warn("No DT definition\n");
		return -ENXIO;
	}

	clk = of_clk_get(node, 0);
	if (!IS_ERR(clk)) {
		ret = clk_prepare_enable(clk);
		if (ret < 0) {
			pr_err("Failed to enable clock\n");
			clk_put(clk);
			return ret;
		}

		gic_frequency = clk_get_rate(clk);
	} else if (of_property_read_u32(node, "clock-frequency",
					&gic_frequency)) {
		pr_err("Frequency not specified\n");
		return -EINVAL;
	}
	gic_timer_irq = irq_of_parse_and_map(node, 0);
	if (!gic_timer_irq) {
		pr_err("IRQ not specified\n");
		return -EINVAL;
	}

	ret = __gic_clocksource_init();
	if (ret)
		return ret;

	ret = gic_clockevent_init();
	if (!ret && !IS_ERR(clk)) {
		if (clk_notifier_register(clk, &gic_clk_nb) < 0)
			pr_warn("Unable to register clock notifier\n");
	}

	/* And finally start the counter */
	clear_gic_config(GIC_CONFIG_COUNTSTOP);

	return 0;
}
TIMER_OF_DECLARE(mips_gic_timer, "mti,gic-timer",
		       gic_clocksource_of_init);
