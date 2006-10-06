/*
 * linux/arch/sh/boards/snapgear/setup.c
 *
 * Copyright (C) 2002  David McCullough <davidm@snapgear.com>
 * Copyright (C) 2003  Paul Mundt <lethal@linux-sh.org>
 *
 * Based on files with the following comments:
 *
 *           Copyright (C) 2000  Kazumoto Kojima
 *
 *           Modified for 7751 Solution Engine by
 *           Ian da Silva and Jeremy Siegel, 2001.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/machvec.h>
#include <asm/snapgear.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/rtc.h>
#include <asm/cpu/timer.h>

extern void secureedge5410_rtc_init(void);
extern void pcibios_init(void);

/****************************************************************************/
/*
 * EraseConfig handling functions
 */

static irqreturn_t eraseconfig_interrupt(int irq, void *dev_id)
{
	volatile char dummy __attribute__((unused)) = * (volatile char *) 0xb8000000;

	printk("SnapGear: erase switch interrupt!\n");

	return IRQ_HANDLED;
}

static int __init eraseconfig_init(void)
{
	printk("SnapGear: EraseConfig init\n");
	/* Setup "EraseConfig" switch on external IRQ 0 */
	if (request_irq(IRL0_IRQ, eraseconfig_interrupt, IRQF_DISABLED,
				"Erase Config", NULL))
		printk("SnapGear: failed to register IRQ%d for Reset witch\n",
				IRL0_IRQ);
	else
		printk("SnapGear: registered EraseConfig switch on IRQ%d\n",
				IRL0_IRQ);
	return(0);
}

module_init(eraseconfig_init);

/****************************************************************************/
/*
 * Initialize IRQ setting
 *
 * IRL0 = erase switch
 * IRL1 = eth0
 * IRL2 = eth1
 * IRL3 = crypto
 */

static void __init init_snapgear_IRQ(void)
{
	/* enable individual interrupt mode for externals */
	ctrl_outw(ctrl_inw(INTC_ICR) | INTC_ICR_IRLM, INTC_ICR);

	printk("Setup SnapGear IRQ/IPR ...\n");

	make_ipr_irq(IRL0_IRQ, IRL0_IPR_ADDR, IRL0_IPR_POS, IRL0_PRIORITY);
	make_ipr_irq(IRL1_IRQ, IRL1_IPR_ADDR, IRL1_IPR_POS, IRL1_PRIORITY);
	make_ipr_irq(IRL2_IRQ, IRL2_IPR_ADDR, IRL2_IPR_POS, IRL2_PRIORITY);
	make_ipr_irq(IRL3_IRQ, IRL3_IPR_ADDR, IRL3_IPR_POS, IRL3_PRIORITY);
}

/*
 * Initialize the board
 */
static void __init snapgear_setup(char **cmdline_p)
{
	board_time_init = secureedge5410_rtc_init;
}

/*
 * The Machine Vector
 */
struct sh_machine_vector mv_snapgear __initmv = {
	.mv_name		= "SnapGear SecureEdge5410",
	.mv_setup		= snapgear_setup,
	.mv_nr_irqs		= 72,

	.mv_inb			= snapgear_inb,
	.mv_inw			= snapgear_inw,
	.mv_inl			= snapgear_inl,
	.mv_outb		= snapgear_outb,
	.mv_outw		= snapgear_outw,
	.mv_outl		= snapgear_outl,

	.mv_inb_p		= snapgear_inb_p,
	.mv_inw_p		= snapgear_inw,
	.mv_inl_p		= snapgear_inl,
	.mv_outb_p		= snapgear_outb_p,
	.mv_outw_p		= snapgear_outw,
	.mv_outl_p		= snapgear_outl,

	.mv_init_irq		= init_snapgear_IRQ,
};
ALIAS_MV(snapgear)
