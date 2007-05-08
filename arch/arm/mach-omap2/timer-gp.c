/*
 * linux/arch/arm/mach-omap2/timer-gp.c
 *
 * OMAP2 GP timer support.
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *         Juha Yrjölä <juha.yrjola@nokia.com>
 * OMAP Dual-mode timer framework support by Timo Teras
 *
 * Some parts based off of TI's 24xx code:
 *
 *   Copyright (C) 2004 Texas Instruments, Inc.
 *
 * Roughly modelled after the OMAP1 MPU timer code.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irq.h>

#include <asm/mach/time.h>
#include <asm/arch/dmtimer.h>

static struct omap_dm_timer *gptimer;

static inline void omap2_gp_timer_start(unsigned long load_val)
{
	omap_dm_timer_set_load(gptimer, 1, 0xffffffff - load_val);
	omap_dm_timer_set_int_enable(gptimer, OMAP_TIMER_INT_OVERFLOW);
	omap_dm_timer_start(gptimer);
}

static irqreturn_t omap2_gp_timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);

	omap_dm_timer_write_status(gptimer, OMAP_TIMER_INT_OVERFLOW);
	timer_tick();

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction omap2_gp_timer_irq = {
	.name		= "gp timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= omap2_gp_timer_interrupt,
};

static void __init omap2_gp_timer_init(void)
{
	u32 tick_period;

	omap_dm_timer_init();
	gptimer = omap_dm_timer_request_specific(1);
	BUG_ON(gptimer == NULL);

	omap_dm_timer_set_source(gptimer, OMAP_TIMER_SRC_SYS_CLK);
	tick_period = clk_get_rate(omap_dm_timer_get_fclk(gptimer)) / HZ;
	tick_period -= 1;

	setup_irq(omap_dm_timer_get_irq(gptimer), &omap2_gp_timer_irq);
	omap2_gp_timer_start(tick_period);
}

struct sys_timer omap_timer = {
	.init	= omap2_gp_timer_init,
};
