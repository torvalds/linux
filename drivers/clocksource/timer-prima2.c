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
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/sched_clock.h>

#define PRIMA2_CLOCK_FREQ 1000000

#define SIRFSOC_TIMER_COUNTER_LO	0x0000
#define SIRFSOC_TIMER_COUNTER_HI	0x0004
#define SIRFSOC_TIMER_MATCH_0		0x0008
#define SIRFSOC_TIMER_MATCH_1		0x000C
#define SIRFSOC_TIMER_MATCH_2		0x0010
#define SIRFSOC_TIMER_MATCH_3		0x0014
#define SIRFSOC_TIMER_MATCH_4		0x0018
#define SIRFSOC_TIMER_MATCH_5		0x001C
#define SIRFSOC_TIMER_STATUS		0x0020
#define SIRFSOC_TIMER_INT_EN		0x0024
#define SIRFSOC_TIMER_WATCHDOG_EN	0x0028
#define SIRFSOC_TIMER_DIV		0x002C
#define SIRFSOC_TIMER_LATCH		0x0030
#define SIRFSOC_TIMER_LATCHED_LO	0x0034
#define SIRFSOC_TIMER_LATCHED_HI	0x0038

#define SIRFSOC_TIMER_WDT_INDEX		5

#define SIRFSOC_TIMER_LATCH_BIT	 BIT(0)

#define SIRFSOC_TIMER_REG_CNT 11

static const u32 sirfsoc_timer_reg_list[SIRFSOC_TIMER_REG_CNT] = {
	SIRFSOC_TIMER_MATCH_0, SIRFSOC_TIMER_MATCH_1, SIRFSOC_TIMER_MATCH_2,
	SIRFSOC_TIMER_MATCH_3, SIRFSOC_TIMER_MATCH_4, SIRFSOC_TIMER_MATCH_5,
	SIRFSOC_TIMER_INT_EN, SIRFSOC_TIMER_WATCHDOG_EN, SIRFSOC_TIMER_DIV,
	SIRFSOC_TIMER_LATCHED_LO, SIRFSOC_TIMER_LATCHED_HI,
};

static u32 sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT];

static void __iomem *sirfsoc_timer_base;

/* timer0 interrupt handler */
static irqreturn_t sirfsoc_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ce = dev_id;

	WARN_ON(!(readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_STATUS) &
		BIT(0)));

	/* clear timer0 interrupt */
	writel_relaxed(BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_STATUS);

	ce->event_handler(ce);

	return IRQ_HANDLED;
}

/* read 64-bit timer counter */
static u64 notrace sirfsoc_timer_read(struct clocksource *cs)
{
	u64 cycles;

	/* latch the 64-bit timer counter */
	writel_relaxed(SIRFSOC_TIMER_LATCH_BIT,
		sirfsoc_timer_base + SIRFSOC_TIMER_LATCH);
	cycles = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_LATCHED_HI);
	cycles = (cycles << 32) |
		readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_LATCHED_LO);

	return cycles;
}

static int sirfsoc_timer_set_next_event(unsigned long delta,
	struct clock_event_device *ce)
{
	unsigned long now, next;

	writel_relaxed(SIRFSOC_TIMER_LATCH_BIT,
		sirfsoc_timer_base + SIRFSOC_TIMER_LATCH);
	now = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_LATCHED_LO);
	next = now + delta;
	writel_relaxed(next, sirfsoc_timer_base + SIRFSOC_TIMER_MATCH_0);
	writel_relaxed(SIRFSOC_TIMER_LATCH_BIT,
		sirfsoc_timer_base + SIRFSOC_TIMER_LATCH);
	now = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_LATCHED_LO);

	return next - now > delta ? -ETIME : 0;
}

static int sirfsoc_timer_shutdown(struct clock_event_device *evt)
{
	u32 val = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_INT_EN);

	writel_relaxed(val & ~BIT(0),
		       sirfsoc_timer_base + SIRFSOC_TIMER_INT_EN);
	return 0;
}

