// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Extreme Engineering Solutions, Inc.
 *
 * X-ES board-specific functionality
 *
 * Based on mpc85xx_ds code from Freescale Semiconductor, Inc.
 *
 * Author: Nate Case <ncase@xes-inc.com>
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include "smp.h"

#include "mpc85xx.h"

/* A few bit definitions needed for fixups on some boards */
#define MPC85xx_L2CTL_L2E		0x80000000 /* L2 enable */
#define MPC85xx_L2CTL_L2I		0x40000000 /* L2 flash invalidate */
#define MPC85xx_L2CTL_L2SIZ_MASK	0x30000000 /* L2 SRAM size (R/O) */

void __init xes_mpc85xx_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);
}

static void __init xes_mpc85xx_configure_l2(void __iomem *l2_base)
{
	volatile uint32_t ctl, tmp;

	asm volatile("msync; isync");
	tmp = in_be32(l2_base);

	/*
	 * xMon may have enabled part of L2 as SRAM, so we need to set it
	 * up for all cache mode just to be safe.
	 */
	printk(KERN_INFO "xes_mpc85xx: Enabling L2 as cache\n");

	ctl = MPC85xx_L2CTL_L2E | MPC85xx_L2CTL_L2I;
	if (of_machine_is_compatible("MPC8540") ||
	    of_machine_is_compatible("MPC8560"))
		/*
		 * Assume L2 SRAM is used fully for cache, so set
		 * L2BLKSZ (bits 4:5) to match L2SIZ (bits 2:3).
		 */
		ctl |= (tmp & MPC85xx_L2CTL_L2SIZ_MASK) >> 2;

	asm volatile("msync; isync");
	out_be32(l2_base, ctl);
	asm volatile("msync; isync");
}

static void __init xes_mpc85xx_fixups(void)
{
	struct device_node *np;
	int err;

	/*
	 * Legacy xMon firmware on some X-ES boards does not enable L2
	 * as cache.  We must ensure that they get enabled here.
	 */
	for_each_node_by_name(np, "l2-cache-controller") {
		struct resource r[2];
		void __iomem *l2_base;

		/* Only MPC8548, MPC8540, and MPC8560 boards are affected */
		if (!of_device_is_compatible(np,
				    "fsl,mpc8548-l2-cache-controller") &&
		    !of_device_is_compatible(np,
				    "fsl,mpc8540-l2-cache-controller") &&
		    !of_device_is_compatible(np,
				    "fsl,mpc8560-l2-cache-controller"))
			continue;

		err = of_address_to_resource(np, 0, &r[0]);
		if (err) {
			printk(KERN_WARNING "xes_mpc85xx: Could not get "
			       "resource for device tree node '%pOF'",
			       np);
			continue;
		}

		l2_base = ioremap(r[0].start, resource_size(&r[0]));

		xes_mpc85xx_configure_l2(l2_base);
	}
}

/*
 * Setup the architecture
 */
static void __init xes_mpc85xx_setup_arch(void)
{
	struct device_node *root;
	const char *model = "Unknown";

	root = of_find_node_by_path("/");
	if (root == NULL)
		return;

	model = of_get_property(root, "model", NULL);

	printk(KERN_INFO "X-ES MPC85xx-based single-board computer: %s\n",
	       model + strlen("xes,"));

	xes_mpc85xx_fixups();

	mpc85xx_smp_init();

	fsl_pci_assign_primary();
}

machine_arch_initcall(xes_mpc8572, mpc85xx_common_publish_devices);
machine_arch_initcall(xes_mpc8548, mpc85xx_common_publish_devices);
machine_arch_initcall(xes_mpc8540, mpc85xx_common_publish_devices);

define_machine(xes_mpc8572) {
	.name			= "X-ES MPC8572",
	.compatible		= "xes,MPC8572",
	.setup_arch		= xes_mpc85xx_setup_arch,
	.init_IRQ		= xes_mpc85xx_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(xes_mpc8548) {
	.name			= "X-ES MPC8548",
	.compatible		= "xes,MPC8548",
	.setup_arch		= xes_mpc85xx_setup_arch,
	.init_IRQ		= xes_mpc85xx_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};

define_machine(xes_mpc8540) {
	.name			= "X-ES MPC8540",
	.compatible		= "xes,MPC8540",
	.setup_arch		= xes_mpc85xx_setup_arch,
	.init_IRQ		= xes_mpc85xx_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
