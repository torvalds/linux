/*
 * arch/arm/mach-ks8695/time.c
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/io.h>

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

/*
 * Returns number of ms since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long ks8695_gettimeoffset (void)
{
	unsigned long elapsed, tick2, intpending;

	/*
	 * Get the current number of ticks.  Note that there is a race
	 * condition between us reading the timer and checking for an
	 * interrupt.  We solve this by ensuring that the counter has not
	 * reloaded between our two reads.
	 */
	elapsed = __raw_readl(KS8695_TMR_VA + KS8695_T1TC) + __raw_readl(KS8695_TMR_VA + KS8695_T1PD);
	do {
		tick2 = elapsed;
		intpending = __raw_readl(KS8695_IRQ_VA + KS8695_INTST) & (1 << KS8695_IRQ_TIMER1);
		elapsed = __raw_readl(KS8695_TMR_VA + KS8695_T1TC) + __raw_readl(KS8695_TMR_VA + KS8695_T1PD);
	} while (elapsed > tick2);

	/* Convert to number of ticks expired (not remaining) */
	elapsed = (CLOCK_TICK_RATE / HZ) - elapsed;

	/* Is interrupt pending?  If so, then timer has been reloaded already. */
	if (intpending)
		elapsed += (CLOCK_TICK_RATE / HZ);

	/* Convert ticks to usecs */
	return (unsigned long)(elapsed * (tick_nsec / 1000)) / LATCH;
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t ks8695_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();
	return IRQ_HANDLED;
}

static struct irqaction ks8695_timer_irq = {
	.name		= "ks8695_tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= ks8695_timer_interrupt,
};

static void ks8695_timer_setup(void)
{
	unsigned long tmout = CLOCK_TICK_RATE / HZ;
	unsigned long tmcon;

	/* disable timer1 */
	tmcon = __raw_readl(KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(tmcon & ~TMCON_T1EN, KS8695_TMR_VA + KS8695_TMCON);

	__raw_writel(tmout / 2, KS8695_TMR_VA + KS8695_T1TC);
	__raw_writel(tmout / 2, KS8695_TMR_VA + KS8695_T1PD);

	/* re-enable timer1 */
	__raw_writel(tmcon | TMCON_T1EN, KS8695_TMR_VA + KS8695_TMCON);
}

static void __init ks8695_timer_init (void)
{
	ks8695_timer_setup();

	/* Enable timer interrupts */
	setup_irq(KS8695_IRQ_TIMER1, &ks8695_timer_irq);
}

struct sys_timer ks8695_timer = {
	.init		= ks8695_timer_init,
	.offset		= ks8695_gettimeoffset,
	.resume		= ks8695_timer_setup,
};

void ks8695_restart(char mode, const char *cmd)
{
	unsigned int reg;

	if (mode == 's')
		soft_restart(0);

	/* disable timer0 */
	reg = __raw_readl(KS8695_TMR_VA + KS8695_TMCON);
	__raw_writel(reg & ~TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);

	/* enable watchdog mode */
	__raw_writel((10 << 8) | T0TC_WATCHDOG, KS8695_TMR_VA + KS8695_T0TC);

	/* re-enable timer0 */
	__raw_writel(reg | TMCON_T0EN, KS8695_TMR_VA + KS8695_TMCON);
}
