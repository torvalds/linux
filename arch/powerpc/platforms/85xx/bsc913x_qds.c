// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BSC913xQDS Board Setup
 *
 * Author:
 *   Harninder Rai <harninder.rai@freescale.com>
 *   Priyanka Jain <Priyanka.Jain@freescale.com>
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 */

#include <linux/of.h>
#include <linux/pci.h>
#include <asm/mpic.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include <asm/udbg.h>

#include "mpc85xx.h"
#include "smp.h"

void __init bsc913x_qds_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
	  MPIC_SINGLE_DEST_CPU,
	  0, 256, " OpenPIC  ");

	if (!mpic)
		pr_err("bsc913x: Failed to allocate MPIC structure\n");
	else
		mpic_init(mpic);
}

/*
 * Setup the architecture
 */
static void __init bsc913x_qds_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("bsc913x_qds_setup_arch()", 0);

#if defined(CONFIG_SMP)
	mpc85xx_smp_init();
#endif

	fsl_pci_assign_primary();

	pr_info("bsc913x board from Freescale Semiconductor\n");
}

machine_arch_initcall(bsc9132_qds, mpc85xx_common_publish_devices);

define_machine(bsc9132_qds) {
	.name			= "BSC9132 QDS",
	.compatible		= "fsl,bsc9132qds",
	.setup_arch		= bsc913x_qds_setup_arch,
	.init_IRQ		= bsc913x_qds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
