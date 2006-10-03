/*
 * Copyright (C) Freescale Semicondutor, Inc. 2006. All rights reserved.
 *
 * Description:
 * MPC832xE MDS board specific routines.
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
#include <linux/root_dev.h>
#include <linux/initrd.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <asm/qe.h>
#include <asm/qe_ic.h>

#include "mpc83xx.h"
#include "mpc832x_mds.h"

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

static u8 *bcsr_regs = NULL;

u8 *get_bcsr(void)
{
	return bcsr_regs;
}

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc832x_sys_setup_arch(void)
{
	struct device_node *np;

	if (ppc_md.progress)
		ppc_md.progress("mpc832x_sys_setup_arch()", 0);

	np = of_find_node_by_type(NULL, "cpu");
	if (np != 0) {
		unsigned int *fp =
		    (int *)get_property(np, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(np);
	}

	/* Map BCSR area */
	np = of_find_node_by_name(NULL, "bcsr");
	if (np != 0) {
		struct resource res;

		of_address_to_resource(np, 0, &res);
		bcsr_regs = ioremap(res.start, res.end - res.start +1);
		of_node_put(np);
	}

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_exclude_device = mpc83xx_exclude_device;
#endif

#ifdef CONFIG_QUICC_ENGINE
	qe_reset();

	if ((np = of_find_node_by_name(np, "par_io")) != NULL) {
		par_io_init(np);
		of_node_put(np);

		for (np = NULL; (np = of_find_node_by_name(np, "ucc")) != NULL;)
			par_io_of_config(np);
	}

	if ((np = of_find_compatible_node(NULL, "network", "ucc_geth"))
			!= NULL){
		/* Reset the Ethernet PHY */
		bcsr_regs[9] &= ~0x20;
		udelay(1000);
		bcsr_regs[9] |= 0x20;
		iounmap(bcsr_regs);
		of_node_put(np);
	}

#endif				/* CONFIG_QUICC_ENGINE */

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef  CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif
}

void __init mpc832x_sys_init_IRQ(void)
{

	struct device_node *np;

	np = of_find_node_by_type(NULL, "ipic");
	if (!np)
		return;

	ipic_init(np, 0);

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
	of_node_put(np);

#ifdef CONFIG_QUICC_ENGINE
	np = of_find_node_by_type(NULL, "qeic");
	if (!np)
		return;

	qe_ic_init(np, 0);
	of_node_put(np);
#endif				/* CONFIG_QUICC_ENGINE */
}

#if defined(CONFIG_I2C_MPC) && defined(CONFIG_SENSORS_DS1374)
extern ulong ds1374_get_rtc_time(void);
extern int ds1374_set_rtc_time(ulong);

static int __init mpc832x_rtc_hookup(void)
{
	struct timespec tv;

	ppc_md.get_rtc_time = ds1374_get_rtc_time;
	ppc_md.set_rtc_time = ds1374_set_rtc_time;

	tv.tv_nsec = 0;
	tv.tv_sec = (ppc_md.get_rtc_time) ();
	do_settimeofday(&tv);

	return 0;
}

late_initcall(mpc832x_rtc_hookup);
#endif

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc832x_sys_probe(void)
{
	char *model = of_get_flat_dt_prop(of_get_flat_dt_root(),
					  "model", NULL);

	if (model == NULL)
		return 0;
	if (strcmp(model, "MPC8323EMDS"))
		return 0;

	DBG("%s found\n", model);

	return 1;
}

define_machine(mpc832x_mds) {
	.name 		= "MPC832x MDS",
	.probe 		= mpc832x_sys_probe,
	.setup_arch 	= mpc832x_sys_setup_arch,
	.init_IRQ 	= mpc832x_sys_init_IRQ,
	.get_irq 	= ipic_get_irq,
	.restart 	= mpc83xx_restart,
	.time_init 	= mpc83xx_time_init,
	.calibrate_decr	= generic_calibrate_decr,
	.progress 	= udbg_progress,
};
