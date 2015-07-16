/*
 * BSC913xQDS Board Setup
 *
 * Author:
 *   Harninder Rai <harninder.rai@freescale.com>
 *   Priyanka Jain <Priyanka.Jain@freescale.com>
 *
 * Copyright 2014 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/of_platform.h>
#include <linux/pci.h>
#include <asm/mpic.h>
#include <sysdev/fsl_soc.h>
#include <asm/udbg.h>

#include "mpc85xx.h"
#include "smp.h"

void __init bsc913x_qds_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN |
	  MPIC_SINGLE_DEST_CPU,
	  0, 256, " OpenPIC  ");

	if (!mpic)
		pr_err("bsc913x: Failed to allocate MPIC structure\n");
	else
		mpic_init(mpic);
}

/*
 * Setup the architecture
 */
static void __init bsc913x_qds_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("bsc913x_qds_setup_arch()", 0);

#if defined(CONFIG_SMP)
	mpc85xx_smp_init();
#endif

	pr_info("bsc913x board from Freescale Semiconductor\n");
}

machine_device_initcall(bsc9132_qds, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */

static int __init bsc9132_qds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,bsc9132qds");
}

define_machine(bsc9132_qds) {
	.name			= "BSC9132 QDS",
	.probe			= bsc9132_qds_probe,
	.setup_arch		= bsc913x_qds_setup_arch,
	.init_IRQ		= bsc913x_qds_pic_init,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
