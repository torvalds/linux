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
#include <linux/cpu.h>
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

#include <asm/smp_plat.h>
#include <asm/smp_twd.h>

/* set up by the platform code */
static void __iomem *twd_base;

static struct clk *twd_clk;
static unsigned long twd_timer_rate;
static DEFINE_PER_CPU(bool, percpu_setup_called);

static struct clock_event_device __percpu *twd_evt;
static int twd_ppi;

static int twd_shutdown(struct clock_event_device *clk)
{
	writel_relaxed(0, twd_base + TWD_TIMER_CONTROL);
	return 0;
}

static int twd_set_oneshot(struct clock_event_device *clk)
{
	/* period set, and timer enabled in 'next_event' hook */
	writel_relaxed(TWD_TIMER_CONTROL_IT_ENABLE | TWD_TIMER_CONTROL_ONESHOT,
		       twd_base + TWD_TIMER_CONTROL);
	return 0;
}

static int twd_set_periodic(struct clock_event_device *clk)
{
	unsigned long ctrl = TWD_TIMER_CONTROL_ENABLE |
			     TWD_TIMER_CONTROL_IT_ENABLE |
			     TWD_TIMER_CONTROL_PERIODIC;

	writel_relaxed(DIV_ROUND_CLOSEST(twd_timer_rate, HZ),
		       twd_base + TWD_TIMER_LOAD);
	writel_relaxed(ctrl, twd_base + TWD_TIMER_CONTROL);
	return 0;
}

static int twd_set_next_event(unsigned long evt,
			struct clock_event_device *unused)
{
	unsigned long ctrl = readl_relaxed(twd_base + TWD_TIMER_CONTROL);

	ctrl |= TWD_TIMER_CONTROL_ENABLE;

	writel_relaxed(evt, twd_base + TWD_TIMER_COUNTER);
	writel_relaxed(ctrl, twd_base + TWD_TIMER_CONTROL);

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
	if (readl_relaxed(twd_base + TWD_TIMER_INTSTAT)) {
		writel_relaxed(1, twd_base + TWD_TIMER_INTSTAT);
		return 1;
	}

	return 0;
}

static void twd_timer_stop(void)
{
	struct clock_event_device *clk = raw_cpu_ptr(twd_evt);

	twd_shutdown(clk);
	disable_percpu_irq(clk->irq);
}

#ifdef CONFIG_COMMON_CLK

/*
 * Updates clockevent frequency when the cpu frequency changes.
 * Called on the cpu that is changing frequency with interrupts disabled.
 */
static void twd_update_frequency(void *new_rate)
{
	twd_timer_rate = *((unsigned long *) new_rate);

	clockevents_update_freq(raw_cpu_ptr(twd_evt), twd_timer_rate);
}

static int twd_rate_change(struct notifier_block *nb,
	unsigned long flags, void *data)
{
	struct clk_notifier_data *cnd = data;

	/*
	 * The twd clock events must be reprogrammed to account for the new
	 * frequency.  The timer is local to a cpu, so cross-call to the
	 * changing cpu.
	 */
	if (flags == POST_RATE_CHANGE)
		on_each_cpu(twd_update_frequency,
				  (void *)&cnd->new_rate, 1);

	return NOTIFY_OK;
}

static struct notifier_block twd_clk_nb = {
	.notifier_call = twd_rate_change,
};

static int twd_clk_init(void)
{
	if (twd_evt && raw_cpu_ptr(twd_evt) && !IS_ERR(twd_clk))
		return clk_notifier_register(twd_clk, &twd_clk_nb);

	return 0;
}
core_initcall(twd_clk_init);

#elif defined (CONFIG_CPU_FREQ)

#include <linux/cpufreq.h>

/*
 * Updates clockevent frequency when the cpu frequency changes.
 * Called on the cpu that is changing frequency with interrupts disabled.
 */
static void twd_update_frequency(void *data)
{
	twd_timer_rate = clk_get_rate(twd_clk);

	clockevents_update_freq(raw_cpu_ptr(twd_evt), twd_timer_rate);
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
	if (state == CPUFREQ_POSTCHANGE)
		smp_call_function_single(freqs->cpu, twd_update_frequency,
			NULL, 1);

	return NOTIFY_OK;
}

static struct notifier_block twd_cpufreq_nb = {
	.notifier_call = twd_cpufreq_transition,
};

static int twd_cpufreq_init(void)
{
	if (twd_evt && raw_cpu_ptr(twd_evt) && !IS_ERR(twd_clk))
		return cpufreq_register_notifier(&twd_cpufreq_nb,
			CPUFREQ_TRANSITION_NOTIFIER);

	return 0;
}
core_initcall(twd_cpufreq_init);

#endif

