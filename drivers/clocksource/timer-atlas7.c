// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * System timer for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/cpu.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>

#define SIRFSOC_TIMER_32COUNTER_0_CTRL			0x0000
#define SIRFSOC_TIMER_32COUNTER_1_CTRL			0x0004
#define SIRFSOC_TIMER_MATCH_0				0x0018
#define SIRFSOC_TIMER_MATCH_1				0x001c
#define SIRFSOC_TIMER_COUNTER_0				0x0048
#define SIRFSOC_TIMER_COUNTER_1				0x004c
#define SIRFSOC_TIMER_INTR_STATUS			0x0060
#define SIRFSOC_TIMER_WATCHDOG_EN			0x0064
#define SIRFSOC_TIMER_64COUNTER_CTRL			0x0068
#define SIRFSOC_TIMER_64COUNTER_LO			0x006c
#define SIRFSOC_TIMER_64COUNTER_HI			0x0070
#define SIRFSOC_TIMER_64COUNTER_LOAD_LO			0x0074
#define SIRFSOC_TIMER_64COUNTER_LOAD_HI			0x0078
#define SIRFSOC_TIMER_64COUNTER_RLATCHED_LO		0x007c
#define SIRFSOC_TIMER_64COUNTER_RLATCHED_HI		0x0080

#define SIRFSOC_TIMER_REG_CNT 6

static unsigned long atlas7_timer_rate;

static const u32 sirfsoc_timer_reg_list[SIRFSOC_TIMER_REG_CNT] = {
	SIRFSOC_TIMER_WATCHDOG_EN,
	SIRFSOC_TIMER_32COUNTER_0_CTRL,
	SIRFSOC_TIMER_32COUNTER_1_CTRL,
	SIRFSOC_TIMER_64COUNTER_CTRL,
	SIRFSOC_TIMER_64COUNTER_RLATCHED_LO,
	SIRFSOC_TIMER_64COUNTER_RLATCHED_HI,
};

static u32 sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT];

static void __iomem *sirfsoc_timer_base;

/* disable count and interrupt */
static inline void sirfsoc_timer_count_disable(int idx)
{
	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL + 4 * idx) & ~0x7,
		sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL + 4 * idx);
}

/* enable count and interrupt */
static inline void sirfsoc_timer_count_enable(int idx)
{
	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL + 4 * idx) | 0x3,
		sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL + 4 * idx);
}

/* timer interrupt handler */
static irqreturn_t sirfsoc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;
	int cpu = smp_processor_id();

	/* clear timer interrupt */
	writel_relaxed(BIT(cpu), sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS);

	if (clockevent_state_oneshot(ce))
		sirfsoc_timer_count_disable(cpu);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

/* read 64-bit timer counter */
static u64 sirfsoc_timer_read(struct clocksource *cs)
{
	u64 cycles;

	writel_relaxed((readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
			BIT(0)) & ~BIT(1), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);

	cycles = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_RLATCHED_HI);
	cycles = (cycles << 32) | readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_RLATCHED_LO);

	return cycles;
}

static int sirfsoc_timer_set_next_event(unsigned long delta,
	struct clock_event_device *ce)
{
	int cpu = smp_processor_id();

	/* disable timer first, then modify the related registers */
	sirfsoc_timer_count_disable(cpu);

	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0 +
		4 * cpu);
	writel_relaxed(delta, sirfsoc_timer_base + SIRFSOC_TIMER_MATCH_0 +
		4 * cpu);

	/* enable the tick */
	sirfsoc_timer_count_enable(cpu);

	return 0;
}

/* Oneshot is enabled in set_next_event */
static int sirfsoc_timer_shutdown(struct clock_event_device *evt)
{
	sirfsoc_timer_count_disable(smp_processor_id());
	return 0;
}

static void sirfsoc_clocksource_suspend(struct clocksource *cs)
{
	int i;

	for (i = 0; i < SIRFSOC_TIMER_REG_CNT; i++)
		sirfsoc_timer_reg_val[i] = readl_relaxed(sirfsoc_timer_base + sirfsoc_timer_reg_list[i]);
}

static void sirfsoc_clocksource_resume(struct clocksource *cs)
{
	int i;

	for (i = 0; i < SIRFSOC_TIMER_REG_CNT - 2; i++)
		writel_relaxed(sirfsoc_timer_reg_val[i], sirfsoc_timer_base + sirfsoc_timer_reg_list[i]);

	writel_relaxed(sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT - 2],
		sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_LO);
	writel_relaxed(sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT - 1],
		sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_HI);

	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
		BIT(1) | BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
}

static struct clock_event_device __percpu *sirfsoc_clockevent;

