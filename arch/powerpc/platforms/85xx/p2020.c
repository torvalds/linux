// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale P2020 board Setup
 *
 * Copyright 2007,2009,2012-2013 Freescale Semiconductor Inc.
 * Copyright 2022-2023 Pali Rohár <pali@kernel.org>
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/swiotlb.h>
#include <asm/ppc-pci.h>

#include <sysdev/fsl_pci.h>

#include "smp.h"
#include "mpc85xx.h"

static void __init p2020_pic_init(void)
{
	struct mpic *mpic;
	int flags = MPIC_BIG_ENDIAN | MPIC_SINGLE_DEST_CPU;

	mpic = mpic_alloc(NULL, 0, flags, 0, 256, " OpenPIC  ");

	if (WARN_ON(!mpic))
		return;

	mpic_init(mpic);
	mpc85xx_8259_init();
}

/*
 * Setup the architecture
 */
static void __init p2020_setup_arch(void)
{
	swiotlb_detect_4g();
	fsl_pci_assign_primary();
	uli_init();
	mpc85xx_smp_init();
	mpc85xx_qe_par_io_init();
}

#ifdef CONFIG_MPC85xx_DS
machine_arch_initcall(p2020_ds, mpc85xx_common_publish_devices);
#endif /* CONFIG_MPC85xx_DS */

#ifdef CONFIG_MPC85xx_RDB
machine_arch_initcall(p2020_rdb, mpc85xx_common_publish_devices);
machine_arch_initcall(p2020_rdb_pc, mpc85xx_common_publish_devices);
#endif /* CONFIG_MPC85xx_RDB */

#ifdef CONFIG_MPC85xx_DS
define_machine(p2020_ds) {
	.name			= "P2020 DS",
	.compatible		= "fsl,P2020DS",
	.setup_arch		= p2020_setup_arch,
	.init_IRQ		= p2020_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb	= fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
#endif /* CONFIG_MPC85xx_DS */

#ifdef CONFIG_MPC85xx_RDB
define_machine(p2020_rdb) {
	.name			= "P2020 RDB",
	.compatible		= "fsl,P2020RDB",
	.setup_arch		= p2020_setup_arch,
	.init_IRQ		= p2020_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb	= fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p2020_rdb_pc) {
	.name			= "P2020RDB-PC",
	.compatible		= "fsl,P2020RDB-PC",
	.setup_arch		= p2020_setup_arch,
	.init_IRQ		= p2020_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb	= fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
#endif /* CONFIG_MPC85xx_RDB */
