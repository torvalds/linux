/*
 * MPC85xx DS Board Setup
 *
 * Author Xianghua Xiao (x.xiao@freescale.com)
 * Roy Zang <tie-fei.zang@freescale.com>
 * 	- Add PCI/PCI Exprees support
 * Copyright 2007 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/i8259.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define DBG(fmt, args...)
#endif

#ifdef CONFIG_PPC_I8259
static void mpc85xx_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq != NO_IRQ) {
		generic_handle_irq(cascade_irq);
	}
	desc->chip->eoi(irq);
}
#endif	/* CONFIG_PPC_I8259 */

void __init mpc85xx_ds_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np = NULL;
#ifdef CONFIG_PPC_I8259
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif

	np = of_find_node_by_type(np, "open-pic");

	if (np == NULL) {
		printk(KERN_ERR "Could not find open-pic node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Failed to map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			  MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
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
	if (cascade_irq == NO_IRQ) {
		printk(KERN_ERR "Failed to map cascade interrupt\n");
		return;
	}

	DBG("mpc85xxds: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc85xx_8259_cascade);
#endif	/* CONFIG_PPC_I8259 */
}

#ifdef CONFIG_PCI
static int primary_phb_addr;
extern int uses_fsl_uli_m1575;
extern int uli_exclude_device(struct pci_controller *hose,
				u_char bus, u_char devfn);

static int mpc85xx_exclude_device(struct pci_controller *hose,
				   u_char bus, u_char devfn)
{
	struct device_node* node;
	struct resource rsrc;

	node = hose->dn;
	of_address_to_resource(node, 0, &rsrc);

	if ((rsrc.start & 0xfffff) == primary_phb_addr) {
		return uli_exclude_device(hose, bus, devfn);
	}

	return PCIBIOS_SUCCESSFUL;
}
#endif	/* CONFIG_PCI */

/*
 * Setup the architecture
 */
static void __init mpc85xx_ds_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_ds_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8540-pci") ||
		    of_device_is_compatible(np, "fsl,mpc8548-pcie")) {
			struct resource rsrc;
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == primary_phb_addr)
				fsl_add_bridge(np, 1);
			else
				fsl_add_bridge(np, 0);
		}
	}

	uses_fsl_uli_m1575 = 1;
	ppc_md.pci_exclude_device = mpc85xx_exclude_device;
#endif

	printk("MPC85xx DS board from Freescale Semiconductor\n");
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc8544_ds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "MPC8544DS")) {
#ifdef CONFIG_PCI
		primary_phb_addr = 0xb000;
#endif
		return 1;
	} else {
		return 0;
	}
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc8572_ds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "fsl,MPC8572DS")) {
#ifdef CONFIG_PCI
		primary_phb_addr = 0x8000;
#endif
		return 1;
	} else {
		return 0;
	}
}

define_machine(mpc8544_ds) {
	.name			= "MPC8544 DS",
	.probe			= mpc8544_ds_probe,
	.setup_arch		= mpc85xx_ds_setup_arch,
	.init_IRQ		= mpc85xx_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};

define_machine(mpc8572_ds) {
	.name			= "MPC8572 DS",
	.probe			= mpc8572_ds_probe,
	.setup_arch		= mpc85xx_ds_setup_arch,
	.init_IRQ		= mpc85xx_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