static void twd_calibrate_rate(void)
{
	unsigned long count;
	u64 waitjiffies;

	/*
	 * If this is the first time round, we need to work out how fast
	 * the timer ticks
	 */
	if (twd_timer_rate == 0) {
		pr_info("Calibrating local timer... ");

		/* Wait for a tick to start */
		waitjiffies = get_jiffies_64() + 1;

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		/* OK, now the tick has started, let's get the timer going */
		waitjiffies += 5;

				 /* enable, no interrupt or reload */
		writel_relaxed(0x1, twd_base + TWD_TIMER_CONTROL);

				 /* maximum value */
		writel_relaxed(0xFFFFFFFFU, twd_base + TWD_TIMER_COUNTER);

		while (get_jiffies_64() < waitjiffies)
			udelay(10);

		count = readl_relaxed(twd_base + TWD_TIMER_COUNTER);

		twd_timer_rate = (0xFFFFFFFFU - count) * (HZ / 5);

		pr_cont("%lu.%02luMHz.\n", twd_timer_rate / 1000000,
			(twd_timer_rate / 10000) % 100);
	}
}

static irqreturn_t twd_handler(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	if (twd_timer_ack()) {
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static void twd_get_clock(struct device_node *np)
{
	int err;

	if (np)
		twd_clk = of_clk_get(np, 0);
	else
		twd_clk = clk_get_sys("smp_twd", NULL);

	if (IS_ERR(twd_clk)) {
		pr_err("smp_twd: clock not found %d\n", (int) PTR_ERR(twd_clk));
		return;
	}

	err = clk_prepare_enable(twd_clk);
	if (err) {
		pr_err("smp_twd: clock failed to prepare+enable: %d\n", err);
		clk_put(twd_clk);
		return;
	}

	twd_timer_rate = clk_get_rate(twd_clk);
}

/*
 * Setup the local clock events for a CPU.
 */
static void twd_timer_setup(void)
{
	struct clock_event_device *clk = raw_cpu_ptr(twd_evt);
	int cpu = smp_processor_id();

	/*
	 * If the basic setup for this CPU has been done before don't
	 * bother with the below.
	 */
	if (per_cpu(percpu_setup_called, cpu)) {
		writel_relaxed(0, twd_base + TWD_TIMER_CONTROL);
		clockevents_register_device(clk);
		enable_percpu_irq(clk->irq, 0);
		return;
	}
	per_cpu(percpu_setup_called, cpu) = true;

	twd_calibrate_rate();

	/*
	 * The following is done once per CPU the first time .setup() is
	 * called.
	 */
	writel_relaxed(0, twd_base + TWD_TIMER_CONTROL);

	clk->name = "local_timer";
	clk->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
			CLOCK_EVT_FEAT_C3STOP;
	clk->rating = 350;
	clk->set_state_shutdown = twd_shutdown;
	clk->set_state_periodic = twd_set_periodic;
	clk->set_state_oneshot = twd_set_oneshot;
	clk->tick_resume = twd_shutdown;
	clk->set_next_event = twd_set_next_event;
	clk->irq = twd_ppi;
	clk->cpumask = cpumask_of(cpu);

	clockevents_config_and_register(clk, twd_timer_rate,
					0xf, 0xffffffff);
	enable_percpu_irq(clk->irq, 0);
}

static int twd_timer_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		twd_timer_setup();
		break;
	case CPU_DYING:
		twd_timer_stop();
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block twd_timer_cpu_nb = {
	.notifier_call = twd_timer_cpu_notify,
};

static int __init twd_local_timer_common_register(struct device_node *np)
{
	int err;

	twd_evt = alloc_percpu(struct clock_event_device);
	if (!twd_evt) {
		err = -ENOMEM;
		goto out_free;
	}

	err = request_percpu_irq(twd_ppi, twd_handler, "twd", twd_evt);
	if (err) {
		pr_err("twd: can't register interrupt %d (%d)\n", twd_ppi, err);
		goto out_free;
	}

	err = register_cpu_notifier(&twd_timer_cpu_nb);
	if (err)
		goto out_irq;

	twd_get_clock(np);

	/*
	 * Immediately configure the timer on the boot CPU, unless we need
	 * jiffies to be incrementing to calibrate the rate in which case
	 * setup the timer in late_time_init.
	 */
	if (twd_timer_rate)
		twd_timer_setup();
	else
		late_time_init = twd_timer_setup;

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

	return twd_local_timer_common_register(NULL);
}

#ifdef CONFIG_OF
static void __init twd_local_timer_of_register(struct device_node *np)
{
	int err;

	if (!is_smp() || !setup_max_cpus)
		return;

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

	err = twd_local_timer_common_register(np);

out:
	WARN(err, "twd_local_timer_of_register failed (%d)\n", err);
}
CLOCKSOURCE_OF_DECLARE(arm_twd_a9, "arm,cortex-a9-twd-timer", twd_local_timer_of_register);
CLOCKSOURCE_OF_DECLARE(arm_twd_a5, "arm,cortex-a5-twd-timer", twd_local_timer_of_register);
CLOCKSOURCE_OF_DECLARE(arm_twd_11mp, "arm,arm11mp-twd-timer", twd_local_timer_of_register);
#endif
