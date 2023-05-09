// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2008 Emcraft Systems
 * Sergei Poselenov <sposelenov@emcraft.com>
 *
 * Based on MPC8560 ADS and arch/ppc tqm85xx ports
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2008 Freescale Semiconductor Inc.
 *
 * Copyright (c) 2005-2006 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 * 	Kumar Gala <kumar.gala@freescale.com>
 *      Copyright 2004 Freescale Semiconductor Inc.
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
#include <asm/mpic.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc85xx.h"
#include "socrates_fpga_pic.h"

static void __init socrates_pic_init(void)
{
	struct device_node *np;

	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);

	np = of_find_compatible_node(NULL, NULL, "abb,socrates-fpga-pic");
	if (!np) {
		printk(KERN_ERR "Could not find socrates-fpga-pic node\n");
		return;
	}
	socrates_fpga_pic_init(np);
	of_node_put(np);
}

/*
 * Setup the architecture
 */
static void __init socrates_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("socrates_setup_arch()", 0);

	fsl_pci_assign_primary();
}

machine_arch_initcall(socrates, mpc85xx_common_publish_devices);

define_machine(socrates) {
	.name			= "Socrates",
	.compatible		= "abb,socrates",
	.setup_arch		= socrates_setup_arch,
	.init_IRQ		= socrates_pic_init,
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
