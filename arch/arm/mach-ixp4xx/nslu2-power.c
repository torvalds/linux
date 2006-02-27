/*
 * arch/arm/mach-ixp4xx/nslu2-power.c
 *
 * NSLU2 Power/Reset driver
 *
 * Copyright (C) 2005 Tower Technologies
 *
 * based on nslu2-io.c
 *  Copyright (C) 2004 Karen Spearel
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 * Maintainers: http://www.nslu2-linux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>

#include <asm/mach-types.h>

extern void ctrl_alt_del(void);

static irqreturn_t nslu2_power_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	/* Signal init to do the ctrlaltdel action, this will bypass init if
	 * it hasn't started and do a kernel_restart.
	 */
	ctrl_alt_del();

	return IRQ_HANDLED;
}

static irqreturn_t nslu2_reset_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	/* This is the paper-clip reset, it shuts the machine down directly.
	 */
	machine_power_off();

	return IRQ_HANDLED;
}

static int __init nslu2_power_init(void)
{
	if (!(machine_is_nslu2()))
		return 0;

	*IXP4XX_GPIO_GPISR = 0x20400000;	/* read the 2 irqs to clr */

	set_irq_type(NSLU2_RB_IRQ, IRQT_LOW);
	set_irq_type(NSLU2_PB_IRQ, IRQT_HIGH);

	if (request_irq(NSLU2_RB_IRQ, &nslu2_reset_handler,
		SA_INTERRUPT, "NSLU2 reset button", NULL) < 0) {

		printk(KERN_DEBUG "Reset Button IRQ %d not available\n",
			NSLU2_RB_IRQ);

		return -EIO;
	}

	if (request_irq(NSLU2_PB_IRQ, &nslu2_power_handler,
		SA_INTERRUPT, "NSLU2 power button", NULL) < 0) {

		printk(KERN_DEBUG "Power Button IRQ %d not available\n",
			NSLU2_PB_IRQ);

		return -EIO;
	}

	return 0;
}

static void __exit nslu2_power_exit(void)
{
	if (!(machine_is_nslu2()))
		return;

	free_irq(NSLU2_RB_IRQ, NULL);
	free_irq(NSLU2_PB_IRQ, NULL);
}

module_init(nslu2_power_init);
module_exit(nslu2_power_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("NSLU2 Power/Reset driver");
MODULE_LICENSE("GPL");
