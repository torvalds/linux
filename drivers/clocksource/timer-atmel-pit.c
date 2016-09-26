/*
 * at91sam926x_time.c - Periodic Interval Timer (PIT) for at91sam926x
 *
 * Copyright (C) 2005-2006 M. Amine SAYA, ATMEL Rousset, France
 * Revision	 2005 M. Nicolas Diremdjian, ATMEL Rousset, France
 * Converted to ClockSource/ClockEvents by David Brownell.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	"AT91: PIT: " fmt

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#define AT91_PIT_MR		0x00			/* Mode Register */
#define AT91_PIT_PITIEN			BIT(25)			/* Timer Interrupt Enable */
#define AT91_PIT_PITEN			BIT(24)			/* Timer Enabled */
#define AT91_PIT_PIV			GENMASK(19, 0)		/* Periodic Interval Value */

#define AT91_PIT_SR		0x04			/* Status Register */
#define AT91_PIT_PITS			BIT(0)			/* Timer Status */

#define AT91_PIT_PIVR		0x08			/* Periodic Interval Value Register */
#define AT91_PIT_PIIR		0x0c			/* Periodic Interval Image Register */
#define AT91_PIT_PICNT			GENMASK(31, 20)		/* Interval Counter */
#define AT91_PIT_CPIV			GENMASK(19, 0)		/* Inverval Value */

#define PIT_CPIV(x)	((x) & AT91_PIT_CPIV)
#define PIT_PICNT(x)	(((x) & AT91_PIT_PICNT) >> 20)

struct pit_data {
	struct clock_event_device	clkevt;
	struct clocksource		clksrc;

	void __iomem	*base;
	u32		cycle;
	u32		cnt;
	unsigned int	irq;
	struct clk	*mck;
};

static inline struct pit_data *clksrc_to_pit_data(struct clocksource *clksrc)
{
	return container_of(clksrc, struct pit_data, clksrc);
}

static inline struct pit_data *clkevt_to_pit_data(struct clock_event_device *clkevt)
{
	return container_of(clkevt, struct pit_data, clkevt);
}

static inline unsigned int pit_read(void __iomem *base, unsigned int reg_offset)
{
	return readl_relaxed(base + reg_offset);
}

static inline void pit_write(void __iomem *base, unsigned int reg_offset, unsigned long value)
{
	writel_relaxed(value, base + reg_offset);
}

/*
 * Clocksource:  just a monotonic counter of MCK/16 cycles.
 * We don't care whether or not PIT irqs are enabled.
 */
static cycle_t read_pit_clk(struct clocksource *cs)
{
	struct pit_data *data = clksrc_to_pit_data(cs);
	unsigned long flags;
	u32 elapsed;
	u32 t;

	raw_local_irq_save(flags);
	elapsed = data->cnt;
	t = pit_read(data->base, AT91_PIT_PIIR);
	raw_local_irq_restore(flags);

	elapsed += PIT_PICNT(t) * data->cycle;
	elapsed += PIT_CPIV(t);
	return elapsed;
}

static int pit_clkevt_shutdown(struct clock_event_device *dev)
{
	struct pit_data *data = clkevt_to_pit_data(dev);

	/* disable irq, leaving the clocksource active */
	pit_write(data->base, AT91_PIT_MR, (data->cycle - 1) | AT91_PIT_PITEN);
	return 0;
}

/*
 * Clockevent device:  interrupts every 1/HZ (== pit_cycles * MCK/16)
 */
static int pit_clkevt_set_periodic(struct clock_event_device *dev)
{
	struct pit_data *data = clkevt_to_pit_data(dev);

	/* update clocksource counter */
	data->cnt += data->cycle * PIT_PICNT(pit_read(data->base, AT91_PIT_PIVR));
	pit_write(data->base, AT91_PIT_MR,
		  (data->cycle - 1) | AT91_PIT_PITEN | AT91_PIT_PITIEN);
	return 0;
}

static void at91sam926x_pit_suspend(struct clock_event_device *cedev)
{
	struct pit_data *data = clkevt_to_pit_data(cedev);

	/* Disable timer */
	pit_write(data->base, AT91_PIT_MR, 0);
}

static void at91sam926x_pit_reset(struct pit_data *data)
{
	/* Disable timer and irqs */
	pit_write(data->base, AT91_PIT_MR, 0);

	/* Clear any pending interrupts, wait for PIT to stop counting */
	while (PIT_CPIV(pit_read(data->base, AT91_PIT_PIVR)) != 0)
		cpu_relax();

	/* Start PIT but don't enable IRQ */
	pit_write(data->base, AT91_PIT_MR,
		  (data->cycle - 1) | AT91_PIT_PITEN);
}

