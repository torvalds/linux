// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BSC913xRDB Board Setup
 *
 * Author: Priyanka Jain <Priyanka.Jain@freescale.com>
 *
 * Copyright 2011-2012 Freescale Semiconductor Inc.
 */

#include <linux/of_platform.h>
#include <linux/pci.h>
#include <asm/mpic.h>
#include <sysdev/fsl_soc.h>
#include <asm/udbg.h>

#include "mpc85xx.h"

void __init bsc913x_rdb_pic_init(void)
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
static void __init bsc913x_rdb_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("bsc913x_rdb_setup_arch()", 0);

	pr_info("bsc913x board from Freescale Semiconductor\n");
}

machine_device_initcall(bsc9131_rdb, mpc85xx_common_publish_devices);

define_machine(bsc9131_rdb) {
	.name			= "BSC9131 RDB",
	.compatible		= "fsl,bsc9131rdb",
	.setup_arch		= bsc913x_rdb_setup_arch,
	.init_IRQ		= bsc913x_rdb_pic_init,
	.get_irq		= mpic_get_irq,
	.progress		= udbg_progress,
};
