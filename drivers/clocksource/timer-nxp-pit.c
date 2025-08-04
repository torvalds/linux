// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 * Copyright 2018,2021-2025 NXP
 */
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/cpuhotplug.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/platform_device.h>

/*
 * Each pit takes 0x10 Bytes register space
 */
#define PIT0_OFFSET	0x100
#define PIT_CH(n)       (PIT0_OFFSET + 0x10 * (n))

#define PITMCR(__base)	(__base)

#define PITMCR_FRZ	BIT(0)
#define PITMCR_MDIS	BIT(1)

#define PITLDVAL(__base)	(__base)
#define PITTCTRL(__base)	((__base) + 0x08)

#define PITCVAL_OFFSET	0x04
#define PITCVAL(__base)	((__base) + 0x04)

#define PITTCTRL_TEN			BIT(0)
#define PITTCTRL_TIE			BIT(1)

#define PITTFLG(__base)	((__base) + 0x0c)

#define PITTFLG_TIF			BIT(0)

struct pit_timer {
	void __iomem *clksrc_base;
	void __iomem *clkevt_base;
	struct clock_event_device ced;
	struct clocksource cs;
	int rate;
};

struct pit_timer_data {
	int max_pit_instances;
};

static DEFINE_PER_CPU(struct pit_timer *, pit_timers);

/*
 * Global structure for multiple PITs initialization
 */
static int pit_instances;
static int max_pit_instances = 1;

static void __iomem *sched_clock_base;

static inline struct pit_timer *ced_to_pit(struct clock_event_device *ced)
{
	return container_of(ced, struct pit_timer, ced);
}

static inline struct pit_timer *cs_to_pit(struct clocksource *cs)
{
	return container_of(cs, struct pit_timer, cs);
}

static inline void pit_module_enable(void __iomem *base)
{
	writel(0, PITMCR(base));
}

static inline void pit_module_disable(void __iomem *base)
{
	writel(PITMCR_MDIS, PITMCR(base));
}

static inline void pit_timer_enable(void __iomem *base, bool tie)
{
	u32 val = PITTCTRL_TEN | (tie ? PITTCTRL_TIE : 0);

	writel(val, PITTCTRL(base));
}

static inline void pit_timer_disable(void __iomem *base)
{
	writel(0, PITTCTRL(base));
}

static inline void pit_timer_set_counter(void __iomem *base, unsigned int cnt)
{
	writel(cnt, PITLDVAL(base));
}

static inline void pit_timer_irqack(struct pit_timer *pit)
{
	writel(PITTFLG_TIF, PITTFLG(pit->clkevt_base));
}

static u64 notrace pit_read_sched_clock(void)
{
	return ~readl(sched_clock_base);
}

static u64 pit_timer_clocksource_read(struct clocksource *cs)
{
	struct pit_timer *pit = cs_to_pit(cs);

	return (u64)~readl(PITCVAL(pit->clksrc_base));
}

static int pit_clocksource_init(struct pit_timer *pit, const char *name,
				void __iomem *base, unsigned long rate)
{
	/*
	 * The channels 0 and 1 can be chained to build a 64-bit
	 * timer. Let's use the channel 2 as a clocksource and leave
	 * the channels 0 and 1 unused for anyone else who needs them
	 */
	pit->clksrc_base = base + PIT_CH(2);
	pit->cs.name = name;
	pit->cs.rating = 300;
	pit->cs.read = pit_timer_clocksource_read;
	pit->cs.mask = CLOCKSOURCE_MASK(32);
	pit->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	/* set the max load value and start the clock source counter */
	pit_timer_disable(pit->clksrc_base);
	pit_timer_set_counter(pit->clksrc_base, ~0);
	pit_timer_enable(pit->clksrc_base, 0);

	sched_clock_base = pit->clksrc_base + PITCVAL_OFFSET;
	sched_clock_register(pit_read_sched_clock, 32, rate);

	return clocksource_register_hz(&pit->cs, rate);
}

static int pit_set_next_event(unsigned long delta, struct clock_event_device *ced)
{
	struct pit_timer *pit = ced_to_pit(ced);

	/*
	 * set a new value to PITLDVAL register will not restart the timer,
	 * to abort the current cycle and start a timer period with the new
	 * value, the timer must be disabled and enabled again.
	 * and the PITLAVAL should be set to delta minus one according to pit
	 * hardware requirement.
	 */
	pit_timer_disable(pit->clkevt_base);
	pit_timer_set_counter(pit->clkevt_base, delta - 1);
	pit_timer_enable(pit->clkevt_base, true);

	return 0;
}

static int pit_shutdown(struct clock_event_device *ced)
{
	struct pit_timer *pit = ced_to_pit(ced);

	pit_timer_disable(pit->clkevt_base);

	return 0;
}

static int pit_set_periodic(struct clock_event_device *ced)
{
	struct pit_timer *pit = ced_to_pit(ced);

	pit_set_next_event(pit->rate / HZ, ced);

	return 0;
}

static irqreturn_t pit_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ced = dev_id;
	struct pit_timer *pit = ced_to_pit(ced);

	pit_timer_irqack(pit);

	/*
	 * pit hardware doesn't support oneshot, it will generate an interrupt
	 * and reload the counter value from PITLDVAL when PITCVAL reach zero,
	 * and start the counter again. So software need to disable the timer
	 * to stop the counter loop in ONESHOT mode.
	 */
	if (likely(clockevent_state_oneshot(ced)))
		pit_timer_disable(pit->clkevt_base);

	ced->event_handler(ced);

	return IRQ_HANDLED;
}

