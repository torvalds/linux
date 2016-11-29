/*
 * drivers/clocksource/arm_global_timer.c
 *
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited.
 * Author: Stuart Menefy <stuart.menefy@st.com>
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>

#include <asm/cputype.h>

#define GT_COUNTER0	0x00
#define GT_COUNTER1	0x04

#define GT_CONTROL	0x08
#define GT_CONTROL_TIMER_ENABLE		BIT(0)  /* this bit is NOT banked */
#define GT_CONTROL_COMP_ENABLE		BIT(1)	/* banked */
#define GT_CONTROL_IRQ_ENABLE		BIT(2)	/* banked */
#define GT_CONTROL_AUTO_INC		BIT(3)	/* banked */

#define GT_INT_STATUS	0x0c
#define GT_INT_STATUS_EVENT_FLAG	BIT(0)

#define GT_COMP0	0x10
#define GT_COMP1	0x14
#define GT_AUTO_INC	0x18

/*
 * We are expecting to be clocked by the ARM peripheral clock.
 *
 * Note: it is assumed we are using a prescaler value of zero, so this is
 * the units for all operations.
 */
static void __iomem *gt_base;
static struct clk *gt_clk;
static unsigned long gt_clk_rate;
static int gt_ppi;
static struct clock_event_device __percpu *gt_evt;

/*
 * To get the value from the Global Timer Counter register proceed as follows:
 * 1. Read the upper 32-bit timer counter register
 * 2. Read the lower 32-bit timer counter register
 * 3. Read the upper 32-bit timer counter register again. If the value is
 *  different to the 32-bit upper value read previously, go back to step 2.
 *  Otherwise the 64-bit timer counter value is correct.
 */
static u64 notrace _gt_counter_read(void)
{
	u64 counter;
	u32 lower;
	u32 upper, old_upper;

	upper = readl_relaxed(gt_base + GT_COUNTER1);
	do {
		old_upper = upper;
		lower = readl_relaxed(gt_base + GT_COUNTER0);
		upper = readl_relaxed(gt_base + GT_COUNTER1);
	} while (upper != old_upper);

	counter = upper;
	counter <<= 32;
	counter |= lower;
	return counter;
}

static u64 gt_counter_read(void)
{
	return _gt_counter_read();
}

/**
 * To ensure that updates to comparator value register do not set the
 * Interrupt Status Register proceed as follows:
 * 1. Clear the Comp Enable bit in the Timer Control Register.
 * 2. Write the lower 32-bit Comparator Value Register.
 * 3. Write the upper 32-bit Comparator Value Register.
 * 4. Set the Comp Enable bit and, if necessary, the IRQ enable bit.
 */
static void gt_compare_set(unsigned long delta, int periodic)
{
	u64 counter = gt_counter_read();
	unsigned long ctrl;

	counter += delta;
	ctrl = GT_CONTROL_TIMER_ENABLE;
	writel_relaxed(ctrl, gt_base + GT_CONTROL);
	writel_relaxed(lower_32_bits(counter), gt_base + GT_COMP0);
	writel_relaxed(upper_32_bits(counter), gt_base + GT_COMP1);

	if (periodic) {
		writel_relaxed(delta, gt_base + GT_AUTO_INC);
		ctrl |= GT_CONTROL_AUTO_INC;
	}

	ctrl |= GT_CONTROL_COMP_ENABLE | GT_CONTROL_IRQ_ENABLE;
	writel_relaxed(ctrl, gt_base + GT_CONTROL);
}

static int gt_clockevent_shutdown(struct clock_event_device *evt)
{
	unsigned long ctrl;

	ctrl = readl(gt_base + GT_CONTROL);
	ctrl &= ~(GT_CONTROL_COMP_ENABLE | GT_CONTROL_IRQ_ENABLE |
		  GT_CONTROL_AUTO_INC);
	writel(ctrl, gt_base + GT_CONTROL);
	return 0;
}

static int gt_clockevent_set_periodic(struct clock_event_device *evt)
{
	gt_compare_set(DIV_ROUND_CLOSEST(gt_clk_rate, HZ), 1);
	return 0;
}

static int gt_clockevent_set_next_event(unsigned long evt,
					struct clock_event_device *unused)
{
	gt_compare_set(evt, 0);
	return 0;
}

#ifdef CONFIG_COMMON_CLK

/*
 * Updates clockevent frequency when the cpu frequency changes.
 * Called on the cpu that is changing frequency with interrupts disabled.
 */
