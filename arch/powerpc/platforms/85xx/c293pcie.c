/*
 * C293PCIE Board Setup
 *
 * Copyright 2013 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc85xx.h"

void __init c293_pcie_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
	  MPIC_SINGLE_DEST_CPU, 0, 256, " OpenPIC  ");

	BUG_ON(mpic == NULL);

	mpic_init(mpic);
}


/*
 * Setup the architecture
 */
static void __init c293_pcie_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("c293_pcie_setup_arch()", 0);

	fsl_pci_assign_primary();

	printk(KERN_INFO "C293 PCIE board from Freescale Semiconductor\n");
}

machine_arch_initcall(c293_pcie, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init c293_pcie_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "fsl,C293PCIE"))
		return 1;
	return 0;
}

define_machine(c293_pcie) {
	.name			= "C293 PCIE",
	.probe			= c293_pcie_probe,
	.setup_arch		= c293_pcie_setup_arch,
	.init_IRQ		= c293_pcie_pic_init,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