static int pit_clockevent_per_cpu_init(struct pit_timer *pit, const char *name,
				       void __iomem *base, unsigned long rate,
				       int irq, unsigned int cpu)
{
	int ret;

	/*
	 * The channels 0 and 1 can be chained to build a 64-bit
	 * timer. Let's use the channel 3 as a clockevent and leave
	 * the channels 0 and 1 unused for anyone else who needs them
	 */
	pit->clkevt_base = base + PIT_CH(3);
	pit->rate = rate;

	pit_timer_disable(pit->clkevt_base);

	pit_timer_irqack(pit);

	ret = request_irq(irq, pit_timer_interrupt, IRQF_TIMER | IRQF_NOBALANCING,
			  name, &pit->ced);
	if (ret)
		return ret;

	pit->ced.cpumask = cpumask_of(cpu);
	pit->ced.irq = irq;

	pit->ced.name = name;
	pit->ced.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	pit->ced.set_state_shutdown = pit_shutdown;
	pit->ced.set_state_periodic = pit_set_periodic;
	pit->ced.set_next_event	= pit_set_next_event;
	pit->ced.rating	= 300;

	per_cpu(pit_timers, cpu) = pit;

	return 0;
}

static void pit_clockevent_per_cpu_exit(struct pit_timer *pit, unsigned int cpu)
{
	pit_timer_disable(pit->clkevt_base);
	free_irq(pit->ced.irq, &pit->ced);
	per_cpu(pit_timers, cpu) = NULL;
}

static int pit_clockevent_starting_cpu(unsigned int cpu)
{
	struct pit_timer *pit = per_cpu(pit_timers, cpu);
	int ret;

	if (!pit)
		return 0;

	ret = irq_force_affinity(pit->ced.irq, cpumask_of(cpu));
	if (ret) {
		pit_clockevent_per_cpu_exit(pit, cpu);
		return ret;
	}

	/*
	 * The value for the LDVAL register trigger is calculated as:
	 * LDVAL trigger = (period / clock period) - 1
	 * The pit is a 32-bit down count timer, when the counter value
	 * reaches 0, it will generate an interrupt, thus the minimal
	 * LDVAL trigger value is 1. And then the min_delta is
	 * minimal LDVAL trigger value + 1, and the max_delta is full 32-bit.
	 */
	clockevents_config_and_register(&pit->ced, pit->rate, 2, 0xffffffff);

	return 0;
}

static int pit_timer_init(struct device_node *np)
{
	struct pit_timer *pit;
	struct clk *pit_clk;
	void __iomem *timer_base;
	const char *name = of_node_full_name(np);
	unsigned long clk_rate;
	int irq, ret;

	pit = kzalloc(sizeof(*pit), GFP_KERNEL);
	if (!pit)
		return -ENOMEM;

	ret = -ENXIO;
	timer_base = of_iomap(np, 0);
	if (!timer_base) {
		pr_err("Failed to iomap\n");
		goto out_kfree;
	}

	ret = -EINVAL;
	irq = irq_of_parse_and_map(np, 0);
	if (irq <= 0) {
		pr_err("Failed to irq_of_parse_and_map\n");
		goto out_iounmap;
	}

	pit_clk = of_clk_get(np, 0);
	if (IS_ERR(pit_clk)) {
		ret = PTR_ERR(pit_clk);
		goto out_irq_dispose_mapping;
	}

	ret = clk_prepare_enable(pit_clk);
	if (ret)
		goto out_clk_put;

	clk_rate = clk_get_rate(pit_clk);

	pit_module_disable(timer_base);

	ret = pit_clocksource_init(pit, name, timer_base, clk_rate);
	if (ret) {
		pr_err("Failed to initialize clocksource '%pOF'\n", np);
		goto out_pit_module_disable;
	}

	ret = pit_clockevent_per_cpu_init(pit, name, timer_base, clk_rate, irq, pit_instances);
	if (ret) {
		pr_err("Failed to initialize clockevent '%pOF'\n", np);
		goto out_pit_clocksource_unregister;
	}

	/* enable the pit module */
	pit_module_enable(timer_base);

	pit_instances++;

	if (pit_instances == max_pit_instances) {
		ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "PIT timer:starting",
					pit_clockevent_starting_cpu, NULL);
		if (ret < 0)
			goto out_pit_clocksource_unregister;
	}

	return 0;

out_pit_clocksource_unregister:
	clocksource_unregister(&pit->cs);
out_pit_module_disable:
	pit_module_disable(timer_base);
	clk_disable_unprepare(pit_clk);
out_clk_put:
	clk_put(pit_clk);
out_irq_dispose_mapping:
	irq_dispose_mapping(irq);
out_iounmap:
	iounmap(timer_base);
out_kfree:
	kfree(pit);

	return ret;
}

static int pit_timer_probe(struct platform_device *pdev)
{
	const struct pit_timer_data *pit_timer_data;

	pit_timer_data = of_device_get_match_data(&pdev->dev);
	if (pit_timer_data)
		max_pit_instances = pit_timer_data->max_pit_instances;

	return pit_timer_init(pdev->dev.of_node);
}

static struct pit_timer_data s32g2_data = { .max_pit_instances = 2 };

static const struct of_device_id pit_timer_of_match[] = {
	{ .compatible = "nxp,s32g2-pit", .data = &s32g2_data },
	{ }
};
MODULE_DEVICE_TABLE(of, pit_timer_of_match);

static struct platform_driver nxp_pit_driver = {
	.driver = {
		.name = "nxp-pit",
		.of_match_table = pit_timer_of_match,
	},
	.probe = pit_timer_probe,
};
module_platform_driver(nxp_pit_driver);

TIMER_OF_DECLARE(vf610, "fsl,vf610-pit", pit_timer_init);
