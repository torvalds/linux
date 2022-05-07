// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/arch/arm/mach-at91/at91rm9200_time.c
 *
 *  Copyright (C) 2003 SAN People
 *  Copyright (C) 2003 ATMEL
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/export.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/atmel-st.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

static unsigned long last_crtr;
static u32 irqmask;
static struct clock_event_device clkevt;
static struct regmap *regmap_st;
static int timer_latch;

/*
 * The ST_CRTR is updated asynchronously to the master clock ... but
 * the updates as seen by the CPU don't seem to be strictly monotonic.
 * Waiting until we read the same value twice avoids glitching.
 */
static inline unsigned long read_CRTR(void)
{
	unsigned int x1, x2;

	regmap_read(regmap_st, AT91_ST_CRTR, &x1);
	do {
		regmap_read(regmap_st, AT91_ST_CRTR, &x2);
		if (x1 == x2)
			break;
		x1 = x2;
	} while (1);
	return x1;
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91rm9200_timer_interrupt(int irq, void *dev_id)
{
	u32 sr;

	regmap_read(regmap_st, AT91_ST_SR, &sr);
	sr &= irqmask;

	/*
	 * irqs should be disabled here, but as the irq is shared they are only
	 * guaranteed to be off if the timer irq is registered first.
	 */
	WARN_ON_ONCE(!irqs_disabled());

	/* simulate "oneshot" timer with alarm */
	if (sr & AT91_ST_ALMS) {
		clkevt.event_handler(&clkevt);
		return IRQ_HANDLED;
	}

	/* periodic mode should handle delayed ticks */
	if (sr & AT91_ST_PITS) {
		u32	crtr = read_CRTR();

		while (((crtr - last_crtr) & AT91_ST_CRTV) >= timer_latch) {
			last_crtr += timer_latch;
			clkevt.event_handler(&clkevt);
		}
		return IRQ_HANDLED;
	}

	/* this irq is shared ... */
	return IRQ_NONE;
}

static u64 read_clk32k(struct clocksource *cs)
{
	return read_CRTR();
}

static struct clocksource clk32k = {
	.name		= "32k_counter",
	.rating		= 150,
	.read		= read_clk32k,
	.mask		= CLOCKSOURCE_MASK(20),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void clkdev32k_disable_and_flush_irq(void)
{
	unsigned int val;

	/* Disable and flush pending timer interrupts */
	regmap_write(regmap_st, AT91_ST_IDR, AT91_ST_PITS | AT91_ST_ALMS);
	regmap_read(regmap_st, AT91_ST_SR, &val);
	last_crtr = read_CRTR();
}

static int clkevt32k_shutdown(struct clock_event_device *evt)
{
	clkdev32k_disable_and_flush_irq();
	irqmask = 0;
	regmap_write(regmap_st, AT91_ST_IER, irqmask);
	return 0;
}

static int clkevt32k_set_oneshot(struct clock_event_device *dev)
{
	clkdev32k_disable_and_flush_irq();

	/*
	 * ALM for oneshot irqs, set by next_event()
	 * before 32 seconds have passed.
	 */
	irqmask = AT91_ST_ALMS;
	regmap_write(regmap_st, AT91_ST_RTAR, last_crtr);
	regmap_write(regmap_st, AT91_ST_IER, irqmask);
	return 0;
}

static int clkevt32k_set_periodic(struct clock_event_device *dev)
{
	clkdev32k_disable_and_flush_irq();

	/* PIT for periodic irqs; fixed rate of 1/HZ */
	irqmask = AT91_ST_PITS;
	regmap_write(regmap_st, AT91_ST_PIMR, timer_latch);
	regmap_write(regmap_st, AT91_ST_IER, irqmask);
	return 0;
}

static int
clkevt32k_next_event(unsigned long delta, struct clock_event_device *dev)
{
	u32		alm;
	unsigned int	val;

	BUG_ON(delta < 2);

	/* The alarm IRQ uses absolute time (now+delta), not the relative
	 * time (delta) in our calling convention.  Like all clockevents
	 * using such "match" hardware, we have a race to defend against.
	 *
	 * Our defense here is to have set up the clockevent device so the
	 * delta is at least two.  That way we never end up writing RTAR
	 * with the value then held in CRTR ... which would mean the match
	 * wouldn't trigger until 32 seconds later, after CRTR wraps.
	 */
	alm = read_CRTR();

	/* Cancel any pending alarm; flush any pending IRQ */
	regmap_write(regmap_st, AT91_ST_RTAR, alm);
	regmap_read(regmap_st, AT91_ST_SR, &val);

	/* Schedule alarm by writing RTAR. */
	alm += delta;
	regmap_write(regmap_st, AT91_ST_RTAR, alm);

	return 0;
}

static struct clock_event_device clkevt = {
	.name			= "at91_tick",
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 150,
	.set_next_event		= clkevt32k_next_event,
	.set_state_shutdown	= clkevt32k_shutdown,
	.set_state_periodic	= clkevt32k_set_periodic,
	.set_state_oneshot	= clkevt32k_set_oneshot,
	.tick_resume		= clkevt32k_shutdown,
};

/*
 * ST (system timer) module supports both clockevents and clocksource.
 */
static int __init atmel_st_timer_init(struct device_node *node)
{
	struct clk *sclk;
	unsigned int sclk_rate, val;
	int irq, ret;

	regmap_st = syscon_node_to_regmap(node);
	if (IS_ERR(regmap_st)) {
		pr_err("Unable to get regmap\n");
		return PTR_ERR(regmap_st);
	}

	/* Disable all timer interrupts, and clear any pending ones */
	regmap_write(regmap_st, AT91_ST_IDR,
		AT91_ST_PITS | AT91_ST_WDOVF | AT91_ST_RTTINC | AT91_ST_ALMS);
	regmap_read(regmap_st, AT91_ST_SR, &val);

	/* Get the interrupts property */
	irq  = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("Unable to get IRQ from DT\n");
		return -EINVAL;
	}

	/* Make IRQs happen for the system timer */
	ret = request_irq(irq, at91rm9200_timer_interrupt,
			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
			  "at91_tick", regmap_st);
	if (ret) {
		pr_err("Unable to setup IRQ\n");
		return ret;
	}

	sclk = of_clk_get(node, 0);
	if (IS_ERR(sclk)) {
		pr_err("Unable to get slow clock\n");
		return PTR_ERR(sclk);
	}

	ret = clk_prepare_enable(sclk);
	if (ret) {
		pr_err("Could not enable slow clock\n");
		return ret;
	}

	sclk_rate = clk_get_rate(sclk);
	if (!sclk_rate) {
		pr_err("Invalid slow clock rate\n");
		return -EINVAL;
	}
	timer_latch = (sclk_rate + HZ / 2) / HZ;

	/* The 32KiHz "Slow Clock" (tick every 30517.58 nanoseconds) is used
	 * directly for the clocksource and all clockevents, after adjusting
	 * its prescaler from the 1 Hz default.
	 */
	regmap_write(regmap_st, AT91_ST_RTMR, 1);

	/* Setup timer clockevent, with minimum of two ticks (important!!) */
	clkevt.cpumask = cpumask_of(0);
	clockevents_config_and_register(&clkevt, sclk_rate,
					2, AT91_ST_ALMV);

	/* register clocksource */
	return clocksource_register_hz(&clk32k, sclk_rate);
}
TIMER_OF_DECLARE(atmel_st_timer, "atmel,at91rm9200-st",
		       atmel_st_timer_init);
