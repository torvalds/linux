/*
 *  linux/arch/arm/mach-imx/time.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/clocksource.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/irq.h>
#include <asm/mach/time.h>

/* Use timer 1 as system timer */
#define TIMER_BASE IMX_TIM1_BASE

static unsigned long evt_diff;

/*
 * IRQ handler for the timer
 */
static irqreturn_t
imx_timer_interrupt(int irq, void *dev_id)
{
	uint32_t tstat;

	/* clear the interrupt */
	tstat = IMX_TSTAT(TIMER_BASE);
	IMX_TSTAT(TIMER_BASE) = 0;

	if (tstat & TSTAT_COMP) {
		do {

			write_seqlock(&xtime_lock);
			timer_tick();
			write_sequnlock(&xtime_lock);
			IMX_TCMP(TIMER_BASE) += evt_diff;

		} while (unlikely((int32_t)(IMX_TCMP(TIMER_BASE)
					- IMX_TCN(TIMER_BASE)) < 0));
	}

	return IRQ_HANDLED;
}

static struct irqaction imx_timer_irq = {
	.name		= "i.MX Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= imx_timer_interrupt,
};

/*
 * Set up timer hardware into expected mode and state.
 */
static void __init imx_timer_hardware_init(void)
{
	/*
	 * Initialise to a known state (all timers off, and timing reset)
	 */
	IMX_TCTL(TIMER_BASE) = 0;
	IMX_TPRER(TIMER_BASE) = 0;
	IMX_TCMP(TIMER_BASE) = LATCH - 1;

	IMX_TCTL(TIMER_BASE) = TCTL_FRR | TCTL_CLK_PCLK1 | TCTL_IRQEN | TCTL_TEN;
	evt_diff = LATCH;
}

cycle_t imx_get_cycles(void)
{
	return IMX_TCN(TIMER_BASE);
}

static struct clocksource clocksource_imx = {
	.name 		= "imx_timer1",
	.rating		= 200,
	.read		= imx_get_cycles,
	.mask		= 0xFFFFFFFF,
	.shift 		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init imx_clocksource_init(void)
{
	clocksource_imx.mult =
		clocksource_hz2mult(imx_get_perclk1(), clocksource_imx.shift);
	clocksource_register(&clocksource_imx);

	return 0;
}

static void __init imx_timer_init(void)
{
	imx_timer_hardware_init();
	imx_clocksource_init();

	/*
	 * Make irqs happen for the system timer
	 */
	setup_irq(TIM1_INT, &imx_timer_irq);
}

struct sys_timer imx_timer = {
	.init		= imx_timer_init,
};
