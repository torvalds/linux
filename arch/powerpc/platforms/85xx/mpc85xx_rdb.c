// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MPC85xx RDB Board Setup
 *
 * Copyright 2009,2012-2013 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/fsl/guts.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <soc/fsl/qe/qe.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "smp.h"

#include "mpc85xx.h"

static void __init mpc85xx_rdb_pic_init(void)
{
	struct mpic *mpic;
	int flags = MPIC_BIG_ENDIAN | MPIC_SINGLE_DEST_CPU;

	if (of_machine_is_compatible("fsl,MPC85XXRDB-CAMP"))
		flags |= MPIC_NO_RESET;

	mpic = mpic_alloc(NULL, 0, flags, 0, 256, " OpenPIC  ");

	if (WARN_ON(!mpic))
		return;

	mpic_init(mpic);
}

/*
 * Setup the architecture
 */
static void __init mpc85xx_rdb_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_rdb_setup_arch()", 0);

	mpc85xx_smp_init();

	fsl_pci_assign_primary();

	mpc85xx_qe_par_io_init();
#if defined(CONFIG_UCC_GETH) || defined(CONFIG_SERIAL_QE)
	if (machine_is(p1025_rdb)) {
		struct device_node *np;

		struct ccsr_guts __iomem *guts;

		np = of_find_node_by_name(NULL, "global-utilities");
		if (np) {
			guts = of_iomap(np, 0);
			if (!guts) {

				pr_err("mpc85xx-rdb: could not map global utilities register\n");

			} else {
			/* P1025 has pins muxed for QE and other functions. To
			* enable QE UEC mode, we need to set bit QE0 for UCC1
			* in Eth mode, QE0 and QE3 for UCC5 in Eth mode, QE9
			* and QE12 for QE MII management singals in PMUXCR
			* register.
			*/
				setbits32(&guts->pmuxcr, MPC85xx_PMUXCR_QE(0) |
						MPC85xx_PMUXCR_QE(3) |
						MPC85xx_PMUXCR_QE(9) |
						MPC85xx_PMUXCR_QE(12));
				iounmap(guts);
			}
			of_node_put(np);
		}

	}
#endif

	pr_info("MPC85xx RDB board from Freescale Semiconductor\n");
}

machine_arch_initcall(p1020_mbg_pc, mpc85xx_common_publish_devices);
machine_arch_initcall(p1020_rdb, mpc85xx_common_publish_devices);
machine_arch_initcall(p1020_rdb_pc, mpc85xx_common_publish_devices);
machine_arch_initcall(p1020_rdb_pd, mpc85xx_common_publish_devices);
machine_arch_initcall(p1020_utm_pc, mpc85xx_common_publish_devices);
machine_arch_initcall(p1021_rdb_pc, mpc85xx_common_publish_devices);
machine_arch_initcall(p1025_rdb, mpc85xx_common_publish_devices);
machine_arch_initcall(p1024_rdb, mpc85xx_common_publish_devices);

define_machine(p1020_rdb) {
	.name			= "P1020 RDB",
	.compatible		= "fsl,P1020RDB",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1021_rdb_pc) {
	.name			= "P1021 RDB-PC",
	.compatible		= "fsl,P1021RDB-PC",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1025_rdb) {
	.name			= "P1025 RDB",
	.compatible		= "fsl,P1025RDB",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1020_mbg_pc) {
	.name			= "P1020 MBG-PC",
	.compatible		= "fsl,P1020MBG-PC",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1020_utm_pc) {
	.name			= "P1020 UTM-PC",
	.compatible		= "fsl,P1020UTM-PC",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1020_rdb_pc) {
	.name			= "P1020RDB-PC",
	.compatible		= "fsl,P1020RDB-PC",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1020_rdb_pd) {
	.name			= "P1020RDB-PD",
	.compatible		= "fsl,P1020RDB-PD",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(p1024_rdb) {
	.name			= "P1024 RDB",
	.compatible		= "fsl,P1024RDB",
	.setup_arch		= mpc85xx_rdb_setup_arch,
	.init_IRQ		= mpc85xx_rdb_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
