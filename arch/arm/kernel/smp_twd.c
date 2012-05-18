/*
 *  linux/arch/arm/kernel/smp_twd.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/smp.h>
#include <linux/jiffies.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/smp_twd.h>
#include <asm/localtimer.h>
#include <asm/hardware/gic.h>

/* set up by the platform code */
static void __iomem *twd_base;

static struct clk *twd_clk;
static unsigned long twd_timer_rate;

static struct clock_event_device __percpu **twd_evt;
static int twd_ppi;

static void twd_set_mode(enum clock_event_mode mode,
			struct clock_event_device *clk)
{
	unsigned long ctrl;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* timer load already set up */
		ctrl = TWD_TIMER_CONTROL_ENABLE | TWD_TIMER_CONTROL_IT_ENABLE
			| TWD_TIMER_CONTROL_PERIODIC;
		__raw_writel(twd_timer_rate / HZ, twd_base + TWD_TIMER_LOAD);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* period set, and timer enabled in 'next_event' hook */
		ctrl = TWD_TIMER_CONTROL_IT_ENABLE | TWD_TIMER_CONTROL_ONESHOT;
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		ctrl = 0;
	}

	__raw_writel(ctrl, twd_base + TWD_TIMER_CONTROL);
}

static int twd_set_next_event(unsigned long evt,
			struct clock_event_device *unused)
{
	unsigned long ctrl = __raw_readl(twd_base + TWD_TIMER_CONTROL);

	ctrl |= TWD_TIMER_CONTROL_ENABLE;

	__raw_writel(evt, twd_base + TWD_TIMER_COUNTER);
	__raw_writel(ctrl, twd_base + TWD_TIMER_CONTROL);

	return 0;
}

/*
 * local_timer_ack: checks for a local timer interrupt.
 *
 * If a local timer interrupt has occurred, acknowledge and return 1.
 * Otherwise, return 0.
 */
static int twd_timer_ack(void)
{
	if (__raw_readl(twd_base + TWD_TIMER_INTSTAT)) {
		__raw_writel(1, twd_base + TWD_TIMER_INTSTAT);
		return 1;
	}

	return 0;
}

static void twd_timer_stop(struct clock_event_device *clk)
{
	twd_set_mode(CLOCK_EVT_MODE_UNUSED, clk);
	disable_percpu_irq(clk->irq);
}

#ifdef CONFIG_CPU_FREQ

/*
 * Updates clockevent frequency when the cpu frequency changes.
 * Called on the cpu that is changing frequency with interrupts disabled.
 */
static void twd_update_frequency(void *data)
{
	twd_timer_rate = clk_get_rate(twd_clk);

	clockevents_update_freq(*__this_cpu_ptr(twd_evt), twd_timer_rate);
}

static int twd_cpufreq_transition(struct notifier_block *nb,
	unsigned long state, void *data)
{
	struct cpufreq_freqs *freqs = data;

	/*
	 * The twd clock events must be reprogrammed to account for the new
	 * frequency.  The timer is local to a cpu, so cross-call to the
	 * changing cpu.
	 */
	if (state == CPUFREQ_POSTCHANGE || state == CPUFREQ_RESUMECHANGE)
		smp_call_function_single(freqs->cpu, twd_update_frequency,
			NULL, 1);

	return NOTIFY_OK;
}

static struct notifier_block twd_cpufreq_nb = {
	.notifier_call = twd_cpufreq_transition,
};

static int twd_cpufreq_init(void)
{
	if (twd_evt && *__this_cpu_ptr(twd_evt) && !IS_ERR(twd_clk))
		return cpufreq_register_notifier(&twd_cpufreq_nb,
			CPUFREQ_TRANSITION_NOTIFIER);

	return 0;
}
core_initcall(twd_cpufreq_init);

#endif

static void __cpuinit twd_calibrate_rate(void)
{
	unsigned long count;
	u64 waitjiffies;

	/*
	 * If this is the first time round, we need to work out how fast
	 * the timer ticks
	 */
	if (twd_timer_rate == 0) {
		printk(KERN_INFO "Calibrating local timer... ");

		/* Wait for a tick to start */
		waitjiffies = get_jiffies_64() + 1;

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		/* OK, now the tick has started, let's get the timer going */
		waitjiffies += 5;

				 /* enable, no interrupt or reload */
		__raw_writel(0x1, twd_base + TWD_TIMER_CONTROL);

				 /* maximum value */
		__raw_writel(0xFFFFFFFFU, twd_base + TWD_TIMER_COUNTER);

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		count = __raw_readl(twd_base + TWD_TIMER_COUNTER);

		twd_timer_rate = (0xFFFFFFFFU - count) * (HZ / 5);

		printk("%lu.%02luMHz.\n", twd_timer_rate / 1000000,
			(twd_timer_rate / 10000) % 100);
	}
}