static void gt_update_frequency(void *new_rate)
{
	gt_clk_rate = *((unsigned long *) new_rate);

	clockevents_update_freq(raw_cpu_ptr(gt_evt), gt_clk_rate);
}

static int gt_rate_change(struct notifier_block *nb,
	unsigned long flags, void *data)
{
	struct clk_notifier_data *cnd = data;

	/*
	 * The gt clock events must be reprogrammed to account for the new
	 * frequency.  The timer is local to a cpu, so cross-call to the
	 * changing cpu.
	 */
	if (flags == POST_RATE_CHANGE)
		on_each_cpu(gt_update_frequency,
				  (void *)&cnd->new_rate, 1);

	return NOTIFY_OK;
}

static struct notifier_block gt_clk_nb = {
	.notifier_call = gt_rate_change,
};

static int gt_clk_init(void)
{
	if (gt_evt && raw_cpu_ptr(gt_evt) && !IS_ERR(gt_clk))
		return clk_notifier_register(gt_clk, &gt_clk_nb);

	return 0;
}
core_initcall(gt_clk_init);

#elif defined (CONFIG_CPU_FREQ)

#include <linux/cpufreq.h>

/*
 * Updates clockevent frequency when the cpu frequency changes.
 * Called on the cpu that is changing frequency with interrupts disabled.
 */
static void gt_update_frequency(void *data)
{
	gt_clk_rate = clk_get_rate(gt_clk);

	clockevents_update_freq(raw_cpu_ptr(gt_evt), gt_clk_rate);
}

static int gt_cpufreq_transition(struct notifier_block *nb,
	unsigned long state, void *data)
{
	struct cpufreq_freqs *freqs = data;

	/*
	 * The gt clock events must be reprogrammed to account for the new
	 * frequency.  The timer is local to a cpu, so cross-call to the
	 * changing cpu.
	 */
	if (state == CPUFREQ_POSTCHANGE)
		smp_call_function_single(freqs->cpu, gt_update_frequency,
			NULL, 1);

	return NOTIFY_OK;
}

static struct notifier_block gt_cpufreq_nb = {
	.notifier_call = gt_cpufreq_transition,
};

static int gt_cpufreq_init(void)
{
	if (gt_evt && raw_cpu_ptr(gt_evt) && !IS_ERR(gt_clk))
		return cpufreq_register_notifier(&gt_cpufreq_nb,
			CPUFREQ_TRANSITION_NOTIFIER);

	return 0;
}
core_initcall(gt_cpufreq_init);

#endif

static irqreturn_t gt_clockevent_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	if (!(readl_relaxed(gt_base + GT_INT_STATUS) &
				GT_INT_STATUS_EVENT_FLAG))
		return IRQ_NONE;

	/**
	 * ERRATA 740657( Global Timer can send 2 interrupts for
	 * the same event in single-shot mode)
	 * Workaround:
	 *	Either disable single-shot mode.
	 *	Or
	 *	Modify the Interrupt Handler to avoid the
	 *	offending sequence. This is achieved by clearing
	 *	the Global Timer flag _after_ having incremented
	 *	the Comparator register	value to a higher value.
	 */
	if (clockevent_state_oneshot(evt))
		gt_compare_set(ULONG_MAX, 0);

	writel_relaxed(GT_INT_STATUS_EVENT_FLAG, gt_base + GT_INT_STATUS);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int gt_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(gt_evt);

	clk->name = "arm_global_timer";
	clk->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT |
		CLOCK_EVT_FEAT_PERCPU;
	clk->set_state_shutdown = gt_clockevent_shutdown;
	clk->set_state_periodic = gt_clockevent_set_periodic;
	clk->set_state_oneshot = gt_clockevent_shutdown;
	clk->set_state_oneshot_stopped = gt_clockevent_shutdown;
	clk->set_next_event = gt_clockevent_set_next_event;
	clk->cpumask = cpumask_of(cpu);
	clk->rating = 300;
	clk->irq = gt_ppi;
	clockevents_config_and_register(clk, gt_clk_rate,
					1, 0xffffffff);
	enable_percpu_irq(clk->irq, IRQ_TYPE_NONE);
	return 0;
}

static int gt_dying_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(gt_evt);

	gt_clockevent_shutdown(clk);
	disable_percpu_irq(clk->irq);
	return 0;
}

