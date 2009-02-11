/*
 * arch/powerpc/platforms/83xx/mpc831x_rdb.c
 *
 * Description: MPC831x RDB board specific routines.
 * This file is based on mpc834x_sys.c
 * Author: Lo Wlison <r43300@freescale.com>
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2006. All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

/*
 * Setup the architecture
 */
static void __init mpc831x_rdb_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc831x_rdb_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8349-pci")
		mpc83xx_add_bridge(np);
	for_each_compatible_node(np, "pci", "fsl,mpc8314-pcie")
		mpc83xx_add_bridge(np);
#endif
	mpc831x_usb_cfg();
}

static void __init mpc831x_rdb_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_node_by_type(NULL, "ipic");
	if (!np)
		return;

	ipic_init(np, 0);

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc831x_rdb_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "MPC8313ERDB") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8315erdb");
}

static struct of_device_id __initdata of_bus_ids[] = {
	{ .compatible = "simple-bus" },
	{ .compatible = "gianfar" },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);
	return 0;
}
machine_device_initcall(mpc831x_rdb, declare_of_platform_devices);

define_machine(mpc831x_rdb) {
	.name			= "MPC831x RDB",
	.probe			= mpc831x_rdb_probe,
	.setup_arch		= mpc831x_rdb_setup_arch,
	.init_IRQ		= mpc831x_rdb_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
