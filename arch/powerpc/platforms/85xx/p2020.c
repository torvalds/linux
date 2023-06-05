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

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init p2020_probe(void)
{
	struct device_node *p2020_cpu;

	/*
	 * There is no common compatible string for all P2020 boards.
	 * The only common thing is "PowerPC,P2020@0" cpu node.
	 * So check for P2020 board via this cpu node.
	 */
	p2020_cpu = of_find_node_by_path("/cpus/PowerPC,P2020@0");
	of_node_put(p2020_cpu);

	return !!p2020_cpu;
}

machine_arch_initcall(p2020, mpc85xx_common_publish_devices);

define_machine(p2020) {
	.name			= "Freescale P2020",
	.probe			= p2020_probe,
	.setup_arch		= p2020_setup_arch,
	.init_IRQ		= p2020_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb	= fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
