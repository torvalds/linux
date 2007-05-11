/*
 * Copyright (C) Freescale Semicondutor, Inc. 2006-2007. All rights reserved.
 *
 * Author: Andy Fleming <afleming@freescale.com>
 *
 * Based on 83xx/mpc8360e_pb.c by:
 *	   Li Yang <LeoLi@freescale.com>
 *	   Yin Olivia <Hong-hua.Yin@freescale.com>
 *
 * Description:
 * MPC85xx MDS board specific routines.
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
#include <linux/fsl_devices.h>

#include <asm/of_device.h>
#include <asm/of_platform.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <asm/qe.h>
#include <asm/qe_ic.h>
#include <asm/mpic.h>

#include "mpc85xx.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc85xx_mds_setup_arch(void)
{
	struct device_node *np;
	static u8 *bcsr_regs = NULL;

	if (ppc_md.progress)
		ppc_md.progress("mpc85xx_mds_setup_arch()", 0);

	np = of_find_node_by_type(NULL, "cpu");
	if (np != NULL) {
		const unsigned int *fp =
		    of_get_property(np, "clock-frequency", NULL);
		if (fp != NULL)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(np);
	}

	/* Map BCSR area */
	np = of_find_node_by_name(NULL, "bcsr");
	if (np != NULL) {
		struct resource res;

		of_address_to_resource(np, 0, &res);
		bcsr_regs = ioremap(res.start, res.end - res.start +1);
		of_node_put(np);
	}

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;) {
		add_bridge(np);
	}
	of_node_put(np);
#endif

#ifdef CONFIG_QUICC_ENGINE
	if ((np = of_find_node_by_name(NULL, "qe")) != NULL) {
		qe_reset();
		of_node_put(np);
	}

	if ((np = of_find_node_by_name(NULL, "par_io")) != NULL) {
		struct device_node *ucc = NULL;

		par_io_init(np);
		of_node_put(np);

		for ( ;(ucc = of_find_node_by_name(ucc, "ucc")) != NULL;)
			par_io_of_config(ucc);

		of_node_put(ucc);
	}

	if (bcsr_regs) {
		u8 bcsr_phy;

		/* Reset the Ethernet PHY */
		bcsr_phy = in_be8(&bcsr_regs[9]);
		bcsr_phy &= ~0x20;
		out_be8(&bcsr_regs[9], bcsr_phy);

		udelay(1000);

		bcsr_phy = in_be8(&bcsr_regs[9]);
		bcsr_phy |= 0x20;
		out_be8(&bcsr_regs[9], bcsr_phy);

		iounmap(bcsr_regs);
	}

#endif	/* CONFIG_QUICC_ENGINE */
}

static struct of_device_id mpc85xx_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .type = "qe", },
	{ .type = "mdio", },
	{},
};

static int __init mpc85xx_publish_devices(void)
{
	if (!machine_is(mpc85xx_mds))
		return 0;

	/* Publish the QE devices */
	of_platform_bus_probe(NULL,mpc85xx_ids,NULL);

	return 0;
}
device_initcall(mpc85xx_publish_devices);

static void __init mpc85xx_mds_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np = NULL;

	np = of_find_node_by_type(NULL, "open-pic");
	if (!np)
		return;

	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Failed to map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			4, 0, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	of_node_put(np);

	/* Internal Interrupts */
	mpic_assign_isu(mpic, 0, r.start + 0x10200);
	mpic_assign_isu(mpic, 1, r.start + 0x10280);
	mpic_assign_isu(mpic, 2, r.start + 0x10300);
	mpic_assign_isu(mpic, 3, r.start + 0x10380);
	mpic_assign_isu(mpic, 4, r.start + 0x10400);
	mpic_assign_isu(mpic, 5, r.start + 0x10480);
	mpic_assign_isu(mpic, 6, r.start + 0x10500);
	mpic_assign_isu(mpic, 7, r.start + 0x10580);
	mpic_assign_isu(mpic, 8, r.start + 0x10600);
	mpic_assign_isu(mpic, 9, r.start + 0x10680);
	mpic_assign_isu(mpic, 10, r.start + 0x10700);
	mpic_assign_isu(mpic, 11, r.start + 0x10780);

	/* External Interrupts */
	mpic_assign_isu(mpic, 12, r.start + 0x10000);
	mpic_assign_isu(mpic, 13, r.start + 0x10080);
	mpic_assign_isu(mpic, 14, r.start + 0x10100);

	mpic_init(mpic);

#ifdef CONFIG_QUICC_ENGINE
	np = of_find_node_by_type(NULL, "qeic");
	if (!np)
		return;

	qe_ic_init(np, 0);
	of_node_put(np);
#endif				/* CONFIG_QUICC_ENGINE */
}

static int __init mpc85xx_mds_probe(void)
{
        unsigned long root = of_get_flat_dt_root();

        return of_flat_dt_is_compatible(root, "MPC85xxMDS");
}

define_machine(mpc85xx_mds) {
	.name		= "MPC85xx MDS",
	.probe		= mpc85xx_mds_probe,
	.setup_arch	= mpc85xx_mds_setup_arch,
	.init_IRQ	= mpc85xx_mds_pic_init,
	.get_irq	= mpic_get_irq,
	.restart	= mpc85xx_restart,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
};