static irqreturn_t twd_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;

	if (twd_timer_ack()) {
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct clk *twd_get_clock(void)
{
	struct clk *clk;
	int err;

	clk = clk_get_sys("smp_twd", NULL);
	if (IS_ERR(clk)) {
		pr_err("smp_twd: clock not found: %d\n", (int)PTR_ERR(clk));
		return clk;
	}

	err = clk_prepare(clk);
	if (err) {
		pr_err("smp_twd: clock failed to prepare: %d\n", err);
		clk_put(clk);
		return ERR_PTR(err);
	}

	err = clk_enable(clk);
	if (err) {
		pr_err("smp_twd: clock failed to enable: %d\n", err);
		clk_unprepare(clk);
		clk_put(clk);
		return ERR_PTR(err);
	}

	return clk;
}

/*
 * Setup the local clock events for a CPU.
 */
static int __cpuinit twd_timer_setup(struct clock_event_device *clk)
{
	struct clock_event_device **this_cpu_clk;

	if (!twd_clk)
		twd_clk = twd_get_clock();

	if (!IS_ERR_OR_NULL(twd_clk))
		twd_timer_rate = clk_get_rate(twd_clk);
	else
		twd_calibrate_rate();

	__raw_writel(0, twd_base + TWD_TIMER_CONTROL);

	clk->name = "local_timer";
	clk->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
			CLOCK_EVT_FEAT_C3STOP;
	clk->rating = 350;
	clk->set_mode = twd_set_mode;
	clk->set_next_event = twd_set_next_event;
	clk->irq = twd_ppi;

	this_cpu_clk = __this_cpu_ptr(twd_evt);
	*this_cpu_clk = clk;

	clockevents_config_and_register(clk, twd_timer_rate,
					0xf, 0xffffffff);
	enable_percpu_irq(clk->irq, 0);

	return 0;
}

static struct local_timer_ops twd_lt_ops __cpuinitdata = {
	.setup	= twd_timer_setup,
	.stop	= twd_timer_stop,
};

static int __init twd_local_timer_common_register(void)
{
	int err;

	twd_evt = alloc_percpu(struct clock_event_device *);
	if (!twd_evt) {
		err = -ENOMEM;
		goto out_free;
	}

	err = request_percpu_irq(twd_ppi, twd_handler, "twd", twd_evt);
	if (err) {
		pr_err("twd: can't register interrupt %d (%d)\n", twd_ppi, err);
		goto out_free;
	}

	err = local_timer_register(&twd_lt_ops);
	if (err)
		goto out_irq;

	return 0;

out_irq:
	free_percpu_irq(twd_ppi, twd_evt);
out_free:
	iounmap(twd_base);
	twd_base = NULL;
	free_percpu(twd_evt);

	return err;
}

int __init twd_local_timer_register(struct twd_local_timer *tlt)
{
	if (twd_base || twd_evt)
		return -EBUSY;

	twd_ppi	= tlt->res[1].start;

	twd_base = ioremap(tlt->res[0].start, resource_size(&tlt->res[0]));
	if (!twd_base)
		return -ENOMEM;

	return twd_local_timer_common_register();
}

#ifdef CONFIG_OF
const static struct of_device_id twd_of_match[] __initconst = {
	{ .compatible = "arm,cortex-a9-twd-timer",	},
	{ .compatible = "arm,cortex-a5-twd-timer",	},
	{ .compatible = "arm,arm11mp-twd-timer",	},
	{ },
};

void __init twd_local_timer_of_register(void)
{
	struct device_node *np;
	int err;

	np = of_find_matching_node(NULL, twd_of_match);
	if (!np) {
		err = -ENODEV;
		goto out;
	}

	twd_ppi = irq_of_parse_and_map(np, 0);
	if (!twd_ppi) {
		err = -EINVAL;
		goto out;
	}

	twd_base = of_iomap(np, 0);
	if (!twd_base) {
		err = -ENOMEM;
		goto out;
	}

	err = twd_local_timer_common_register();

out:
	WARN(err, "twd_local_timer_of_register failed (%d)\n", err);
}
#endif
