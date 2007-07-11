/*
 * MPC8544 DS Board Setup
 *
 * Author Xianghua Xiao (x.xiao@freescale.com)
 * Copyright 2007 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/mpc85xx.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>
#include <asm/i8259.h>

#include <sysdev/fsl_soc.h>
#include "mpc85xx.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define DBG(fmt, args...)
#endif


void __init mpc8544_ds_pic_init(void)
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

	DBG("mpc8544ds: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc8544_8259_cascade);
#endif	/* CONFIG_PPC_I8259 */
}


/*
 * Setup the architecture
 */
static void __init mpc8544_ds_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc8544_ds_setup_arch()", 0);

	printk("MPC8544 DS board from Freescale Semiconductor\n");
}


/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc8544_ds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "MPC8544DS");
}

define_machine(mpc8544_ds) {
	.name			= "MPC8544 DS",
	.probe			= mpc8544_ds_probe,
	.setup_arch		= mpc8544_ds_setup_arch,
	.init_IRQ		= mpc8544_ds_pic_init,
	.get_irq		= mpic_get_irq,
	.restart		= mpc85xx_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
