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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/mach/time.h>
#include <mach/hardware.h>

#define AT91_PIT_MR		0x00			/* Mode Register */
#define		AT91_PIT_PITIEN		(1 << 25)		/* Timer Interrupt Enable */
#define		AT91_PIT_PITEN		(1 << 24)		/* Timer Enabled */
#define		AT91_PIT_PIV		(0xfffff)		/* Periodic Interval Value */

#define AT91_PIT_SR		0x04			/* Status Register */
#define		AT91_PIT_PITS		(1 << 0)		/* Timer Status */

#define AT91_PIT_PIVR		0x08			/* Periodic Interval Value Register */
#define AT91_PIT_PIIR		0x0c			/* Periodic Interval Image Register */
#define		AT91_PIT_PICNT		(0xfff << 20)		/* Interval Counter */
#define		AT91_PIT_CPIV		(0xfffff)		/* Inverval Value */

#define PIT_CPIV(x)	((x) & AT91_PIT_CPIV)
#define PIT_PICNT(x)	(((x) & AT91_PIT_PICNT) >> 20)

static u32 pit_cycle;		/* write-once */
static u32 pit_cnt;		/* access only w/system irq blocked */
static void __iomem *pit_base_addr __read_mostly;
static struct clk *mck;

static inline unsigned int pit_read(unsigned int reg_offset)
{
	return __raw_readl(pit_base_addr + reg_offset);
}

static inline void pit_write(unsigned int reg_offset, unsigned long value)
{
	__raw_writel(value, pit_base_addr + reg_offset);
}

/*
 * Clocksource:  just a monotonic counter of MCK/16 cycles.
 * We don't care whether or not PIT irqs are enabled.
 */
static cycle_t read_pit_clk(struct clocksource *cs)
{
	unsigned long flags;
	u32 elapsed;
	u32 t;

	raw_local_irq_save(flags);
	elapsed = pit_cnt;
	t = pit_read(AT91_PIT_PIIR);
	raw_local_irq_restore(flags);

	elapsed += PIT_PICNT(t) * pit_cycle;
	elapsed += PIT_CPIV(t);
	return elapsed;
}

static struct clocksource pit_clk = {
	.name		= "pit",
	.rating		= 175,
	.read		= read_pit_clk,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};


/*
 * Clockevent device:  interrupts every 1/HZ (== pit_cycles * MCK/16)
 */
static void
pit_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* update clocksource counter */
		pit_cnt += pit_cycle * PIT_PICNT(pit_read(AT91_PIT_PIVR));
		pit_write(AT91_PIT_MR, (pit_cycle - 1) | AT91_PIT_PITEN
				| AT91_PIT_PITIEN);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		BUG();
		/* FALLTHROUGH */
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		/* disable irq, leaving the clocksource active */
		pit_write(AT91_PIT_MR, (pit_cycle - 1) | AT91_PIT_PITEN);
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static void at91sam926x_pit_suspend(struct clock_event_device *cedev)
{
	/* Disable timer */
	pit_write(AT91_PIT_MR, 0);
}

static void at91sam926x_pit_reset(void)
{
	/* Disable timer and irqs */
	pit_write(AT91_PIT_MR, 0);

	/* Clear any pending interrupts, wait for PIT to stop counting */
	while (PIT_CPIV(pit_read(AT91_PIT_PIVR)) != 0)
		cpu_relax();

	/* Start PIT but don't enable IRQ */
	pit_write(AT91_PIT_MR, (pit_cycle - 1) | AT91_PIT_PITEN);
}

static void at91sam926x_pit_resume(struct clock_event_device *cedev)
{
	at91sam926x_pit_reset();
}

static struct clock_event_device pit_clkevt = {
	.name		= "pit",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 100,
	.set_mode	= pit_clkevt_mode,
	.suspend	= at91sam926x_pit_suspend,
	.resume		= at91sam926x_pit_resume,
};


