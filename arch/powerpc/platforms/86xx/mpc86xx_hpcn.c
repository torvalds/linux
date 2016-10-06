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

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>
#include <asm/swiotlb.h>

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
extern int uli_exclude_device(struct pci_controller *hose,
				u_char bus, u_char devfn);

static int mpc86xx_exclude_device(struct pci_controller *hose,
				   u_char bus, u_char devfn)
{
	if (hose->dn == fsl_pci_primary)
		return uli_exclude_device(hose, bus, devfn);

	return PCIBIOS_SUCCESSFUL;
}
#endif /* CONFIG_PCI */


static void __init
mpc86xx_hpcn_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc86xx_hpcn_setup_arch()", 0);

#ifdef CONFIG_PCI
	ppc_md.pci_exclude_device = mpc86xx_exclude_device;
#endif

	printk("MPC86xx HPCN board from Freescale Semiconductor\n");

#ifdef CONFIG_SMP
	mpc86xx_smp_init();
#endif

	fsl_pci_assign_primary();

	swiotlb_detect_4g();
}


static void
mpc86xx_hpcn_show_cpuinfo(struct seq_file *m)
{
	uint svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");

	seq_printf(m, "SVR\t\t: 0x%x\n", svid);
}


/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc86xx_hpcn_probe(void)
{
	if (of_machine_is_compatible("fsl,mpc8641hpcn"))
		return 1;	/* Looks good */

	/* Be nice and don't give silent boot death.  Delete this in 2.6.27 */
	if (of_machine_is_compatible("mpc86xx")) {
		pr_warning("WARNING: your dts/dtb is old. You must update before the next kernel release\n");
		return 1;
	}

	return 0;
}

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .compatible = "fsl,srio", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	mpc86xx_common_publish_devices();
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_arch_initcall(mpc86xx_hpcn, declare_of_platform_devices);
machine_arch_initcall(mpc86xx_hpcn, swiotlb_setup_bus_notifier);

define_machine(mpc86xx_hpcn) {
	.name			= "MPC86xx HPCN",
	.probe			= mpc86xx_hpcn_probe,
	.setup_arch		= mpc86xx_hpcn_setup_arch,
	.init_IRQ		= mpc86xx_init_irq,
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
