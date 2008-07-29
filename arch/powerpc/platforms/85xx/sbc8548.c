/*
 * Wind River SBC8548 setup and early boot code.
 *
 * Copyright 2007 Wind River Systems Inc.
 *
 * By Paul Gortmaker (see MAINTAINERS for contact information)
 *
 * Based largely on the MPC8548CDS support - Copyright 2005 Freescale Inc.
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fsl_devices.h>
#include <linux/of_platform.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

static int sbc_rev;

static void __init sbc8548_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np = NULL;

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

	/* Return the mpic node */
	of_node_put(np);

	mpic_init(mpic);
}

/* Extract the HW Rev from the EPLD on the board */
static int __init sbc8548_hw_rev(void)
{
	struct device_node *np;
	struct resource res;
	unsigned int *rev;
	int board_rev = 0;

	np = of_find_compatible_node(NULL, NULL, "hw-rev");
	if (np == NULL) {
		printk("No HW-REV found in DTB.\n");
		return -ENODEV;
	}

	of_address_to_resource(np, 0, &res);
	of_node_put(np);

	rev = ioremap(res.start,sizeof(unsigned int));
	board_rev = (*rev) >> 28;
	iounmap(rev);

	return board_rev;
}

/*
 * Setup the architecture
 */
static void __init sbc8548_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("sbc8548_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8540-pci") ||
		    of_device_is_compatible(np, "fsl,mpc8548-pcie")) {
			struct resource rsrc;
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == 0x8000)
				fsl_add_bridge(np, 1);
			else
				fsl_add_bridge(np, 0);
		}
	}
#endif
	sbc_rev = sbc8548_hw_rev();
}

static void sbc8548_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Wind River\n");
	seq_printf(m, "Machine\t\t: SBC8548 v%d\n", sbc_rev);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}

static struct of_device_id __initdata of_bus_ids[] = {
	{ .name = "soc", },
	{ .type = "soc", },
	{ .compatible = "simple-bus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(sbc8548, declare_of_platform_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init sbc8548_probe(void)
{
        unsigned long root = of_get_flat_dt_root();

        return of_flat_dt_is_compatible(root, "SBC8548");
}

define_machine(sbc8548) {
	.name		= "SBC8548",
	.probe		= sbc8548_probe,
	.setup_arch	= sbc8548_setup_arch,
	.init_IRQ	= sbc8548_pic_init,
	.show_cpuinfo	= sbc8548_show_cpuinfo,
	.get_irq	= mpic_get_irq,
	.restart	= fsl_rstcr_restart,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.calibrate_decr = generic_calibrate_decr,
	.progress	= udbg_progress,
};
