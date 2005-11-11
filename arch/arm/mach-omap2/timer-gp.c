/*
 * linux/arch/arm/mach-omap2/timer-gp.c
 *
 * OMAP2 GP timer support.
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *         Juha Yrjölä <juha.yrjola@nokia.com>
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
#include <asm/mach/time.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/hardware/clock.h>

#define OMAP2_GP_TIMER1_BASE	0x48028000
#define OMAP2_GP_TIMER2_BASE	0x4802a000
#define OMAP2_GP_TIMER3_BASE	0x48078000
#define OMAP2_GP_TIMER4_BASE	0x4807a000

#define GP_TIMER_TIDR		0x00
#define GP_TIMER_TISR		0x18
#define GP_TIMER_TIER		0x1c
#define GP_TIMER_TCLR		0x24
#define GP_TIMER_TCRR		0x28
#define GP_TIMER_TLDR		0x2c
#define GP_TIMER_TSICR		0x40

#define OS_TIMER_NR		1  /* GP timer 2 */

static unsigned long timer_base[] = {
	IO_ADDRESS(OMAP2_GP_TIMER1_BASE),
	IO_ADDRESS(OMAP2_GP_TIMER2_BASE),
	IO_ADDRESS(OMAP2_GP_TIMER3_BASE),
	IO_ADDRESS(OMAP2_GP_TIMER4_BASE),
};

static inline unsigned int timer_read_reg(int nr, unsigned int reg)
{
	return __raw_readl(timer_base[nr] + reg);
}

static inline void timer_write_reg(int nr, unsigned int reg, unsigned int val)
{
	__raw_writel(val, timer_base[nr] + reg);
}

/* Note that we always enable the clock prescale divider bit */
static inline void omap2_gp_timer_start(int nr, unsigned long load_val)
{
	unsigned int tmp;

	tmp = 0xffffffff - load_val;

	timer_write_reg(nr, GP_TIMER_TLDR, tmp);
	timer_write_reg(nr, GP_TIMER_TCRR, tmp);
	timer_write_reg(nr, GP_TIMER_TIER, 1 << 1);
	timer_write_reg(nr, GP_TIMER_TCLR, (1 << 5) | (1 << 1) | 1);
}

static irqreturn_t omap2_gp_timer_interrupt(int irq, void *dev_id,
					    struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	timer_write_reg(OS_TIMER_NR, GP_TIMER_TISR, 1 << 1);
	timer_tick(regs);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction omap2_gp_timer_irq = {
	.name		= "gp timer",
	.flags		= SA_INTERRUPT,
	.handler	= omap2_gp_timer_interrupt,
};

static void __init omap2_gp_timer_init(void)
{
	struct clk * sys_ck;
	u32 tick_period = 120000;
	u32 l;

	/* Reset clock and prescale value */
	timer_write_reg(OS_TIMER_NR, GP_TIMER_TCLR, 0);

	sys_ck = clk_get(NULL, "sys_ck");
	if (IS_ERR(sys_ck))
		printk(KERN_ERR "Could not get sys_ck\n");
	else {
		clk_use(sys_ck);
		tick_period = clk_get_rate(sys_ck) / 100;
		clk_put(sys_ck);
	}

	tick_period /= 2;	/* Minimum prescale divider is 2 */
	tick_period -= 1;

	l = timer_read_reg(OS_TIMER_NR, GP_TIMER_TIDR);
	printk(KERN_INFO "OMAP2 GP timer (HW version %d.%d)\n",
	       (l >> 4) & 0x0f, l & 0x0f);

	setup_irq(38, &omap2_gp_timer_irq);

	omap2_gp_timer_start(OS_TIMER_NR, tick_period);
}

struct sys_timer omap_timer = {
	.init	= omap2_gp_timer_init,
};

