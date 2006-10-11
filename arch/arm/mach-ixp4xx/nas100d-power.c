/*
 * arch/arm/mach-ixp4xx/nas100d-power.c
 *
 * NAS 100d Power/Reset driver
 *
 * Copyright (C) 2005 Tower Technologies
 *
 * based on nas100d-io.c
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

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/reboot.h>

#include <asm/mach-types.h>

static irqreturn_t nas100d_reset_handler(int irq, void *dev_id)
{
	/* Signal init to do the ctrlaltdel action, this will bypass init if
	 * it hasn't started and do a kernel_restart.
	 */
	ctrl_alt_del();

	return IRQ_HANDLED;
}

static int __init nas100d_power_init(void)
{
	if (!(machine_is_nas100d()))
		return 0;

	set_irq_type(NAS100D_RB_IRQ, IRQT_LOW);

	if (request_irq(NAS100D_RB_IRQ, &nas100d_reset_handler,
		IRQF_DISABLED, "NAS100D reset button", NULL) < 0) {

		printk(KERN_DEBUG "Reset Button IRQ %d not available\n",
			NAS100D_RB_IRQ);

		return -EIO;
	}

	return 0;
}

static void __exit nas100d_power_exit(void)
{
	if (!(machine_is_nas100d()))
		return;

	free_irq(NAS100D_RB_IRQ, NULL);
}

module_init(nas100d_power_init);
module_exit(nas100d_power_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("NAS100D Power/Reset driver");
MODULE_LICENSE("GPL");
