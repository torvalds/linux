/*
 * arch/arm/mach-pnx4008/time.c
 *
 * PNX4008 Timers
 *
 * Authors: Vitaly Wool, Dmitry Chigirev, Grigory Tolstolytkin <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/leds.h>
#include <asm/mach/time.h>
#include <asm/errno.h>

#include "time.h"

/*! Note: all timers are UPCOUNTING */

/*!
 * Returns number of us since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 */
static unsigned long pnx4008_gettimeoffset(void)
{
	u32 ticks_to_match =
	    __raw_readl(HSTIM_MATCH0) - __raw_readl(HSTIM_COUNTER);
	u32 elapsed = LATCH - ticks_to_match;
	return (elapsed * (tick_nsec / 1000)) / LATCH;
}

/*!
 * IRQ handler for the timer
 */
static irqreturn_t pnx4008_timer_interrupt(int irq, void *dev_id)
{
	if (__raw_readl(HSTIM_INT) & MATCH0_INT) {

		do {
			timer_tick();

			/*
			 * this algorithm takes care of possible delay
			 * for this interrupt handling longer than a normal
			 * timer period
			 */
			__raw_writel(__raw_readl(HSTIM_MATCH0) + LATCH,
				     HSTIM_MATCH0);
			__raw_writel(MATCH0_INT, HSTIM_INT);	/* clear interrupt */

			/*
			 * The goal is to keep incrementing HSTIM_MATCH0
			 * register until HSTIM_MATCH0 indicates time after
			 * what HSTIM_COUNTER indicates.
			 */
		} while ((signed)
			 (__raw_readl(HSTIM_MATCH0) -
			  __raw_readl(HSTIM_COUNTER)) < 0);
	}

	return IRQ_HANDLED;
}

static struct irqaction pnx4008_timer_irq = {
	.name = "PNX4008 Tick Timer",
	.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = pnx4008_timer_interrupt
};

/*!
 * Set up timer and timer interrupt.
 */
static __init void pnx4008_setup_timer(void)
{
	__raw_writel(RESET_COUNT, MSTIM_CTRL);
	while (__raw_readl(MSTIM_COUNTER)) ;	/* wait for reset to complete. 100% guarantee event */
	__raw_writel(0, MSTIM_CTRL);	/* stop the timer */
	__raw_writel(0, MSTIM_MCTRL);

	__raw_writel(RESET_COUNT, HSTIM_CTRL);
	while (__raw_readl(HSTIM_COUNTER)) ;	/* wait for reset to complete. 100% guarantee event */
	__raw_writel(0, HSTIM_CTRL);
	__raw_writel(0, HSTIM_MCTRL);
	__raw_writel(0, HSTIM_CCR);
	__raw_writel(12, HSTIM_PMATCH);	/* scale down to 1 MHZ */
	__raw_writel(LATCH, HSTIM_MATCH0);
	__raw_writel(MR0_INT, HSTIM_MCTRL);

	setup_irq(HSTIMER_INT, &pnx4008_timer_irq);

	__raw_writel(COUNT_ENAB | DEBUG_EN, HSTIM_CTRL);	/*start timer, stop when JTAG active */
}

/* Timer Clock Control in PM register */
#define TIMCLK_CTRL_REG  IO_ADDRESS((PNX4008_PWRMAN_BASE + 0xBC))
#define WATCHDOG_CLK_EN                   1
#define TIMER_CLK_EN                      2	/* HS and MS timers? */

static u32 timclk_ctrl_reg_save;

void pnx4008_timer_suspend(void)
{
	timclk_ctrl_reg_save = __raw_readl(TIMCLK_CTRL_REG);
	__raw_writel(0, TIMCLK_CTRL_REG);	/* disable timers */
}

void pnx4008_timer_resume(void)
{
	__raw_writel(timclk_ctrl_reg_save, TIMCLK_CTRL_REG);	/* enable timers */
}

struct sys_timer pnx4008_timer = {
	.init = pnx4008_setup_timer,
	.offset = pnx4008_gettimeoffset,
	.suspend = pnx4008_timer_suspend,
	.resume = pnx4008_timer_resume,
};

