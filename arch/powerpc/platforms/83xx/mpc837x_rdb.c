/*
 * arch/powerpc/platforms/83xx/mpc837x_rdb.c
 *
 * Copyright (C) 2007 Freescale Semicondutor, Inc. All rights reserved.
 *
 * MPC837x RDB board specific routines
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
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

static void mpc837x_rdb_sd_cfg(void)
{
	void __iomem *im;

	im = ioremap(get_immrbase(), 0x1000);
	if (!im) {
		WARN_ON(1);
		return;
	}

	/*
	 * On RDB boards (in contrast to MDS) USBB pins are used for SD only,
	 * so we can safely mux them away from the USB block.
	 */
	clrsetbits_be32(im + MPC83XX_SICRL_OFFS, MPC837X_SICRL_USBB_MASK,
						 MPC837X_SICRL_SD);
	clrsetbits_be32(im + MPC83XX_SICRH_OFFS, MPC837X_SICRH_SPI_MASK,
						 MPC837X_SICRH_SD);
	iounmap(im);
}

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc837x_rdb_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc837x_rdb_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8349-pci")
		mpc83xx_add_bridge(np);
	for_each_compatible_node(np, "pci", "fsl,mpc8314-pcie")
		mpc83xx_add_bridge(np);
#endif
	mpc837x_usb_cfg();
	mpc837x_rdb_sd_cfg();
}

static struct of_device_id mpc837x_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{ .compatible = "gianfar", },
	{},
};

static int __init mpc837x_declare_of_platform_devices(void)
{
	/* Publish of_device */
	of_platform_bus_probe(NULL, mpc837x_ids, NULL);

	return 0;
}
machine_device_initcall(mpc837x_rdb, mpc837x_declare_of_platform_devices);

static void __init mpc837x_rdb_init_IRQ(void)
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
static int __init mpc837x_rdb_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,mpc8377rdb") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8378rdb") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8379rdb") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8377wlan");
}

define_machine(mpc837x_rdb) {
	.name			= "MPC837x RDB/WLAN",
	.probe			= mpc837x_rdb_probe,
	.setup_arch		= mpc837x_rdb_setup_arch,
	.init_IRQ		= mpc837x_rdb_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