static void at91sam926x_pit_resume(struct clock_event_device *cedev)
{
	struct pit_data *data = clkevt_to_pit_data(cedev);

	at91sam926x_pit_reset(data);
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91sam926x_pit_interrupt(int irq, void *dev_id)
{
	struct pit_data *data = dev_id;

	/*
	 * irqs should be disabled here, but as the irq is shared they are only
	 * guaranteed to be off if the timer irq is registered first.
	 */
	WARN_ON_ONCE(!irqs_disabled());

	/* The PIT interrupt may be disabled, and is shared */
	if (clockevent_state_periodic(&data->clkevt) &&
	    (pit_read(data->base, AT91_PIT_SR) & AT91_PIT_PITS)) {
		unsigned nr_ticks;

		/* Get number of ticks performed before irq, and ack it */
		nr_ticks = PIT_PICNT(pit_read(data->base, AT91_PIT_PIVR));
		do {
			data->cnt += data->cycle;
			data->clkevt.event_handler(&data->clkevt);
			nr_ticks--;
		} while (nr_ticks);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/*
 * Set up both clocksource and clockevent support.
 */
static int __init at91sam926x_pit_common_init(struct pit_data *data)
{
	unsigned long	pit_rate;
	unsigned	bits;
	int		ret;

	/*
	 * Use our actual MCK to figure out how many MCK/16 ticks per
	 * 1/HZ period (instead of a compile-time constant LATCH).
	 */
	pit_rate = clk_get_rate(data->mck) / 16;
	data->cycle = DIV_ROUND_CLOSEST(pit_rate, HZ);
	WARN_ON(((data->cycle - 1) & ~AT91_PIT_PIV) != 0);

	/* Initialize and enable the timer */
	at91sam926x_pit_reset(data);

	/*
	 * Register clocksource.  The high order bits of PIV are unused,
	 * so this isn't a 32-bit counter unless we get clockevent irqs.
	 */
	bits = 12 /* PICNT */ + ilog2(data->cycle) /* PIV */;
	data->clksrc.mask = CLOCKSOURCE_MASK(bits);
	data->clksrc.name = "pit";
	data->clksrc.rating = 175;
	data->clksrc.read = read_pit_clk;
	data->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	
	ret = clocksource_register_hz(&data->clksrc, pit_rate);
	if (ret) {
		pr_err("Failed to register clocksource");
		return ret;
	}

	/* Set up irq handler */
	ret = request_irq(data->irq, at91sam926x_pit_interrupt,
			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
			  "at91_tick", data);
	if (ret) {
		pr_err("Unable to setup IRQ\n");
		return ret;
	}

	/* Set up and register clockevents */
	data->clkevt.name = "pit";
	data->clkevt.features = CLOCK_EVT_FEAT_PERIODIC;
	data->clkevt.shift = 32;
	data->clkevt.mult = div_sc(pit_rate, NSEC_PER_SEC, data->clkevt.shift);
	data->clkevt.rating = 100;
	data->clkevt.cpumask = cpumask_of(0);

	data->clkevt.set_state_shutdown = pit_clkevt_shutdown;
	data->clkevt.set_state_periodic = pit_clkevt_set_periodic;
	data->clkevt.resume = at91sam926x_pit_resume;
	data->clkevt.suspend = at91sam926x_pit_suspend;
	clockevents_register_device(&data->clkevt);

	return 0;
}

static int __init at91sam926x_pit_dt_init(struct device_node *node)
{
	struct pit_data *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base = of_iomap(node, 0);
	if (!data->base) {
		pr_err("Could not map PIT address\n");
		return -ENXIO;
	}

	data->mck = of_clk_get(node, 0);
	if (IS_ERR(data->mck))
		/* Fallback on clkdev for !CCF-based boards */
		data->mck = clk_get(NULL, "mck");

	if (IS_ERR(data->mck)) {
		pr_err("Unable to get mck clk\n");
		return PTR_ERR(data->mck);
	}

	ret = clk_prepare_enable(data->mck);
	if (ret) {
		pr_err("Unable to enable mck\n");
		return ret;
	}

	/* Get the interrupts property */
	data->irq = irq_of_parse_and_map(node, 0);
	if (!data->irq) {
		pr_err("Unable to get IRQ from DT\n");
		return -EINVAL;
	}

	return at91sam926x_pit_common_init(data);
}
CLOCKSOURCE_OF_DECLARE(at91sam926x_pit, "atmel,at91sam9260-pit",
		       at91sam926x_pit_dt_init);
