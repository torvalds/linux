/*
 * MPC86xx HPCN board specific routines
 *
 * Recode: ZHANG WEI <wei.zhang@freescale.com>
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 *
 * Copyright 2006 Freescale Semiconductor Inc.
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
#include <linux/of_platform.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc86xx.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>
#include <asm/i8259.h>

#include <asm/mpic.h>

#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) do { printk(KERN_ERR fmt); } while(0)
#else
#define DBG(fmt...) do { } while(0)
#endif

#ifdef CONFIG_PCI
static void mpc86xx_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();
	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
	desc->chip->eoi(irq);
}
#endif	/* CONFIG_PCI */

static void __init
mpc86xx_hpcn_init_irq(void)
{
	struct mpic *mpic1;
	struct device_node *np;
	struct resource res;
#ifdef CONFIG_PCI
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif

	/* Determine PIC address. */
	np = of_find_node_by_type(NULL, "open-pic");
	if (np == NULL)
		return;
	of_address_to_resource(np, 0, &res);

	/* Alloc mpic structure and per isu has 16 INT entries. */
	mpic1 = mpic_alloc(np, res.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			0, 256, " MPIC     ");
	BUG_ON(mpic1 == NULL);

	mpic_init(mpic1);

#ifdef CONFIG_PCI
	/* Initialize i8259 controller */
	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "chrp,iic")) {
			cascade_node = np;
			break;
		}
	if (cascade_node == NULL) {
		printk(KERN_DEBUG "mpc86xxhpcn: no ISA interrupt controller\n");
		return;
	}

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (cascade_irq == NO_IRQ) {
		printk(KERN_ERR "mpc86xxhpcn: failed to map cascade interrupt");
		return;
	}
	DBG("mpc86xxhpcn: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc86xx_8259_cascade);
#endif
}

#ifdef CONFIG_PCI
extern int uses_fsl_uli_m1575;
extern int uli_exclude_device(struct pci_controller *hose,
				u_char bus, u_char devfn);

static int mpc86xx_exclude_device(struct pci_controller *hose,
				   u_char bus, u_char devfn)
{
	struct device_node* node;	
	struct resource rsrc;

	node = hose->dn;
	of_address_to_resource(node, 0, &rsrc);

	if ((rsrc.start & 0xfffff) == 0x8000) {
		return uli_exclude_device(hose, bus, devfn);
	}

	return PCIBIOS_SUCCESSFUL;
}
#endif /* CONFIG_PCI */


static void __init
mpc86xx_hpcn_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc86xx_hpcn_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8641-pcie") {
		struct resource rsrc;
		of_address_to_resource(np, 0, &rsrc);
		if ((rsrc.start & 0xfffff) == 0x8000)
			fsl_add_bridge(np, 1);
		else
			fsl_add_bridge(np, 0);
	}

	uses_fsl_uli_m1575 = 1;
	ppc_md.pci_exclude_device = mpc86xx_exclude_device;

#endif

	printk("MPC86xx HPCN board from Freescale Semiconductor\n");

#ifdef CONFIG_SMP
	mpc86xx_smp_init();
#endif
}


static void
mpc86xx_hpcn_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	uint memsize = total_memory;
	const char *model = "";
	uint svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "Machine\t\t: %s\n", model);
	of_node_put(root);

	seq_printf(m, "SVR\t\t: 0x%x\n", svid);
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}


/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc86xx_hpcn_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "fsl,mpc8641hpcn"))
		return 1;	/* Looks good */

	/* Be nice and don't give silent boot death.  Delete this in 2.6.27 */
	if (of_flat_dt_is_compatible(root, "mpc86xx")) {
		pr_warning("WARNING: your dts/dtb is old. You must update before the next kernel release\n");
		return 1;
	}

	return 0;
}

static long __init
mpc86xx_time_init(void)
{
	unsigned int temp;

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	temp = mfspr(SPRN_HID0);
	temp |= HID0_TBEN;
	mtspr(SPRN_HID0, temp);
	asm volatile("isync");

	return 0;
}

static __initdata struct of_device_id of_bus_ids[] = {
	{ .compatible = "simple-bus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(mpc86xx_hpcn, declare_of_platform_devices);

define_machine(mpc86xx_hpcn) {
	.name			= "MPC86xx HPCN",
	.probe			= mpc86xx_hpcn_probe,
	.setup_arch		= mpc86xx_hpcn_setup_arch,
	.init_IRQ		= mpc86xx_hpcn_init_irq,
	.show_cpuinfo		= mpc86xx_hpcn_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
};
