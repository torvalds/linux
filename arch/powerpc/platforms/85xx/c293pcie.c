// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * C293PCIE Board Setup
 *
 * Copyright 2013 Freescale Semiconductor Inc.
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

static void __init c293_pcie_pic_init(void)
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

define_machine(c293_pcie) {
	.name			= "C293 PCIE",
	.compatible		= "fsl,C293PCIE",
	.setup_arch		= c293_pcie_setup_arch,
	.init_IRQ		= c293_pcie_pic_init,
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
