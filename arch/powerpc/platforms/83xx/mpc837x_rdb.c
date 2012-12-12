/*
 * arch/powerpc/platforms/83xx/mpc837x_rdb.c
 *
 * Copyright (C) 2007 Freescale Semicondutor, Inc. All rights reserved.
 *
 * MPC837x RDB board specific routines
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

static void mpc837x_rdb_sd_cfg(void)
{
	void __iomem *im;

	im = ioremap(get_immrbase(), 0x1000);
	if (!im) {
		WARN_ON(1);
		return;
	}

	/*
	 * On RDB boards (in contrast to MDS) USBB pins are used for SD only,
	 * so we can safely mux them away from the USB block.
	 */
	clrsetbits_be32(im + MPC83XX_SICRL_OFFS, MPC837X_SICRL_USBB_MASK,
						 MPC837X_SICRL_SD);
	clrsetbits_be32(im + MPC83XX_SICRH_OFFS, MPC837X_SICRH_SPI_MASK,
						 MPC837X_SICRH_SD);
	iounmap(im);
}

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc837x_rdb_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc837x_rdb_setup_arch()", 0);

	mpc83xx_setup_pci();
	mpc837x_usb_cfg();
	mpc837x_rdb_sd_cfg();
}

machine_device_initcall(mpc837x_rdb, mpc83xx_declare_of_platform_devices);

static const char * const board[] __initconst = {
	"fsl,mpc8377rdb",
	"fsl,mpc8378rdb",
	"fsl,mpc8379rdb",
	"fsl,mpc8377wlan",
	NULL
};

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc837x_rdb_probe(void)
{
	return of_flat_dt_match(of_get_flat_dt_root(), board);
}

define_machine(mpc837x_rdb) {
	.name			= "MPC837x RDB/WLAN",
	.probe			= mpc837x_rdb_probe,
	.setup_arch		= mpc837x_rdb_setup_arch,
	.init_IRQ		= mpc83xx_ipic_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
