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

#ifdef CONFIG_PPC_I8259
static void mpc85xx_8259_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq) {
		generic_handle_irq(cascade_irq);
	}
	chip->irq_eoi(&desc->irq_data);
}
#endif	/* CONFIG_PPC_I8259 */

void __init mpc85xx_ds_pic_init(void)
{
	struct mpic *mpic;
#ifdef CONFIG_PPC_I8259
	struct device_node *np;
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif
	if (of_machine_is_compatible("fsl,MPC8572DS-CAMP")) {
		mpic = mpic_alloc(NULL, 0,
			MPIC_NO_RESET |
			MPIC_BIG_ENDIAN |
			MPIC_SINGLE_DEST_CPU,
			0, 256, " OpenPIC  ");
	} else {
		mpic = mpic_alloc(NULL, 0,
			  MPIC_BIG_ENDIAN |
			  MPIC_SINGLE_DEST_CPU,
			0, 256, " OpenPIC  ");
	}

	BUG_ON(mpic == NULL);
	mpic_init(mpic);

#ifdef CONFIG_PPC_I8259
	/* Initialize the i8259 controller */
	for_each_node_by_type(np, "interrupt-controller")
	    if (of_device_is_compatible(np, "chrp,iic")) {
		cascade_node = np;
		break;
	}

	if (cascade_node == NULL) {
		printk(KERN_DEBUG "Could not find i8259 PIC\n");
		return;
	}

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (!cascade_irq) {
		printk(KERN_ERR "Failed to map cascade interrupt\n");
		return;
	}

	pr_debug("mpc85xxds: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	irq_set_chained_handler(cascade_irq, mpc85xx_8259_cascade);
#endif	/* CONFIG_PPC_I8259 */
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

	printk("MPC85xx DS board from Freescale Semiconductor\n");
}

machine_arch_initcall(mpc8544_ds, mpc85xx_common_publish_devices);
machine_arch_initcall(mpc8572_ds, mpc85xx_common_publish_devices);
machine_arch_initcall(p2020_ds, mpc85xx_common_publish_devices);

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

define_machine(p2020_ds) {
	.name			= "P2020 DS",
	.compatible		= "fsl,P2020DS",
	.setup_arch		= mpc85xx_ds_setup_arch,
	.init_IRQ		= mpc85xx_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
