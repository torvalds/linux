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

#include <asm/mach/time.h>

#include <mach/at91_pit.h>


#define PIT_CPIV(x)	((x) & AT91_PIT_CPIV)
#define PIT_PICNT(x)	(((x) & AT91_PIT_PICNT) >> 20)

static u32 pit_cycle;		/* write-once */
static u32 pit_cnt;		/* access only w/system irq blocked */


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
	t = at91_sys_read(AT91_PIT_PIIR);
	raw_local_irq_restore(flags);

	elapsed += PIT_PICNT(t) * pit_cycle;
	elapsed += PIT_CPIV(t);
	return elapsed;
}

static struct clocksource pit_clk = {
	.name		= "pit",
	.rating		= 175,
	.read		= read_pit_clk,
	.shift		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};


/*
 * Clockevent device:  interrupts every 1/HZ (== pit_cycles * MCK/16)
 */
static void
pit_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	unsigned long	flags;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* update clocksource counter, then enable the IRQ */
		raw_local_irq_save(flags);
		pit_cnt += pit_cycle * PIT_PICNT(at91_sys_read(AT91_PIT_PIVR));
		at91_sys_write(AT91_PIT_MR, (pit_cycle - 1) | AT91_PIT_PITEN
				| AT91_PIT_PITIEN);
		raw_local_irq_restore(flags);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		BUG();
		/* FALLTHROUGH */
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		/* disable irq, leaving the clocksource active */
		at91_sys_write(AT91_PIT_MR, (pit_cycle - 1) | AT91_PIT_PITEN);
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static struct clock_event_device pit_clkevt = {
	.name		= "pit",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 100,
	.set_mode	= pit_clkevt_mode,
};


/*
 * IRQ handler for the timer.
 */
static irqreturn_t at91sam926x_pit_interrupt(int irq, void *dev_id)
{

	/* The PIT interrupt may be disabled, and is shared */
	if ((pit_clkevt.mode == CLOCK_EVT_MODE_PERIODIC)
			&& (at91_sys_read(AT91_PIT_SR) & AT91_PIT_PITS)) {
		unsigned nr_ticks;

		/* Get number of ticks performed before irq, and ack it */
		nr_ticks = PIT_PICNT(at91_sys_read(AT91_PIT_PIVR));
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
	.flags		= IRQF_SHARED | IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= at91sam926x_pit_interrupt
};

static void at91sam926x_pit_reset(void)
{
	/* Disable timer and irqs */
	at91_sys_write(AT91_PIT_MR, 0);

	/* Clear any pending interrupts, wait for PIT to stop counting */
	while (PIT_CPIV(at91_sys_read(AT91_PIT_PIVR)) != 0)
		cpu_relax();

	/* Start PIT but don't enable IRQ */
	at91_sys_write(AT91_PIT_MR, (pit_cycle - 1) | AT91_PIT_PITEN);
}

/*
 * Set up both clocksource and clockevent support.
 */
static void __init at91sam926x_pit_init(void)
{
	unsigned long	pit_rate;
	unsigned	bits;

	/*
	 * Use our actual MCK to figure out how many MCK/16 ticks per
	 * 1/HZ period (instead of a compile-time constant LATCH).
	 */
	pit_rate = clk_get_rate(clk_get(NULL, "mck")) / 16;
	pit_cycle = (pit_rate + HZ/2) / HZ;
	WARN_ON(((pit_cycle - 1) & ~AT91_PIT_PIV) != 0);

	/* Initialize and enable the timer */
	at91sam926x_pit_reset();

	/*
	 * Register clocksource.  The high order bits of PIV are unused,
	 * so this isn't a 32-bit counter unless we get clockevent irqs.
	 */
	pit_clk.mult = clocksource_hz2mult(pit_rate, pit_clk.shift);
	bits = 12 /* PICNT */ + ilog2(pit_cycle) /* PIV */;
	pit_clk.mask = CLOCKSOURCE_MASK(bits);
	clocksource_register(&pit_clk);

	/* Set up irq handler */
	setup_irq(AT91_ID_SYS, &at91sam926x_pit_irq);

	/* Set up and register clockevents */
	pit_clkevt.mult = div_sc(pit_rate, NSEC_PER_SEC, pit_clkevt.shift);
	pit_clkevt.cpumask = cpumask_of(0);
	clockevents_register_device(&pit_clkevt);
}

static void at91sam926x_pit_suspend(void)
{
	/* Disable timer */
	at91_sys_write(AT91_PIT_MR, 0);
}

struct sys_timer at91sam926x_timer = {
	.init		= at91sam926x_pit_init,
	.suspend	= at91sam926x_pit_suspend,
	.resume		= at91sam926x_pit_reset,
};
