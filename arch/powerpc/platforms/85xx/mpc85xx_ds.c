// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPC85xx DS Board Setup
 *
 * Author Xianghua Xiao (x.xiao@freescale.com)
 * Roy Zang <tie-fei.zang@freescale.com>
 * 	- Add PCI/PCI Exprees support
 * Copyright 2007 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/i8259.h>
#include <asm/swiotlb.h>
#include <asm/ppc-pci.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "smp.h"

#include "mpc85xx.h"

static void __init mpc85xx_ds_pic_init(void)
{
	struct mpic *mpic;
	int flags = MPIC_BIG_ENDIAN | MPIC_SINGLE_DEST_CPU;

	if (of_machine_is_compatible("fsl,MPC8572DS-CAMP"))
		flags |= MPIC_NO_RESET;

	mpic = mpic_alloc(NULL, 0, flags, 0, 256, " OpenPIC  ");

	if (WARN_ON(!mpic))
		return;

	mpic_init(mpic);

	mpc85xx_8259_init();
}

/*
 * Setup the architecture
 */
static void __init mpc85xx_ds_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_ds_setup_arch()", 0);

	swiotlb_detect_4g();
	fsl_pci_assign_primary();
	uli_init();
	mpc85xx_smp_init();

	pr_info("MPC85xx DS board from Freescale Semiconductor\n");
}

machine_arch_initcall(mpc8544_ds, mpc85xx_common_publish_devices);
machine_arch_initcall(mpc8572_ds, mpc85xx_common_publish_devices);

define_machine(mpc8544_ds) {
	.name			= "MPC8544 DS",
	.compatible		= "MPC8544DS",
	.setup_arch		= mpc85xx_ds_setup_arch,
	.init_IRQ		= mpc85xx_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(mpc8572_ds) {
	.name			= "MPC8572 DS",
	.compatible		= "fsl,MPC8572DS",
	.setup_arch		= mpc85xx_ds_setup_arch,
	.init_IRQ		= mpc85xx_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
