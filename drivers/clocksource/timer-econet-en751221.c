// SPDX-License-Identifier: GPL-2.0
/*
 * Timer present on EcoNet EN75xx MIPS based SoCs.
 *
 * Copyright (C) 2025 by Caleb James DeLisle <cjd@cjdns.fr>
 */

#include <linux/io.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/sched_clock.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/cpuhotplug.h>
#include <linux/clk.h>

#define ECONET_BITS			32
#define ECONET_MIN_DELTA		0x00001000
#define ECONET_MAX_DELTA		GENMASK(ECONET_BITS - 2, 0)
/* 34Kc hardware has 1 block and 1004Kc has 2. */
#define ECONET_NUM_BLOCKS		DIV_ROUND_UP(NR_CPUS, 2)

static struct {
	void __iomem	*membase[ECONET_NUM_BLOCKS];
	u32		freq_hz;
} econet_timer __ro_after_init;

static DEFINE_PER_CPU(struct clock_event_device, econet_timer_pcpu);

/* Each memory block has 2 timers, the order of registers is:
 * CTL, CMR0, CNT0, CMR1, CNT1
 */
static inline void __iomem *reg_ctl(u32 timer_n)
{
	return econet_timer.membase[timer_n >> 1];
}

static inline void __iomem *reg_compare(u32 timer_n)
{
	return econet_timer.membase[timer_n >> 1] + (timer_n & 1) * 0x08 + 0x04;
}

static inline void __iomem *reg_count(u32 timer_n)
{
	return econet_timer.membase[timer_n >> 1] + (timer_n & 1) * 0x08 + 0x08;
}

static inline u32 ctl_bit_enabled(u32 timer_n)
{
	return 1U << (timer_n & 1);
}

static inline u32 ctl_bit_pending(u32 timer_n)
{
	return 1U << ((timer_n & 1) + 16);
}

static bool cevt_is_pending(int cpu_id)
{
	return ioread32(reg_ctl(cpu_id)) & ctl_bit_pending(cpu_id);
}

static irqreturn_t cevt_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *dev = this_cpu_ptr(&econet_timer_pcpu);
	int cpu = cpumask_first(dev->cpumask);

	/* Each VPE has its own events,
	 * so this will only happen on spurious interrupt.
	 */
	if (!cevt_is_pending(cpu))
		return IRQ_NONE;

	iowrite32(ioread32(reg_count(cpu)), reg_compare(cpu));
	dev->event_handler(dev);
	return IRQ_HANDLED;
}

static int cevt_set_next_event(ulong delta, struct clock_event_device *dev)
{
	u32 next;
	int cpu;

	cpu = cpumask_first(dev->cpumask);
	next = ioread32(reg_count(cpu)) + delta;
	iowrite32(next, reg_compare(cpu));

	if ((s32)(next - ioread32(reg_count(cpu))) < ECONET_MIN_DELTA / 2)
		return -ETIME;

	return 0;
}

static int cevt_init_cpu(uint cpu)
{
	struct clock_event_device *cd = &per_cpu(econet_timer_pcpu, cpu);
	u32 reg;

	pr_debug("%s: Setting up clockevent for CPU %d\n", cd->name, cpu);

	reg = ioread32(reg_ctl(cpu)) | ctl_bit_enabled(cpu);
	iowrite32(reg, reg_ctl(cpu));

	enable_percpu_irq(cd->irq, IRQ_TYPE_NONE);

	/* Do this last because it synchronously configures the timer */
	clockevents_config_and_register(cd, econet_timer.freq_hz,
					ECONET_MIN_DELTA, ECONET_MAX_DELTA);

	return 0;
}

static u64 notrace sched_clock_read(void)
{
	/* Always read from clock zero no matter the CPU */
	return (u64)ioread32(reg_count(0));
}

/* Init */

static void __init cevt_dev_init(uint cpu)
{
	iowrite32(0, reg_count(cpu));
	iowrite32(U32_MAX, reg_compare(cpu));
}

static int __init cevt_init(struct device_node *np)
{
	int i, irq, ret;

	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("%pOFn: irq_of_parse_and_map failed", np);
		return -EINVAL;
	}

	ret = request_percpu_irq(irq, cevt_interrupt, np->name, &econet_timer_pcpu);

	if (ret < 0) {
		pr_err("%pOFn: IRQ %d setup failed (%d)\n", np, irq, ret);
		goto err_unmap_irq;
	}

	for_each_possible_cpu(i) {
		struct clock_event_device *cd = &per_cpu(econet_timer_pcpu, i);

		cd->rating		= 310;
		cd->features		= CLOCK_EVT_FEAT_ONESHOT |
					  CLOCK_EVT_FEAT_C3STOP |
					  CLOCK_EVT_FEAT_PERCPU;
		cd->set_next_event	= cevt_set_next_event;
		cd->irq			= irq;
		cd->cpumask		= cpumask_of(i);
		cd->name		= np->name;

		cevt_dev_init(i);
	}

	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			  "clockevents/econet/timer:starting",
			  cevt_init_cpu, NULL);
	return 0;

err_unmap_irq:
	irq_dispose_mapping(irq);
	return ret;
}

static int __init timer_init(struct device_node *np)
{
	int num_blocks = DIV_ROUND_UP(num_possible_cpus(), 2);
	struct clk *clk;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%pOFn: Failed to get CPU clock from DT %ld\n", np, PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	econet_timer.freq_hz = clk_get_rate(clk);

	for (int i = 0; i < num_blocks; i++) {
		econet_timer.membase[i] = of_iomap(np, i);
		if (!econet_timer.membase[i]) {
			pr_err("%pOFn: failed to map register [%d]\n", np, i);
			return -ENXIO;
		}
	}

	/* For clocksource purposes always read clock zero, whatever the CPU */
	ret = clocksource_mmio_init(reg_count(0), np->name,
				    econet_timer.freq_hz, 301, ECONET_BITS,
				    clocksource_mmio_readl_up);
	if (ret) {
		pr_err("%pOFn: clocksource_mmio_init failed: %d", np, ret);
		return ret;
	}

	ret = cevt_init(np);
	if (ret < 0)
		return ret;

	sched_clock_register(sched_clock_read, ECONET_BITS,
			     econet_timer.freq_hz);

	pr_info("%pOFn: using %u.%03u MHz high precision timer\n", np,
		econet_timer.freq_hz / 1000000,
		(econet_timer.freq_hz / 1000) % 1000);

	return 0;
}

TIMER_OF_DECLARE(econet_timer_hpt, "econet,en751221-timer", timer_init);
