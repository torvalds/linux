// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007, 2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby, <jrigby@freescale.com>, Thur Mar 29 2007
 *
 * Description:
 * MPC5121 ADS board setup
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/time.h>

#include <sysdev/fsl_pci.h>

#include "mpc512x.h"
#include "mpc5121_ads.h"

static void __init mpc5121_ads_setup_arch(void)
{
	printk(KERN_INFO "MPC5121 ADS board from Freescale Semiconductor\n");
	/*
	 * cpld regs are needed early
	 */
	mpc5121_ads_cpld_map();

	mpc512x_setup_arch();
}

static void __init mpc5121_ads_setup_pci(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;

	for_each_compatible_node(np, "pci", "fsl,mpc5121-pci")
		mpc83xx_add_bridge(np);
#endif
}

static void __init mpc5121_ads_init_IRQ(void)
{
	mpc512x_init_IRQ();
	mpc5121_ads_cpld_pic_init();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc5121_ads_probe(void)
{
	mpc512x_init_early();

	return 1;
}

define_machine(mpc5121_ads) {
	.name			= "MPC5121 ADS",
	.compatible		= "fsl,mpc5121ads",
	.probe			= mpc5121_ads_probe,
	.setup_arch		= mpc5121_ads_setup_arch,
	.discover_phbs		= mpc5121_ads_setup_pci,
	.init			= mpc512x_init,
	.init_IRQ		= mpc5121_ads_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc512x_restart,
};
