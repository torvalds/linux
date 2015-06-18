/*
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
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
#include <linux/delay.h>
#include <linux/percpu.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#ifdef CONFIG_LOCAL_TIMERS
#include <asm/localtimer.h>
#endif
#ifdef CONFIG_ARM
#include <asm/sched_clock.h>
#endif

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

struct bc_timer {
	struct clock_event_device ce;
	void __iomem *base;
	struct clk *clk;
	struct clk *pclk;
	struct irqaction irq;
	char name[16];
};

static struct cs_timer cs_timer;
static DEFINE_PER_CPU(struct ce_timer, ce_timer);
static struct bc_timer bc_timer;

static inline void rk_timer_disable(void __iomem *base)
{
	writel_relaxed(TIMER_DISABLE, base + TIMER_CONTROL_REG);
	dsb(sy);
}

static inline void rk_timer_enable(void __iomem *base, u32 flags)
{
	writel_relaxed(TIMER_ENABLE | flags, base + TIMER_CONTROL_REG);
	dsb(sy);
}

static inline u32 rk_timer_read_current_value(void __iomem *base)
{
	return readl_relaxed(base + TIMER_CURRENT_VALUE0);
}

static inline u64 rk_timer_read_current_value64(void __iomem *base)
{
	u32 upper, lower;

	do {
		upper = readl_relaxed(base + TIMER_CURRENT_VALUE1);
		lower = readl_relaxed(base + TIMER_CURRENT_VALUE0);
	} while (upper != readl_relaxed(base + TIMER_CURRENT_VALUE1));

	return ((u64) upper << 32) + lower;
}

static inline int rk_timer_do_set_next_event(unsigned long cycles, void __iomem *base)
{
	rk_timer_disable(base);
	writel_relaxed(cycles, base + TIMER_LOAD_COUNT0);
	writel_relaxed(0, base + TIMER_LOAD_COUNT1);
	dsb(sy);
	rk_timer_enable(base, TIMER_MODE_USER_DEFINED_COUNT | TIMER_INT_UNMASK);
	return 0;
}

static int rk_timer_set_next_event(unsigned long cycles, struct clock_event_device *ce)
{
	return rk_timer_do_set_next_event(cycles, __get_cpu_var(ce_timer).base);
}

static int rk_timer_broadcast_set_next_event(unsigned long cycles, struct clock_event_device *ce)
{
	return rk_timer_do_set_next_event(cycles, bc_timer.base);
}

static inline void rk_timer_do_set_mode(enum clock_event_mode mode, void __iomem *base)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		rk_timer_disable(base);
		writel_relaxed(24000000 / HZ - 1, base + TIMER_LOAD_COUNT0);
		dsb(sy);
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

static void rk_timer_set_mode(enum clock_event_mode mode, struct clock_event_device *ce)
{
	rk_timer_do_set_mode(mode, __get_cpu_var(ce_timer).base);
}

static void rk_timer_broadcast_set_mode(enum clock_event_mode mode, struct clock_event_device *ce)
{
	rk_timer_do_set_mode(mode, bc_timer.base);
}

static inline irqreturn_t rk_timer_interrupt(void __iomem *base, struct clock_event_device *ce)
{
	/* clear interrupt */
	writel_relaxed(1, base + TIMER_INT_STATUS);
	if (ce->mode == CLOCK_EVT_MODE_ONESHOT) {
		writel_relaxed(TIMER_DISABLE, base + TIMER_CONTROL_REG);
	}
	dsb(sy);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

static irqreturn_t rk_timer_clockevent_interrupt(int irq, void *dev_id)
{
	return rk_timer_interrupt(__get_cpu_var(ce_timer).base, dev_id);
}

static irqreturn_t rk_timer_broadcast_interrupt(int irq, void *dev_id)
{
	return rk_timer_interrupt(bc_timer.base, dev_id);
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

	clockevents_config_and_register(ce, 24000000, 0xF, 0x7FFFFFFF);

	return 0;
}

