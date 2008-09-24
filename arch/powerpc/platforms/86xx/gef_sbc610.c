/*
 * GE Fanuc SBC610 board support
 *
 * Author: Martyn Welch <martyn.welch@gefanuc.com>
 *
 * Copyright 2008 GE Fanuc Intelligent Platforms Embedded Systems, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Based on: mpc86xx_hpcn.c (MPC86xx HPCN board specific routines)
 * Copyright 2006 Freescale Semiconductor Inc.
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

#include <asm/mpic.h>

#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"

#undef DEBUG

#ifdef DEBUG
#define DBG (fmt...) do { printk(KERN_ERR "SBC610: " fmt); } while (0)
#else
#define DBG (fmt...) do { } while (0)
#endif

static void __init gef_sbc610_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;

	for_each_compatible_node(np, "pci", "fsl,mpc8641-pcie") {
		fsl_add_bridge(np, 1);
	}
#endif

	printk(KERN_INFO "GE Fanuc Intelligent Platforms SBC610 6U VPX SBC\n");

#ifdef CONFIG_SMP
	mpc86xx_smp_init();
#endif
}


static void gef_sbc610_show_cpuinfo(struct seq_file *m)
{
	uint memsize = total_memory;
	uint svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: GE Fanuc Intelligent Platforms\n");

	seq_printf(m, "SVR\t\t: 0x%x\n", svid);
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}


/*
 * Called very early, device-tree isn't unflattened
 *
 * This function is called to determine whether the BSP is compatible with the
 * supplied device-tree, which is assumed to be the correct one for the actual
 * board. It is expected thati, in the future, a kernel may support multiple
 * boards.
 */
static int __init gef_sbc610_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "gef,sbc610"))
		return 1;

	return 0;
}

static long __init mpc86xx_time_init(void)
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
	printk(KERN_DEBUG "Probe platform devices\n");
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(gef_sbc610, declare_of_platform_devices);

define_machine(gef_sbc610) {
	.name			= "GE Fanuc SBC610",
	.probe			= gef_sbc610_probe,
	.setup_arch		= gef_sbc610_setup_arch,
	.init_IRQ		= mpc86xx_init_irq,
	.show_cpuinfo		= gef_sbc610_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
};
