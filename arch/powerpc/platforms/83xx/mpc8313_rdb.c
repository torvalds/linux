/*
 * arch/powerpc/platforms/83xx/mpc8313_rdb.c
 *
 * Description: MPC8313x RDB board specific routines.
 * This file is based on mpc834x_sys.c
 * Author: Lo Wlison <r43300@freescale.com>
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2006. All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pci.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>

#include "mpc83xx.h"

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
static void __init mpc8313_rdb_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc8313_rdb_setup_arch()", 0);

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	ppc_md.pci_exclude_device = mpc83xx_exclude_device;
#endif
}

void __init mpc8313_rdb_init_IRQ(void)
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
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc8313_rdb_probe(void)
{
        unsigned long root = of_get_flat_dt_root();

        return of_flat_dt_is_compatible(root, "MPC8313ERDB");
}

define_machine(mpc8313_rdb) {
	.name			= "MPC8313 RDB",
	.probe			= mpc8313_rdb_probe,
	.setup_arch		= mpc8313_rdb_setup_arch,
	.init_IRQ		= mpc8313_rdb_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