static struct clocksource sirfsoc_clocksource = {
	.name = "sirfsoc_clocksource",
	.rating = 200,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.read = sirfsoc_timer_read,
	.suspend = sirfsoc_clocksource_suspend,
	.resume = sirfsoc_clocksource_resume,
};

static struct irqaction sirfsoc_timer_irq = {
	.name = "sirfsoc_timer0",
	.flags = IRQF_TIMER | IRQF_NOBALANCING,
	.handler = sirfsoc_timer_interrupt,
};

static struct irqaction sirfsoc_timer1_irq = {
	.name = "sirfsoc_timer1",
	.flags = IRQF_TIMER | IRQF_NOBALANCING,
	.handler = sirfsoc_timer_interrupt,
};

static int sirfsoc_local_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *ce = per_cpu_ptr(sirfsoc_clockevent, cpu);
	struct irqaction *action;

	if (cpu == 0)
		action = &sirfsoc_timer_irq;
	else
		action = &sirfsoc_timer1_irq;

	ce->irq = action->irq;
	ce->name = "local_timer";
	ce->features = CLOCK_EVT_FEAT_ONESHOT;
	ce->rating = 200;
	ce->set_state_shutdown = sirfsoc_timer_shutdown;
	ce->set_state_oneshot = sirfsoc_timer_shutdown;
	ce->tick_resume = sirfsoc_timer_shutdown;
	ce->set_next_event = sirfsoc_timer_set_next_event;
	clockevents_calc_mult_shift(ce, atlas7_timer_rate, 60);
	ce->max_delta_ns = clockevent_delta2ns(-2, ce);
	ce->max_delta_ticks = (unsigned long)-2;
	ce->min_delta_ns = clockevent_delta2ns(2, ce);
	ce->min_delta_ticks = 2;
	ce->cpumask = cpumask_of(cpu);

	action->dev_id = ce;
	BUG_ON(setup_irq(ce->irq, action));
	irq_force_affinity(action->irq, cpumask_of(cpu));

	clockevents_register_device(ce);
	return 0;
}

static int sirfsoc_local_timer_dying_cpu(unsigned int cpu)
{
	sirfsoc_timer_count_disable(1);

	if (cpu == 0)
		remove_irq(sirfsoc_timer_irq.irq, &sirfsoc_timer_irq);
	else
		remove_irq(sirfsoc_timer1_irq.irq, &sirfsoc_timer1_irq);
	return 0;
}

static int __init sirfsoc_clockevent_init(void)
{
	sirfsoc_clockevent = alloc_percpu(struct clock_event_device);
	BUG_ON(!sirfsoc_clockevent);

	/* Install and invoke hotplug callbacks */
	return cpuhp_setup_state(CPUHP_AP_MARCO_TIMER_STARTING,
				 "clockevents/marco:starting",
				 sirfsoc_local_timer_starting_cpu,
				 sirfsoc_local_timer_dying_cpu);
}

/* initialize the kernel jiffy timer source */
static int __init sirfsoc_atlas7_timer_init(struct device_node *np)
{
	struct clk *clk;

	clk = of_clk_get(np, 0);
	BUG_ON(IS_ERR(clk));

	BUG_ON(clk_prepare_enable(clk));

	atlas7_timer_rate = clk_get_rate(clk);

	/* timer dividers: 0, not divided */
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_1_CTRL);

	/* Initialize timer counters to 0 */
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_LO);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_HI);
	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
		BIT(1) | BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_1);

	/* Clear all interrupts */
	writel_relaxed(0xFFFF, sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS);

	BUG_ON(clocksource_register_hz(&sirfsoc_clocksource, atlas7_timer_rate));

	return sirfsoc_clockevent_init();
}

static int __init sirfsoc_of_timer_init(struct device_node *np)
{
	sirfsoc_timer_base = of_iomap(np, 0);
	if (!sirfsoc_timer_base) {
		pr_err("unable to map timer cpu registers\n");
		return -ENXIO;
	}

	sirfsoc_timer_irq.irq = irq_of_parse_and_map(np, 0);
	if (!sirfsoc_timer_irq.irq) {
		pr_err("No irq passed for timer0 via DT\n");
		return -EINVAL;
	}

	sirfsoc_timer1_irq.irq = irq_of_parse_and_map(np, 1);
	if (!sirfsoc_timer1_irq.irq) {
		pr_err("No irq passed for timer1 via DT\n");
		return -EINVAL;
	}

	return sirfsoc_atlas7_timer_init(np);
}
TIMER_OF_DECLARE(sirfsoc_atlas7_timer, "sirf,atlas7-tick", sirfsoc_of_timer_init);