static __init void rk_timer_init_broadcast(struct device_node *np)
{
	struct bc_timer *timer = &bc_timer;
	struct irqaction *irq = &timer->irq;
	struct clock_event_device *ce = &timer->ce;
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base)
		return;

	timer->base = base;
	snprintf(timer->name, sizeof(timer->name), TIMER_NAME);
	irq->irq = irq_of_parse_and_map(np, 0);
	irq->name = timer->name;
	irq->flags = IRQF_TIMER | IRQF_NOBALANCING;
	irq->handler = rk_timer_broadcast_interrupt;

	ce->name = timer->name;
	ce->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	ce->set_next_event = rk_timer_broadcast_set_next_event;
	ce->set_mode = rk_timer_broadcast_set_mode;
	ce->irq = irq->irq;
	ce->cpumask = cpumask_of(0);
	ce->rating = 250;

	writel_relaxed(1, base + TIMER_INT_STATUS);
	rk_timer_disable(base);

	irq->dev_id = ce;
	setup_irq(irq->irq, irq);

	clockevents_config_and_register(ce, 24000000, 0xF, 0xFFFFFFFF);
}

#ifdef CONFIG_LOCAL_TIMERS
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
#else
static DEFINE_PER_CPU(struct clock_event_device, percpu_clockevent);

static int __init rk_timer_init_percpu_clockevent(unsigned int cpu)
{
	struct clock_event_device *ce = &per_cpu(percpu_clockevent, cpu);

	ce->rating = 500;
	return rk_timer_init_clockevent(ce, cpu);
}
#endif

static cycle_t rk_timer_read(struct clocksource *cs)
{
	return ~rk_timer_read_current_value(cs_timer.base);
}

static cycle_t rk_timer_read_up(struct clocksource *cs)
{
	return rk_timer_read_current_value(cs_timer.base);
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
	dsb(sy);
	rk_timer_enable(base, TIMER_MODE_FREE_RUNNING | TIMER_INT_MASK);
	clocksource_register_hz(cs, 24000000);
}

#ifdef CONFIG_ARM
static u32 rockchip_read_sched_clock(void)
{
	return ~rk_timer_read_current_value(cs_timer.base);
}

static u32 rockchip_read_sched_clock_up(void)
{
	return rk_timer_read_current_value(cs_timer.base);
}
#endif

static void __init rk_timer_init_ce_timer(struct device_node *np, unsigned int cpu)
{
	struct ce_timer *timer = &per_cpu(ce_timer, cpu);
	struct irqaction *irq = &timer->irq;

	timer->base = of_iomap(np, 0);
	snprintf(timer->name, sizeof(timer->name), TIMER_NAME "%d", cpu);
	irq->irq = irq_of_parse_and_map(np, 0);
	irq->name = timer->name;
	irq->flags = IRQF_TIMER | IRQF_NOBALANCING | IRQF_PERCPU;
	irq->handler = rk_timer_clockevent_interrupt;
}

#ifdef CONFIG_ARM
static struct delay_timer rk_delay_timer = {
	.read_current_timer = (unsigned long (*)(void))rockchip_read_sched_clock,
	.freq = 24000000,
};
#endif

static void __init rk_timer_init(struct device_node *np)
{
	u32 val = 0;
	if (of_property_read_u32(np, "rockchip,percpu", &val) == 0) {
#ifdef CONFIG_LOCAL_TIMERS
		local_timer_register(&rk_local_timer_ops);
#else
#endif
		rk_timer_init_ce_timer(np, val);
#ifndef CONFIG_LOCAL_TIMERS
		rk_timer_init_percpu_clockevent(val);
#endif
	} else if (of_property_read_u32(np, "rockchip,clocksource", &val) == 0 && val) {
		u32 count_up = 0;
		of_property_read_u32(np, "rockchip,count-up", &count_up);
		if (count_up) {
			rk_timer_clocksource.read = rk_timer_read_up;
#ifdef CONFIG_ARM
			rk_delay_timer.read_current_timer = (unsigned long (*)(void))rockchip_read_sched_clock_up;
#endif
		}
		rk_timer_init_clocksource(np);
#ifdef CONFIG_ARM
		if (!lpj_fine) {
			if (count_up)
				setup_sched_clock(rockchip_read_sched_clock_up, 32, 24000000);
			else
				setup_sched_clock(rockchip_read_sched_clock, 32, 24000000);
			register_current_timer_delay(&rk_delay_timer);
		}
#endif
	} else if (of_property_read_u32(np, "rockchip,broadcast", &val) == 0 && val) {
		rk_timer_init_broadcast(np);
	}
}
CLOCKSOURCE_OF_DECLARE(rk_timer, "rockchip,timer", rk_timer_init);