static int sirfsoc_timer_set_oneshot(struct clock_event_device *evt)
{
	u32 val = readl_relaxed(sirfsoc_timer_base + SIRFSOC_TIMER_INT_EN);

	writel_relaxed(val | BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_INT_EN);
	return 0;
}

static void sirfsoc_clocksource_suspend(struct clocksource *cs)
{
	int i;

	writel_relaxed(SIRFSOC_TIMER_LATCH_BIT,
		sirfsoc_timer_base + SIRFSOC_TIMER_LATCH);

	for (i = 0; i < SIRFSOC_TIMER_REG_CNT; i++)
		sirfsoc_timer_reg_val[i] =
			readl_relaxed(sirfsoc_timer_base +
				sirfsoc_timer_reg_list[i]);
}

static void sirfsoc_clocksource_resume(struct clocksource *cs)
{
	int i;

	for (i = 0; i < SIRFSOC_TIMER_REG_CNT - 2; i++)
		writel_relaxed(sirfsoc_timer_reg_val[i],
			sirfsoc_timer_base + sirfsoc_timer_reg_list[i]);

	writel_relaxed(sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT - 2],
		sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_LO);
	writel_relaxed(sirfsoc_timer_reg_val[SIRFSOC_TIMER_REG_CNT - 1],
		sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_HI);
}

static struct clock_event_device sirfsoc_clockevent = {
	.name = "sirfsoc_clockevent",
	.rating = 200,
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_state_shutdown = sirfsoc_timer_shutdown,
	.set_state_oneshot = sirfsoc_timer_set_oneshot,
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
	.flags = IRQF_TIMER,
	.irq = 0,
	.handler = sirfsoc_timer_interrupt,
	.dev_id = &sirfsoc_clockevent,
};

/* Overwrite weak default sched_clock with more precise one */
static u64 notrace sirfsoc_read_sched_clock(void)
{
	return sirfsoc_timer_read(NULL);
}

static void __init sirfsoc_clockevent_init(void)
{
	sirfsoc_clockevent.cpumask = cpumask_of(0);
	clockevents_config_and_register(&sirfsoc_clockevent, PRIMA2_CLOCK_FREQ,
					2, -2);
}

/* initialize the kernel jiffy timer source */
static int __init sirfsoc_prima2_timer_init(struct device_node *np)
{
	unsigned long rate;
	struct clk *clk;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("Failed to get clock\n");
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("Failed to enable clock\n");
		return ret;
	}

	rate = clk_get_rate(clk);

	if (rate < PRIMA2_CLOCK_FREQ || rate % PRIMA2_CLOCK_FREQ) {
		pr_err("Invalid clock rate\n");
		return -EINVAL;
	}

	sirfsoc_timer_base = of_iomap(np, 0);
	if (!sirfsoc_timer_base) {
		pr_err("unable to map timer cpu registers\n");
		return -ENXIO;
	}

	sirfsoc_timer_irq.irq = irq_of_parse_and_map(np, 0);

	writel_relaxed(rate / PRIMA2_CLOCK_FREQ / 2 - 1,
		sirfsoc_timer_base + SIRFSOC_TIMER_DIV);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_LO);
	writel_relaxed(0, sirfsoc_timer_base + SIRFSOC_TIMER_COUNTER_HI);
	writel_relaxed(BIT(0), sirfsoc_timer_base + SIRFSOC_TIMER_STATUS);

	ret = clocksource_register_hz(&sirfsoc_clocksource, PRIMA2_CLOCK_FREQ);
	if (ret) {
		pr_err("Failed to register clocksource\n");
		return ret;
	}

	sched_clock_register(sirfsoc_read_sched_clock, 64, PRIMA2_CLOCK_FREQ);

	ret = setup_irq(sirfsoc_timer_irq.irq, &sirfsoc_timer_irq);
	if (ret) {
		pr_err("Failed to setup irq\n");
		return ret;
	}

	sirfsoc_clockevent_init();

	return 0;
}
TIMER_OF_DECLARE(sirfsoc_prima2_timer,
	"sirf,prima2-tick", sirfsoc_prima2_timer_init);
