/*
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby, <jrigby@freescale.com>
 *
 * Description:
 * MPC512x SoC setup
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/prom.h>
#include <asm/time.h>

#include "mpc512x.h"

/*
 * list of supported boards
 */
static const char * const board[] __initconst = {
	"prt,prtlvt",
	"fsl,mpc5125ads",
	"ifm,ac14xx",
	NULL
};

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc512x_generic_probe(void)
{
	return of_device_compatible_match(of_root, board);
}

define_machine(mpc512x_generic) {
	.name			= "MPC512x generic",
	.probe			= mpc512x_generic_probe,
	.init			= mpc512x_init,
	.init_early		= mpc512x_init_early,
	.setup_arch		= mpc512x_setup_arch,
	.init_IRQ		= mpc512x_init_IRQ,
	.get_irq		= ipic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
	.restart		= mpc512x_restart,
};
