/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/percpu.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <asm/localtimer.h>
#include <asm/sched_clock.h>

#define TIMER_NAME "rk_timer"

#define TIMER_LOAD_COUNT0               0x00
#define TIMER_LOAD_COUNT1               0x04
#define TIMER_CURRENT_VALUE0            0x08
#define TIMER_CURRENT_VALUE1            0x0c
#define TIMER_CONTROL_REG               0x10
#define TIMER_INT_STATUS                0x18

#define TIMER_DISABLE                   (0 << 0)
#define TIMER_ENABLE                    (1 << 0)
#define TIMER_MODE_FREE_RUNNING         (0 << 1)
#define TIMER_MODE_USER_DEFINED_COUNT   (1 << 1)
#define TIMER_INT_MASK                  (0 << 2)
#define TIMER_INT_UNMASK                (1 << 2)

struct cs_timer {
	void __iomem *base;
	struct clk *clk;
	struct clk *pclk;
};

struct ce_timer {
	void __iomem *base;
	struct clk *clk;
	struct clk *pclk;
	struct irqaction irq;
	char name[16];
};

static struct cs_timer cs_timer;
static DEFINE_PER_CPU(struct ce_timer, ce_timer);

static inline void rk_timer_disable(void __iomem *base)
{
	writel_relaxed(TIMER_DISABLE, base + TIMER_CONTROL_REG);
	dsb();
}

static inline void rk_timer_enable(void __iomem *base, u32 flags)
{
	writel_relaxed(TIMER_ENABLE | flags, base + TIMER_CONTROL_REG);
	dsb();
}

static inline u32 rk_timer_read_current_value(void __iomem *base)
{
	return readl_relaxed(base + TIMER_CURRENT_VALUE0);
}

static inline u32 rk_timer_read_current_value64(void __iomem *base)
{
	u32 upper, lower;

	do {
		upper = readl_relaxed(base + TIMER_CURRENT_VALUE1);
		lower = readl_relaxed(base + TIMER_CURRENT_VALUE0);
	} while (upper != readl_relaxed(base + TIMER_CURRENT_VALUE1));

	return ((u64) upper << 32) + lower;
}

static int rk_timer_set_next_event(unsigned long cycles, struct clock_event_device *ce)
{
	void __iomem *base = __get_cpu_var(ce_timer).base;

	rk_timer_disable(base);
	writel_relaxed(cycles, base + TIMER_LOAD_COUNT0);
	writel_relaxed(0, base + TIMER_LOAD_COUNT1);
	dsb();
	rk_timer_enable(base, TIMER_MODE_USER_DEFINED_COUNT | TIMER_INT_UNMASK);
	return 0;
}

static void rk_timer_set_mode(enum clock_event_mode mode, struct clock_event_device *ce)
{
	struct ce_timer *timer = &__get_cpu_var(ce_timer);
	void __iomem *base = timer->base;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		rk_timer_disable(base);
		writel_relaxed(24000000 / HZ - 1, base + TIMER_LOAD_COUNT0);
		dsb();
		rk_timer_enable(base, TIMER_MODE_FREE_RUNNING | TIMER_INT_UNMASK);
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		rk_timer_disable(base);
		break;
	}
}

static irqreturn_t rk_timer_clockevent_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;
	struct ce_timer *timer = &__get_cpu_var(ce_timer);
	void __iomem *base = timer->base;

	/* clear interrupt */
	writel_relaxed(1, base + TIMER_INT_STATUS);
	if (ce->mode == CLOCK_EVT_MODE_ONESHOT) {
		writel_relaxed(TIMER_DISABLE, base + TIMER_CONTROL_REG);
	}
	dsb();

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

static __cpuinit int rk_timer_init_clockevent(struct clock_event_device *ce, unsigned int cpu)
{
	struct ce_timer *timer = &per_cpu(ce_timer, cpu);
	struct irqaction *irq = &timer->irq;
	void __iomem *base = timer->base;

	if (!base)
		return 0;

	ce->name = timer->name;
	ce->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	ce->set_next_event = rk_timer_set_next_event;
	ce->set_mode = rk_timer_set_mode;
	ce->irq = irq->irq;
	ce->cpumask = cpumask_of(cpu);

	writel_relaxed(1, base + TIMER_INT_STATUS);
	rk_timer_disable(base);

	irq->dev_id = ce;
	irq_set_affinity(irq->irq, cpumask_of(cpu));
	setup_irq(irq->irq, irq);

	clockevents_config_and_register(ce, 24000000, 0xF, 0xFFFFFFFF);

	return 0;
}

static int __cpuinit rk_local_timer_setup(struct clock_event_device *ce)
{
	ce->rating = 450;
	return rk_timer_init_clockevent(ce, smp_processor_id());
}

static void rk_local_timer_stop(struct clock_event_device *ce)
{
	ce->set_mode(CLOCK_EVT_MODE_UNUSED, ce);
	remove_irq(ce->irq, &per_cpu(ce_timer, smp_processor_id()).irq);
}

static struct local_timer_ops rk_local_timer_ops __cpuinitdata = {
	.setup	= rk_local_timer_setup,
	.stop	= rk_local_timer_stop,
};

static cycle_t rk_timer_read(struct clocksource *cs)
{
	return ~rk_timer_read_current_value(cs_timer.base);
}

static struct clocksource rk_timer_clocksource = {
	.name	= TIMER_NAME,
	.rating	= 200,
	.read	= rk_timer_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init rk_timer_init_clocksource(struct device_node *np)
{
	void __iomem *base;
	struct clocksource *cs = &rk_timer_clocksource;

	base = of_iomap(np, 0);
	cs_timer.base = base;

	rk_timer_disable(base);
	writel_relaxed(0xFFFFFFFF, base + TIMER_LOAD_COUNT0);
	writel_relaxed(0xFFFFFFFF, base + TIMER_LOAD_COUNT1);
	dsb();
	rk_timer_enable(base, TIMER_MODE_FREE_RUNNING | TIMER_INT_MASK);
	clocksource_register_hz(cs, 24000000);
}

static u32 rockchip_read_sched_clock(void)
{
	return ~rk_timer_read_current_value(cs_timer.base);
}

static void __init rk_timer_init_ce_timer(struct device_node *np, unsigned int cpu)
{
	struct ce_timer *timer = &per_cpu(ce_timer, cpu);
	struct irqaction *irq = &timer->irq;

	timer->base = of_iomap(np, 0);
	snprintf(timer->name, sizeof(timer->name), TIMER_NAME "%d", cpu);
	irq->irq = irq_of_parse_and_map(np, 0);
	irq->name = timer->name;
	irq->flags = IRQF_TIMER | IRQF_NOBALANCING;
	irq->handler = rk_timer_clockevent_interrupt;
}

static int num_called;
static void __init rk_timer_init(struct device_node *np)
{
	switch (num_called) {
	case 0:
		rk_timer_init_clocksource(np);
		setup_sched_clock(rockchip_read_sched_clock, 32, 24000000);
		local_timer_register(&rk_local_timer_ops);
		break;
	default:
		rk_timer_init_ce_timer(np, num_called - 1);
		break;
	}

	num_called++;
}
CLOCKSOURCE_OF_DECLARE(rk_timer, "rockchip,timer", rk_timer_init);
