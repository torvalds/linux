// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/printk.h>
#include <linux/sched_clock.h>
#include "timer-of.h"

#define RTTM_DATA		0x0
#define RTTM_CNT		0x4
#define RTTM_CTRL		0x8
#define RTTM_INT		0xc

#define RTTM_CTRL_ENABLE	BIT(28)
#define RTTM_INT_PENDING	BIT(16)
#define RTTM_INT_ENABLE		BIT(20)

/*
 * The Otto platform provides multiple 28 bit timers/counters with the following
 * operating logic. If enabled the timer counts up. Per timer one can set a
 * maximum counter value as an end marker. If end marker is reached the timer
 * fires an interrupt. If the timer "overflows" by reaching the end marker or
 * by adding 1 to 0x0fffffff the counter is reset to 0. When this happens and
 * the timer is in operating mode COUNTER it stops. In mode TIMER it will
 * continue to count up.
 */
#define RTTM_CTRL_COUNTER	0
#define RTTM_CTRL_TIMER		BIT(24)

#define RTTM_BIT_COUNT		28
#define RTTM_MIN_DELTA		8
#define RTTM_MAX_DELTA		CLOCKSOURCE_MASK(28)

/*
 * Timers are derived from the LXB clock frequency. Usually this is a fixed
 * multiple of the 25 MHz oscillator. The 930X SOC is an exception from that.
 * Its LXB clock has only dividers and uses the switch PLL of 2.45 GHz as its
 * base. The only meaningful frequencies we can achieve from that are 175.000
 * MHz and 153.125 MHz. The greatest common divisor of all explained possible
 * speeds is 3125000. Pin the timers to this 3.125 MHz reference frequency.
 */
#define RTTM_TICKS_PER_SEC	3125000

struct rttm_cs {
	struct timer_of		to;
	struct clocksource	cs;
};

/* Simple internal register functions */
static inline void rttm_set_counter(void __iomem *base, unsigned int counter)
{
	iowrite32(counter, base + RTTM_CNT);
}

static inline unsigned int rttm_get_counter(void __iomem *base)
{
	return ioread32(base + RTTM_CNT);
}

static inline void rttm_set_period(void __iomem *base, unsigned int period)
{
	iowrite32(period, base + RTTM_DATA);
}

static inline void rttm_disable_timer(void __iomem *base)
{
	iowrite32(0, base + RTTM_CTRL);
}

static inline void rttm_enable_timer(void __iomem *base, u32 mode, u32 divisor)
{
	iowrite32(RTTM_CTRL_ENABLE | mode | divisor, base + RTTM_CTRL);
}

static inline void rttm_ack_irq(void __iomem *base)
{
	iowrite32(ioread32(base + RTTM_INT) | RTTM_INT_PENDING, base + RTTM_INT);
}

static inline void rttm_enable_irq(void __iomem *base)
{
	iowrite32(RTTM_INT_ENABLE, base + RTTM_INT);
}

static inline void rttm_disable_irq(void __iomem *base)
{
	iowrite32(0, base + RTTM_INT);
}

/* Aggregated control functions for kernel clock framework */
#define RTTM_DEBUG(base)			\
	pr_debug("------------- %d %p\n",	\
		 smp_processor_id(), base)

static irqreturn_t rttm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	rttm_ack_irq(to->of_base.base);
	RTTM_DEBUG(to->of_base.base);
	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

static void rttm_stop_timer(void __iomem *base)
{
	rttm_disable_timer(base);
	rttm_ack_irq(base);
}

static void rttm_start_timer(struct timer_of *to, u32 mode)
{
	rttm_set_counter(to->of_base.base, 0);
	rttm_enable_timer(to->of_base.base, mode, to->of_clk.rate / RTTM_TICKS_PER_SEC);
}

static int rttm_next_event(unsigned long delta, struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	RTTM_DEBUG(to->of_base.base);
	rttm_stop_timer(to->of_base.base);
	rttm_set_period(to->of_base.base, delta);
	rttm_start_timer(to, RTTM_CTRL_COUNTER);

	return 0;
}

static int rttm_state_oneshot(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	RTTM_DEBUG(to->of_base.base);
	rttm_stop_timer(to->of_base.base);
	rttm_set_period(to->of_base.base, RTTM_TICKS_PER_SEC / HZ);
	rttm_start_timer(to, RTTM_CTRL_COUNTER);

	return 0;
}

static int rttm_state_periodic(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	RTTM_DEBUG(to->of_base.base);
	rttm_stop_timer(to->of_base.base);
	rttm_set_period(to->of_base.base, RTTM_TICKS_PER_SEC / HZ);
	rttm_start_timer(to, RTTM_CTRL_TIMER);

	return 0;
}

