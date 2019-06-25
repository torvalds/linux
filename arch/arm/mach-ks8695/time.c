// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-ks8695/time.c
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/clockchips.h>

#include <asm/mach/time.h>
#include <asm/system_misc.h>

#include <mach/regs-irq.h>

#include "generic.h"

#define KS8695_TMR_OFFSET	(0xF0000 + 0xE400)
#define KS8695_TMR_VA		(KS8695_IO_VA + KS8695_TMR_OFFSET)
#define KS8695_TMR_PA		(KS8695_IO_PA + KS8695_TMR_OFFSET)

/*
 * Timer registers
 */
#define KS8695_TMCON		(0x00)		/* Timer Control Register */
#define KS8695_T1TC		(0x04)		/* Timer 1 Timeout Count Register */
#define KS8695_T0TC		(0x08)		/* Timer 0 Timeout Count Register */
#define KS8695_T1PD		(0x0C)		/* Timer 1 Pulse Count Register */
#define KS8695_T0PD		(0x10)		/* Timer 0 Pulse Count Register */

/* Timer Control Register */
#define TMCON_T1EN		(1 << 1)	/* Timer 1 Enable */
#define TMCON_T0EN		(1 << 0)	/* Timer 0 Enable */

/* Timer0 Timeout Counter Register */
#define T0TC_WATCHDOG		(0xff)		/* Enable watchdog mode */

static int ks8695_set_periodic(struct clock_event_device *evt)
{
	u32 rate = DIV_ROUND_CLOSEST(KS8695_CLOCK_RATE, HZ);
	u32 half = DIV_ROUND_CLOSEST(rate, 2);
	u32 tmcon;

	/* Disable timer 1 */
	tmcon = readl_relaxed(KS8695_TMR_VA + KS8695_TMCON);
	tmcon &= ~TMCON_T1EN;
	writel_relaxed(tmcon, KS8695_TMR_VA + KS8695_TMCON);

	/* Both registers need to count down */
	writel_relaxed(half, KS8695_TMR_VA + KS8695_T1TC);
	writel_relaxed(half, KS8695_TMR_VA + KS8695_T1PD);

	/* Re-enable timer1 */
	tmcon |= TMCON_T1EN;
	writel_relaxed(tmcon, KS8695_TMR_VA + KS8695_TMCON);
	return 0;
}

static int ks8695_set_next_event(unsigned long cycles,
				 struct clock_event_device *evt)

{
	u32 half = DIV_ROUND_CLOSEST(cycles, 2);
	u32 tmcon;

	/* Disable timer 1 */
	tmcon = readl_relaxed(KS8695_TMR_VA + KS8695_TMCON);
	tmcon &= ~TMCON_T1EN;
	writel_relaxed(tmcon, KS8695_TMR_VA + KS8695_TMCON);

	/* Both registers need to count down */
	writel_relaxed(half, KS8695_TMR_VA + KS8695_T1TC);
	writel_relaxed(half, KS8695_TMR_VA + KS8695_T1PD);

	/* Re-enable timer1 */
	tmcon |= TMCON_T1EN;
	writel_relaxed(tmcon, KS8695_TMR_VA + KS8695_TMCON);

	return 0;
}

static struct clock_event_device clockevent_ks8695 = {
	.name			= "ks8695_t1tc",
	/* Reasonably fast and accurate clock event */
	.rating			= 300,
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.set_next_event		= ks8695_set_next_event,
	.set_state_periodic	= ks8695_set_periodic,
};

/*
 * IRQ handler for the timer.
 */
static irqreturn_t ks8695_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_ks8695;

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction ks8695_timer_irq = {
	.name		= "ks8695_tick",
	.flags		= IRQF_TIMER,
	.handler	= ks8695_timer_interrupt,
};

static void ks8695_timer_setup(void)
{
	unsigned long tmcon;

	/* Disable timer 0 and 1 */
	tmcon = readl_relaxed(KS8695_TMR_VA + KS8695_TMCON);
	tmcon &= ~TMCON_T0EN;
	tmcon &= ~TMCON_T1EN;
	writel_relaxed(tmcon, KS8695_TMR_VA + KS8695_TMCON);

	/*
	 * Use timer 1 to fire IRQs on the timeline, minimum 2 cycles
	 * (one on each counter) maximum 2*2^32, but the API will only
	 * accept up to a 32bit full word (0xFFFFFFFFU).
	 */
	clockevents_config_and_register(&clockevent_ks8695,
					KS8695_CLOCK_RATE, 2,
					0xFFFFFFFFU);
}

void __init ks8695_timer_init(void)
{
	ks8695_timer_setup();

	/* Enable timer interrupts */
	setup_irq(KS8695_IRQ_TIMER1, &ks8695_timer_irq);
}

void ks8695_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	unsigned int reg;

	if (reboot_mode == REBOOT_SOFT)
		soft_restart(0);

	/* disable timer0 */
	reg = readl_relaxed(KS8695_TMR_VA + KS8695_TMCON);
	writel_relaxed(reg & ~TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);

	/* enable watchdog mode */
	writel_relaxed((10 << 8) | T0TC_WATCHDOG, KS8695_TMR_VA + KS8695_T0TC);

	/* re-enable timer0 */
	writel_relaxed(reg | TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);
}