/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91sam926x_pit_interrupt(int irq, void *dev_id)
{
	/*
	 * irqs should be disabled here, but as the irq is shared they are only
	 * guaranteed to be off if the timer irq is registered first.
	 */
	WARN_ON_ONCE(!irqs_disabled());

	/* The PIT interrupt may be disabled, and is shared */
	if ((pit_clkevt.mode == CLOCK_EVT_MODE_PERIODIC)
			&& (pit_read(AT91_PIT_SR) & AT91_PIT_PITS)) {
		unsigned nr_ticks;

		/* Get number of ticks performed before irq, and ack it */
		nr_ticks = PIT_PICNT(pit_read(AT91_PIT_PIVR));
		do {
			pit_cnt += pit_cycle;
			pit_clkevt.event_handler(&pit_clkevt);
			nr_ticks--;
		} while (nr_ticks);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct irqaction at91sam926x_pit_irq = {
	.name		= "at91_tick",
	.flags		= IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= at91sam926x_pit_interrupt,
	.irq		= NR_IRQS_LEGACY + AT91_ID_SYS,
};

#ifdef CONFIG_OF
static struct of_device_id pit_timer_ids[] = {
	{ .compatible = "atmel,at91sam9260-pit" },
	{ /* sentinel */ }
};

static int __init of_at91sam926x_pit_init(void)
{
	struct device_node	*np;
	int			ret;

	np = of_find_matching_node(NULL, pit_timer_ids);
	if (!np)
		goto err;

	pit_base_addr = of_iomap(np, 0);
	if (!pit_base_addr)
		goto node_err;

	mck = of_clk_get(np, 0);

	/* Get the interrupts property */
	ret = irq_of_parse_and_map(np, 0);
	if (!ret) {
		pr_crit("AT91: PIT: Unable to get IRQ from DT\n");
		if (!IS_ERR(mck))
			clk_put(mck);
		goto ioremap_err;
	}
	at91sam926x_pit_irq.irq = ret;

	of_node_put(np);

	return 0;

ioremap_err:
	iounmap(pit_base_addr);
node_err:
	of_node_put(np);
err:
	return -EINVAL;
}
#else
static int __init of_at91sam926x_pit_init(void)
{
	return -EINVAL;
}
#endif

/*
 * Set up both clocksource and clockevent support.
 */
void __init at91sam926x_pit_init(void)
{
	unsigned long	pit_rate;
	unsigned	bits;
	int		ret;

	mck = ERR_PTR(-ENOENT);

	/* For device tree enabled device: initialize here */
	of_at91sam926x_pit_init();

	/*
	 * Use our actual MCK to figure out how many MCK/16 ticks per
	 * 1/HZ period (instead of a compile-time constant LATCH).
	 */
	if (IS_ERR(mck))
		mck = clk_get(NULL, "mck");

	if (IS_ERR(mck))
		panic("AT91: PIT: Unable to get mck clk\n");
	pit_rate = clk_get_rate(mck) / 16;
	pit_cycle = (pit_rate + HZ/2) / HZ;
	WARN_ON(((pit_cycle - 1) & ~AT91_PIT_PIV) != 0);

	/* Initialize and enable the timer */
	at91sam926x_pit_reset();

	/*
	 * Register clocksource.  The high order bits of PIV are unused,
	 * so this isn't a 32-bit counter unless we get clockevent irqs.
	 */
	bits = 12 /* PICNT */ + ilog2(pit_cycle) /* PIV */;
	pit_clk.mask = CLOCKSOURCE_MASK(bits);
	clocksource_register_hz(&pit_clk, pit_rate);

	/* Set up irq handler */
	ret = setup_irq(at91sam926x_pit_irq.irq, &at91sam926x_pit_irq);
	if (ret)
		pr_crit("AT91: PIT: Unable to setup IRQ\n");

	/* Set up and register clockevents */
	pit_clkevt.mult = div_sc(pit_rate, NSEC_PER_SEC, pit_clkevt.shift);
	pit_clkevt.cpumask = cpumask_of(0);
	clockevents_register_device(&pit_clkevt);
}

void __init at91sam926x_ioremap_pit(u32 addr)
{
#if defined(CONFIG_OF)
	struct device_node *np =
		of_find_matching_node(NULL, pit_timer_ids);

	if (np) {
		of_node_put(np);
		return;
	}
#endif
	pit_base_addr = ioremap(addr, 16);

	if (!pit_base_addr)
		panic("Impossible to ioremap PIT\n");
}
