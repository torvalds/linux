/*
 * arch/powerpc/platforms/83xx/mpc837x_mds.c
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * MPC837x MDS board specific routines
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <asm/prom.h>

#include "mpc83xx.h"

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc837x_mds_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc837x_mds_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8349-pci")
		mpc83xx_add_bridge(np);
#endif
}

static struct of_device_id mpc837x_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{},
};

static int __init mpc837x_declare_of_platform_devices(void)
{
	/* Publish of_device */
	of_platform_bus_probe(NULL, mpc837x_ids, NULL);

	return 0;
}
machine_device_initcall(mpc837x_mds, mpc837x_declare_of_platform_devices);

static void __init mpc837x_mds_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,ipic");
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
static int __init mpc837x_mds_probe(void)
{
        unsigned long root = of_get_flat_dt_root();

        return of_flat_dt_is_compatible(root, "fsl,mpc837xmds");
}

define_machine(mpc837x_mds) {
	.name			= "MPC837x MDS",
	.probe			= mpc837x_mds_probe,
	.setup_arch		= mpc837x_mds_setup_arch,
	.init_IRQ		= mpc837x_mds_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
