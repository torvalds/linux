/*
 * Taishan board specific routines based off ebony.c code
 * original copyrights below
 *
 * Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003-2005 Zultys Technologies
 *
 * Rewritten and ported to the merged powerpc tree:
 * Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * Modified from ebony.c for taishan:
 * Copyright 2007 Hugh Blemings <hugh@au.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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

static __initdata struct of_device_id taishan_of_bus[] = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init taishan_device_probe(void)
{
	of_platform_bus_probe(NULL, taishan_of_bus, NULL);

	return 0;
}
machine_device_initcall(taishan, taishan_device_probe);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init taishan_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "amcc,taishan"))
		return 0;

	ppc_pci_flags = PPC_PCI_REASSIGN_ALL_RSRC;

	return 1;
}

define_machine(taishan) {
	.name			= "Taishan",
	.probe			= taishan_probe,
	.progress		= udbg_progress,
	.init_IRQ		= uic_init_tree,
	.get_irq		= uic_get_irq,
	.restart		= ppc4xx_reset_system,
	.calibrate_decr		= generic_calibrate_decr,
};
