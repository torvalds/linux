/*
 * MPC8360E-RDK board file.
 *
 * Copyright (c) 2006  Freescale Semiconductor, Inc.
 * Copyright (c) 2007-2008  MontaVista Software, Inc.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <soc/fsl/qe/qe.h>
#include <soc/fsl/qe/qe_ic.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

machine_device_initcall(mpc836x_rdk, mpc83xx_declare_of_platform_devices);

static void __init mpc836x_rdk_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc836x_rdk_setup_arch()", 0);

	mpc83xx_setup_pci();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened.
 */
static int __init mpc836x_rdk_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,mpc8360rdk");
}

define_machine(mpc836x_rdk) {
	.name		= "MPC836x RDK",
	.probe		= mpc836x_rdk_probe,
	.setup_arch	= mpc836x_rdk_setup_arch,
	.init_IRQ	= mpc83xx_ipic_and_qe_init_IRQ,
	.get_irq	= ipic_get_irq,
	.restart	= mpc83xx_restart,
	.time_init	= mpc83xx_time_init,
	.calibrate_decr	= generic_calibrate_decr,
	.progress	= udbg_progress,
};