static int rttm_state_shutdown(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	RTTM_DEBUG(to->of_base.base);
	rttm_stop_timer(to->of_base.base);

	return 0;
}

static void rttm_setup_timer(void __iomem *base)
{
	RTTM_DEBUG(base);
	rttm_stop_timer(base);
	rttm_set_period(base, 0);
}

static u64 rttm_read_clocksource(struct clocksource *cs)
{
	struct rttm_cs *rcs = container_of(cs, struct rttm_cs, cs);

	return rttm_get_counter(rcs->to.of_base.base);
}

/* Module initialization part. */
static DEFINE_PER_CPU(struct timer_of, rttm_to) = {
	.flags				= TIMER_OF_BASE | TIMER_OF_CLOCK | TIMER_OF_IRQ,
	.of_irq = {
		.flags			= IRQF_PERCPU | IRQF_TIMER,
		.handler		= rttm_timer_interrupt,
	},
	.clkevt = {
		.rating			= 400,
		.features		= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
		.set_state_periodic	= rttm_state_periodic,
		.set_state_shutdown	= rttm_state_shutdown,
		.set_state_oneshot	= rttm_state_oneshot,
		.set_next_event		= rttm_next_event
	},
};

static int rttm_enable_clocksource(struct clocksource *cs)
{
	struct rttm_cs *rcs = container_of(cs, struct rttm_cs, cs);

	rttm_disable_irq(rcs->to.of_base.base);
	rttm_setup_timer(rcs->to.of_base.base);
	rttm_enable_timer(rcs->to.of_base.base, RTTM_CTRL_TIMER,
			  rcs->to.of_clk.rate / RTTM_TICKS_PER_SEC);

	return 0;
}

struct rttm_cs rttm_cs = {
	.to = {
		.flags	= TIMER_OF_BASE | TIMER_OF_CLOCK,
	},
	.cs = {
		.name	= "realtek_otto_timer",
		.rating	= 400,
		.mask	= CLOCKSOURCE_MASK(RTTM_BIT_COUNT),
		.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
		.read	= rttm_read_clocksource,
	}
};

static u64 notrace rttm_read_clock(void)
{
	return rttm_get_counter(rttm_cs.to.of_base.base);
}

static int rttm_cpu_starting(unsigned int cpu)
{
	struct timer_of *to = per_cpu_ptr(&rttm_to, cpu);

	RTTM_DEBUG(to->of_base.base);
	to->clkevt.cpumask = cpumask_of(cpu);
	irq_force_affinity(to->of_irq.irq, to->clkevt.cpumask);
	clockevents_config_and_register(&to->clkevt, RTTM_TICKS_PER_SEC,
					RTTM_MIN_DELTA, RTTM_MAX_DELTA);
	rttm_enable_irq(to->of_base.base);

	return 0;
}

static int __init rttm_probe(struct device_node *np)
{
	unsigned int cpu, cpu_rollback;
	struct timer_of *to;
	unsigned int clkidx = num_possible_cpus();

	/* Use the first n timers as per CPU clock event generators */
	for_each_possible_cpu(cpu) {
		to = per_cpu_ptr(&rttm_to, cpu);
		to->of_irq.index = to->of_base.index = cpu;
		if (timer_of_init(np, to)) {
			pr_err("setup of timer %d failed\n", cpu);
			goto rollback;
		}
		rttm_setup_timer(to->of_base.base);
	}

	/* Activate the n'th + 1 timer as a stable CPU clocksource. */
	to = &rttm_cs.to;
	to->of_base.index = clkidx;
	timer_of_init(np, to);
	if (rttm_cs.to.of_base.base && rttm_cs.to.of_clk.rate) {
		rttm_enable_clocksource(&rttm_cs.cs);
		clocksource_register_hz(&rttm_cs.cs, RTTM_TICKS_PER_SEC);
		sched_clock_register(rttm_read_clock, RTTM_BIT_COUNT, RTTM_TICKS_PER_SEC);
	} else
		pr_err(" setup of timer %d as clocksource failed", clkidx);

	return cpuhp_setup_state(CPUHP_AP_REALTEK_TIMER_STARTING,
				"timer/realtek:online",
				rttm_cpu_starting, NULL);
rollback:
	pr_err("timer registration failed\n");
	for_each_possible_cpu(cpu_rollback) {
		if (cpu_rollback == cpu)
			break;
		to = per_cpu_ptr(&rttm_to, cpu_rollback);
		timer_of_cleanup(to);
	}

	return -EINVAL;
}

TIMER_OF_DECLARE(otto_timer, "realtek,otto-timer", rttm_probe);
