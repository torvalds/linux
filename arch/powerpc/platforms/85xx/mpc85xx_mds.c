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
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>
#include <asm/qe.h>
#include <asm/qe_ic.h>
#include <asm/mpic.h>

#undef DEBUG
#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
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

	/* Map BCSR area */
	np = of_find_node_by_name(NULL, "bcsr");
	if (np != NULL) {
		struct resource res;

		of_address_to_resource(np, 0, &res);
		bcsr_regs = ioremap(res.start, res.end - res.start +1);
		of_node_put(np);
	}

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
#define BCSR_UCC1_GETH_EN	(0x1 << 7)
#define BCSR_UCC2_GETH_EN	(0x1 << 7)
#define BCSR_UCC1_MODE_MSK	(0x3 << 4)
#define BCSR_UCC2_MODE_MSK	(0x3 << 0)

		/* Turn off UCC1 & UCC2 */
		clrbits8(&bcsr_regs[8], BCSR_UCC1_GETH_EN);
		clrbits8(&bcsr_regs[9], BCSR_UCC2_GETH_EN);

		/* Mode is RGMII, all bits clear */
		clrbits8(&bcsr_regs[11], BCSR_UCC1_MODE_MSK |
					 BCSR_UCC2_MODE_MSK);

		/* Turn UCC1 & UCC2 on */
		setbits8(&bcsr_regs[8], BCSR_UCC1_GETH_EN);
		setbits8(&bcsr_regs[9], BCSR_UCC2_GETH_EN);

		iounmap(bcsr_regs);
	}

#endif	/* CONFIG_QUICC_ENGINE */
}

static struct of_device_id mpc85xx_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .type = "qe", },
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
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	of_node_put(np);

	mpic_init(mpic);

#ifdef CONFIG_QUICC_ENGINE
	np = of_find_node_by_type(NULL, "qeic");
	if (!np)
		return;

	qe_ic_init(np, 0, qe_ic_cascade_muxed_mpic, NULL);
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
	.restart	= fsl_rstcr_restart,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
};
