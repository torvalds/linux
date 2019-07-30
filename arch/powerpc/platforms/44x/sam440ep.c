// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sam440ep board specific routines based off bamboo.c code
 * original copyrights below
 *
 * Wade Farnsworth <wfarnsworth@mvista.com>
 * Copyright 2004 MontaVista Software Inc.
 *
 * Rewritten and ported to the merged powerpc tree:
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 * Copyright 2007 IBM Corporation
 *
 * Modified from bamboo.c for sam440ep:
 * Copyright 2008 Giuseppe Coviello <gicoviello@gmail.com>
 */
#include <linux/init.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/pci-bridge.h>
#include <asm/ppc4xx.h>
#include <linux/i2c.h>

static const struct of_device_id sam440ep_of_bus[] __initconst = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init sam440ep_device_probe(void)
{
	of_platform_bus_probe(NULL, sam440ep_of_bus, NULL);

	return 0;
}
machine_device_initcall(sam440ep, sam440ep_device_probe);

static int __init sam440ep_probe(void)
{
	if (!of_machine_is_compatible("acube,sam440ep"))
		return 0;

	pci_set_flags(PCI_REASSIGN_ALL_RSRC);

	return 1;
}

define_machine(sam440ep) {
	.name 			= "Sam440ep",
	.probe 			= sam440ep_probe,
	.progress 		= udbg_progress,
	.init_IRQ 		= uic_init_tree,
	.get_irq 		= uic_get_irq,
	.restart		= ppc4xx_reset_system,
	.calibrate_decr 	= generic_calibrate_decr,
};

static struct i2c_board_info sam440ep_rtc_info = {
	.type = "m41st85",
	.addr = 0x68,
	.irq = -1,
};

static int __init sam440ep_setup_rtc(void)
{
	return i2c_register_board_info(0, &sam440ep_rtc_info, 1);
}
machine_device_initcall(sam440ep, sam440ep_setup_rtc);
