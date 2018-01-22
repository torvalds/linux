// SPDX-License-Identifier: GPL-2.0
/*
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
#include <linux/sched.h>
#include <asm/machvec.h>
#include <mach/secureedge5410.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <cpu/timer.h>

unsigned short secureedge5410_ioport;

/*
 * EraseConfig handling functions
 */
static irqreturn_t eraseconfig_interrupt(int irq, void *dev_id)
{
	printk("SnapGear: erase switch interrupt!\n");

	return IRQ_HANDLED;
}

static int __init eraseconfig_init(void)
{
	unsigned int irq = evt2irq(0x240);

	printk("SnapGear: EraseConfig init\n");

	/* Setup "EraseConfig" switch on external IRQ 0 */
	if (request_irq(irq, eraseconfig_interrupt, 0, "Erase Config", NULL))
		printk("SnapGear: failed to register IRQ%d for Reset witch\n",
				irq);
	else
		printk("SnapGear: registered EraseConfig switch on IRQ%d\n",
				irq);
	return 0;
}
device_initcall(eraseconfig_init);

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
	printk("Setup SnapGear IRQ/IPR ...\n");
	/* enable individual interrupt mode for externals */
	plat_irq_setup_pins(IRQ_MODE_IRQ);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_snapgear __initmv = {
	.mv_name		= "SnapGear SecureEdge5410",
	.mv_init_irq		= init_snapgear_IRQ,
};
