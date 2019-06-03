// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/sched_clock.h>
#include <linux/time.h>

#include "timer-of.h"

#define RTC_SECONDS            0x08
#define RTC_SHADOW_SECONDS     0x0c
#define RTC_MILLISECONDS       0x10

#define TIMERUS_CNTR_1US 0x10
#define TIMERUS_USEC_CFG 0x14
#define TIMERUS_CNTR_FREEZE 0x4c

#define TIMER_PTV		0x0
#define TIMER_PTV_EN		BIT(31)
#define TIMER_PTV_PER		BIT(30)
#define TIMER_PCR		0x4
#define TIMER_PCR_INTR_CLR	BIT(30)

#define TIMER1_BASE		0x00
#define TIMER2_BASE		0x08
#define TIMER3_BASE		0x50
#define TIMER4_BASE		0x58
#define TIMER10_BASE		0x90

#define TIMER1_IRQ_IDX		0
#define TIMER10_IRQ_IDX		10

static u32 usec_config;
static void __iomem *timer_reg_base;

static int tegra_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	void __iomem *reg_base = timer_of_base(to_timer_of(evt));

	writel_relaxed(TIMER_PTV_EN |
		       ((cycles > 1) ? (cycles - 1) : 0), /* n+1 scheme */
		       reg_base + TIMER_PTV);

	return 0;
}

static int tegra_timer_shutdown(struct clock_event_device *evt)
{
	void __iomem *reg_base = timer_of_base(to_timer_of(evt));

	writel_relaxed(0, reg_base + TIMER_PTV);

	return 0;
}

static int tegra_timer_set_periodic(struct clock_event_device *evt)
{
	void __iomem *reg_base = timer_of_base(to_timer_of(evt));

	writel_relaxed(TIMER_PTV_EN | TIMER_PTV_PER |
		       ((timer_of_rate(to_timer_of(evt)) / HZ) - 1),
		       reg_base + TIMER_PTV);

	return 0;
}

