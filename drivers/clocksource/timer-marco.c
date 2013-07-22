/*
 * System timer for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>
#include <asm/localtimer.h>
#include <asm/mach/time.h>

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
	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL + 4 * idx) | 0x7,
		sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL + 4 * idx);
}

/* timer interrupt handler */
static irqreturn_t sirfsoc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;
	int cpu = smp_processor_id();

	/* clear timer interrupt */
	writel_relaxed(BIT(cpu), sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS);

	if (ce->mode == CLOCK_EVT_MODE_ONESHOT)
		sirfsoc_timer_count_disable(cpu);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

/* read 64-bit timer counter */
static cycle_t sirfsoc_timer_read(struct clocksource *cs)
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

	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0 +
		4 * cpu);
	writel_relaxed(delta, sirfsoc_timer_base + SIRFSOC_TIMER_MATCH_0 +
		4 * cpu);

	/* enable the tick */
	sirfsoc_timer_count_enable(cpu);

	return 0;
}

static void sirfsoc_timer_set_mode(enum clock_event_mode mode,
	struct clock_event_device *ce)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
		/* enable in set_next_event */
		break;
	default:
		break;
	}

	sirfsoc_timer_count_disable(smp_processor_id());
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

static struct clock_event_device sirfsoc_clockevent = {
	.name = "sirfsoc_clockevent",
	.rating = 200,
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = sirfsoc_timer_set_mode,
	.set_next_event = sirfsoc_timer_set_next_event,
};

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
	.dev_id = &sirfsoc_clockevent,
};

#ifdef CONFIG_LOCAL_TIMERS

static struct irqaction sirfsoc_timer1_irq = {
	.name = "sirfsoc_timer1",
	.flags = IRQF_TIMER | IRQF_NOBALANCING,
	.handler = sirfsoc_timer_interrupt,
};

static int sirfsoc_local_timer_setup(struct clock_event_device *ce)
{
	/* Use existing clock_event for cpu 0 */
	if (!smp_processor_id())
		return 0;

	ce->irq = sirfsoc_timer1_irq.irq;
	ce->name = "local_timer";
	ce->features = sirfsoc_clockevent.features;
	ce->rating = sirfsoc_clockevent.rating;
	ce->set_mode = sirfsoc_timer_set_mode;
	ce->set_next_event = sirfsoc_timer_set_next_event;
	ce->shift = sirfsoc_clockevent.shift;
	ce->mult = sirfsoc_clockevent.mult;
	ce->max_delta_ns = sirfsoc_clockevent.max_delta_ns;
	ce->min_delta_ns = sirfsoc_clockevent.min_delta_ns;

	sirfsoc_timer1_irq.dev_id = ce;
	BUG_ON(setup_irq(ce->irq, &sirfsoc_timer1_irq));
	irq_set_affinity(sirfsoc_timer1_irq.irq, cpumask_of(1));

	clockevents_register_device(ce);
	return 0;
}

static void sirfsoc_local_timer_stop(struct clock_event_device *ce)
{
	sirfsoc_timer_count_disable(1);

	remove_irq(sirfsoc_timer1_irq.irq, &sirfsoc_timer1_irq);
}

static struct local_timer_ops sirfsoc_local_timer_ops = {
	.setup	= sirfsoc_local_timer_setup,
	.stop	= sirfsoc_local_timer_stop,
};
#endif /* CONFIG_LOCAL_TIMERS */

static void __init sirfsoc_clockevent_init(void)
{
	clockevents_calc_mult_shift(&sirfsoc_clockevent, CLOCK_TICK_RATE, 60);

	sirfsoc_clockevent.max_delta_ns =
		clockevent_delta2ns(-2, &sirfsoc_clockevent);
	sirfsoc_clockevent.min_delta_ns =
		clockevent_delta2ns(2, &sirfsoc_clockevent);

	sirfsoc_clockevent.cpumask = cpumask_of(0);
	clockevents_register_device(&sirfsoc_clockevent);
#ifdef CONFIG_LOCAL_TIMERS
	local_timer_register(&sirfsoc_local_timer_ops);
#endif
}

/* initialize the kernel jiffy timer source */
static void __init sirfsoc_marco_timer_init(void)
{
	unsigned long rate;
	u32 timer_div;
	struct clk *clk;

	/* timer's input clock is io clock */
	clk = clk_get_sys("io", NULL);

	BUG_ON(IS_ERR(clk));
	rate = clk_get_rate(clk);

	BUG_ON(rate < CLOCK_TICK_RATE);
	BUG_ON(rate % CLOCK_TICK_RATE);

	/* Initialize the timer dividers */
	timer_div = rate / CLOCK_TICK_RATE - 1;
	writel_relaxed(timer_div << 16, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
	writel_relaxed(timer_div << 16, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_0_CTRL);
	writel_relaxed(timer_div << 16, sirfsoc_timer_base + SIRFSOC_TIMER_32COUNTER_1_CTRL);

	/* Initialize timer counters to 0 */
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_LO);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_LOAD_HI);
	writel_relaxed(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL) |
		BIT(1) | BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_64COUNTER_CTRL);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_0);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_1);

	/* Clear all interrupts */
	writel_relaxed(0xFFFF, sirfsoc_timer_base + SIRFSOC_TIMER_INTR_STATUS);

	BUG_ON(clocksource_register_hz(&sirfsoc_clocksource, CLOCK_TICK_RATE));

	BUG_ON(setup_irq(sirfsoc_timer_irq.irq, &sirfsoc_timer_irq));

	sirfsoc_clockevent_init();
}

static void __init sirfsoc_of_timer_init(struct device_node *np)
{
	sirfsoc_timer_base = of_iomap(np, 0);
	if (!sirfsoc_timer_base)
		panic("unable to map timer cpu registers\n");

	sirfsoc_timer_irq.irq = irq_of_parse_and_map(np, 0);
	if (!sirfsoc_timer_irq.irq)
		panic("No irq passed for timer0 via DT\n");

#ifdef CONFIG_LOCAL_TIMERS
	sirfsoc_timer1_irq.irq = irq_of_parse_and_map(np, 1);
	if (!sirfsoc_timer1_irq.irq)
		panic("No irq passed for timer1 via DT\n");
#endif

	sirfsoc_marco_timer_init();
}
CLOCKSOURCE_OF_DECLARE(sirfsoc_marco_timer, "sirf,marco-tick", sirfsoc_of_timer_init );