static u64 gt_clocksource_read(struct clocksource *cs)
{
	return gt_counter_read();
}

static void gt_resume(struct clocksource *cs)
{
	unsigned long ctrl;

	ctrl = readl(gt_base + GT_CONTROL);
	if (!(ctrl & GT_CONTROL_TIMER_ENABLE))
		/* re-enable timer on resume */
		writel(GT_CONTROL_TIMER_ENABLE, gt_base + GT_CONTROL);
}

static struct clocksource gt_clocksource = {
	.name	= "arm_global_timer",
	.rating	= 300,
	.read	= gt_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume = gt_resume,
};

#ifdef CONFIG_CLKSRC_ARM_GLOBAL_TIMER_SCHED_CLOCK
static u64 notrace gt_sched_clock_read(void)
{
	return _gt_counter_read();
}
#endif

static unsigned long gt_read_long(void)
{
	return readl_relaxed(gt_base + GT_COUNTER0);
}

static struct delay_timer gt_delay_timer = {
	.read_current_timer = gt_read_long,
};

static void __init gt_delay_timer_init(void)
{
	gt_delay_timer.freq = gt_clk_rate;
	register_current_timer_delay(&gt_delay_timer);
}

static int __init gt_clocksource_init(void)
{
	writel(0, gt_base + GT_CONTROL);
	writel(0, gt_base + GT_COUNTER0);
	writel(0, gt_base + GT_COUNTER1);
	/* enables timer on all the cores */
	writel(GT_CONTROL_TIMER_ENABLE, gt_base + GT_CONTROL);

#ifdef CONFIG_CLKSRC_ARM_GLOBAL_TIMER_SCHED_CLOCK
	sched_clock_register(gt_sched_clock_read, 64, gt_clk_rate);
#endif
	return clocksource_register_hz(&gt_clocksource, gt_clk_rate);
}

static int __init global_timer_of_register(struct device_node *np)
{
	int err = 0;

	/*
	 * In A9 r2p0 the comparators for each processor with the global timer
	 * fire when the timer value is greater than or equal to. In previous
	 * revisions the comparators fired when the timer value was equal to.
	 */
	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9
	    && (read_cpuid_id() & 0xf0000f) < 0x200000) {
		pr_warn("global-timer: non support for this cpu version.\n");
		return -ENOSYS;
	}

	gt_ppi = irq_of_parse_and_map(np, 0);
	if (!gt_ppi) {
		pr_warn("global-timer: unable to parse irq\n");
		return -EINVAL;
	}

	gt_base = of_iomap(np, 0);
	if (!gt_base) {
		pr_warn("global-timer: invalid base address\n");
		return -ENXIO;
	}

	gt_clk = of_clk_get(np, 0);
	if (!IS_ERR(gt_clk)) {
		err = clk_prepare_enable(gt_clk);
		if (err)
			goto out_unmap;
	} else {
		pr_warn("global-timer: clk not found\n");
		err = -EINVAL;
		goto out_unmap;
	}

	gt_clk_rate = clk_get_rate(gt_clk);
	gt_evt = alloc_percpu(struct clock_event_device);
	if (!gt_evt) {
		pr_warn("global-timer: can't allocate memory\n");
		err = -ENOMEM;
		goto out_clk;
	}

	err = request_percpu_irq(gt_ppi, gt_clockevent_interrupt,
				 "gt", gt_evt);
	if (err) {
		pr_warn("global-timer: can't register interrupt %d (%d)\n",
			gt_ppi, err);
		goto out_free;
	}

	/* Register and immediately configure the timer on the boot CPU */
	err = gt_clocksource_init();
	if (err)
		goto out_irq;
	
	err = cpuhp_setup_state(CPUHP_AP_ARM_GLOBAL_TIMER_STARTING,
				"clockevents/arm/global_timer:starting",
				gt_starting_cpu, gt_dying_cpu);
	if (err)
		goto out_irq;

	gt_delay_timer_init();

	return 0;

out_irq:
	free_percpu_irq(gt_ppi, gt_evt);
out_free:
	free_percpu(gt_evt);
out_clk:
	clk_disable_unprepare(gt_clk);
out_unmap:
	iounmap(gt_base);
	WARN(err, "ARM Global timer register failed (%d)\n", err);

	return err;
}

/* Only tested on r2p2 and r3p0  */
TIMER_OF_DECLARE(arm_gt, "arm,cortex-a9-global-timer",
			global_timer_of_register);