static irqreturn_t tegra_timer_isr(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	void __iomem *reg_base = timer_of_base(to_timer_of(evt));

	writel_relaxed(TIMER_PCR_INTR_CLR, reg_base + TIMER_PCR);
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static void tegra_timer_suspend(struct clock_event_device *evt)
{
	void __iomem *reg_base = timer_of_base(to_timer_of(evt));

	writel_relaxed(TIMER_PCR_INTR_CLR, reg_base + TIMER_PCR);
}

static void tegra_timer_resume(struct clock_event_device *evt)
{
	writel_relaxed(usec_config, timer_reg_base + TIMERUS_USEC_CFG);
}

static DEFINE_PER_CPU(struct timer_of, tegra_to) = {
	.flags = TIMER_OF_CLOCK | TIMER_OF_BASE,

	.clkevt = {
		.name = "tegra_timer",
		.rating = 460,
		.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
		.set_next_event = tegra_timer_set_next_event,
		.set_state_shutdown = tegra_timer_shutdown,
		.set_state_periodic = tegra_timer_set_periodic,
		.set_state_oneshot = tegra_timer_shutdown,
		.tick_resume = tegra_timer_shutdown,
		.suspend = tegra_timer_suspend,
		.resume = tegra_timer_resume,
	},
};

static int tegra_timer_setup(unsigned int cpu)
{
	struct timer_of *to = per_cpu_ptr(&tegra_to, cpu);

	writel_relaxed(0, timer_of_base(to) + TIMER_PTV);
	writel_relaxed(TIMER_PCR_INTR_CLR, timer_of_base(to) + TIMER_PCR);

	irq_force_affinity(to->clkevt.irq, cpumask_of(cpu));
	enable_irq(to->clkevt.irq);

	clockevents_config_and_register(&to->clkevt, timer_of_rate(to),
					1, /* min */
					0x1fffffff); /* 29 bits */

	return 0;
}

static int tegra_timer_stop(unsigned int cpu)
{
	struct timer_of *to = per_cpu_ptr(&tegra_to, cpu);

	to->clkevt.set_state_shutdown(&to->clkevt);
	disable_irq_nosync(to->clkevt.irq);

	return 0;
}

static u64 notrace tegra_read_sched_clock(void)
{
	return readl_relaxed(timer_reg_base + TIMERUS_CNTR_1US);
}

#ifdef CONFIG_ARM
static unsigned long tegra_delay_timer_read_counter_long(void)
{
	return readl_relaxed(timer_reg_base + TIMERUS_CNTR_1US);
}

static struct delay_timer tegra_delay_timer = {
	.read_current_timer = tegra_delay_timer_read_counter_long,
	.freq = 1000000,
};
#endif

static struct timer_of suspend_rtc_to = {
	.flags = TIMER_OF_BASE | TIMER_OF_CLOCK,
};

/*
 * tegra_rtc_read - Reads the Tegra RTC registers
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
static u64 tegra_rtc_read_ms(struct clocksource *cs)
{
	void __iomem *reg_base = timer_of_base(&suspend_rtc_to);
	u32 ms = readl_relaxed(reg_base + RTC_MILLISECONDS);
	u32 s = readl_relaxed(reg_base + RTC_SHADOW_SECONDS);
	return (u64)s * MSEC_PER_SEC + ms;
}

static struct clocksource suspend_rtc_clocksource = {
	.name	= "tegra_suspend_timer",
	.rating	= 200,
	.read	= tegra_rtc_read_ms,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_SUSPEND_NONSTOP,
};

static inline unsigned int tegra_base_for_cpu(int cpu, bool tegra20)
{
	if (tegra20) {
		switch (cpu) {
		case 0:
			return TIMER1_BASE;
		case 1:
			return TIMER2_BASE;
		case 2:
			return TIMER3_BASE;
		default:
			return TIMER4_BASE;
		}
	}

	return TIMER10_BASE + cpu * 8;
}

static inline unsigned int tegra_irq_idx_for_cpu(int cpu, bool tegra20)
{
	if (tegra20)
		return TIMER1_IRQ_IDX + cpu;

	return TIMER10_IRQ_IDX + cpu;
}

static int __init tegra_init_timer(struct device_node *np, bool tegra20)
{
	struct timer_of *to;
	int cpu, ret;

	to = this_cpu_ptr(&tegra_to);
	ret = timer_of_init(np, to);
	if (ret < 0)
		goto out;

	timer_reg_base = timer_of_base(to);

	/*
	 * Configure microsecond timers to have 1MHz clock
	 * Config register is 0xqqww, where qq is "dividend", ww is "divisor"
	 * Uses n+1 scheme
	 */
	switch (timer_of_rate(to)) {
	case 12000000:
		usec_config = 0x000b; /* (11+1)/(0+1) */
		break;
	case 12800000:
		usec_config = 0x043f; /* (63+1)/(4+1) */
		break;
	case 13000000:
		usec_config = 0x000c; /* (12+1)/(0+1) */
		break;
	case 16800000:
		usec_config = 0x0453; /* (83+1)/(4+1) */
		break;
	case 19200000:
		usec_config = 0x045f; /* (95+1)/(4+1) */
		break;
	case 26000000:
		usec_config = 0x0019; /* (25+1)/(0+1) */
		break;
	case 38400000:
		usec_config = 0x04bf; /* (191+1)/(4+1) */
		break;
	case 48000000:
		usec_config = 0x002f; /* (47+1)/(0+1) */
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	writel_relaxed(usec_config, timer_reg_base + TIMERUS_USEC_CFG);

	for_each_possible_cpu(cpu) {
		struct timer_of *cpu_to = per_cpu_ptr(&tegra_to, cpu);
		unsigned int base = tegra_base_for_cpu(cpu, tegra20);
		unsigned int idx = tegra_irq_idx_for_cpu(cpu, tegra20);

		/*
		 * TIMER1-9 are fixed to 1MHz, TIMER10-13 are running off the
		 * parent clock.
		 */
		if (tegra20)
			cpu_to->of_clk.rate = 1000000;

		cpu_to = per_cpu_ptr(&tegra_to, cpu);
		cpu_to->of_base.base = timer_reg_base + base;
		cpu_to->clkevt.cpumask = cpumask_of(cpu);
		cpu_to->clkevt.irq = irq_of_parse_and_map(np, idx);
		if (!cpu_to->clkevt.irq) {
			pr_err("%s: can't map IRQ for CPU%d\n",
			       __func__, cpu);
			ret = -EINVAL;
			goto out;
		}

		irq_set_status_flags(cpu_to->clkevt.irq, IRQ_NOAUTOEN);
		ret = request_irq(cpu_to->clkevt.irq, tegra_timer_isr,
				  IRQF_TIMER | IRQF_NOBALANCING,
				  cpu_to->clkevt.name, &cpu_to->clkevt);
		if (ret) {
			pr_err("%s: cannot setup irq %d for CPU%d\n",
				__func__, cpu_to->clkevt.irq, cpu);
			ret = -EINVAL;
			goto out_irq;
		}
	}

	sched_clock_register(tegra_read_sched_clock, 32, 1000000);

	ret = clocksource_mmio_init(timer_reg_base + TIMERUS_CNTR_1US,
				    "timer_us", 1000000,
				    300, 32, clocksource_mmio_readl_up);
	if (ret)
		pr_err("failed to register clocksource: %d\n", ret);

#ifdef CONFIG_ARM
	register_current_timer_delay(&tegra_delay_timer);
#endif

	cpuhp_setup_state(CPUHP_AP_TEGRA_TIMER_STARTING,
			  "AP_TEGRA_TIMER_STARTING", tegra_timer_setup,
			  tegra_timer_stop);

	return ret;
out_irq:
	for_each_possible_cpu(cpu) {
		struct timer_of *cpu_to;

		cpu_to = per_cpu_ptr(&tegra_to, cpu);
		if (cpu_to->clkevt.irq) {
			free_irq(cpu_to->clkevt.irq, &cpu_to->clkevt);
			irq_dispose_mapping(cpu_to->clkevt.irq);
		}
	}
out:
	timer_of_cleanup(to);
	return ret;
}

static int __init tegra210_init_timer(struct device_node *np)
{
	return tegra_init_timer(np, false);
}
TIMER_OF_DECLARE(tegra210_timer, "nvidia,tegra210-timer", tegra210_init_timer);

static int __init tegra20_init_timer(struct device_node *np)
{
	return tegra_init_timer(np, true);
}
TIMER_OF_DECLARE(tegra20_timer, "nvidia,tegra20-timer", tegra20_init_timer);

static int __init tegra20_init_rtc(struct device_node *np)
{
	int ret;

	ret = timer_of_init(np, &suspend_rtc_to);
	if (ret)
		return ret;

	clocksource_register_hz(&suspend_rtc_clocksource, 1000);

	return 0;
}
TIMER_OF_DECLARE(tegra20_rtc, "nvidia,tegra20-rtc", tegra20_init_rtc);
