/*
 * arch/powerpc/platforms/83xx/asp834x.c
 *
 * Analogue & Micro ASP8347 board specific routines
 * clone of mpc834x_itx
 *
 * Copyright 2008 Codehermit
 *
 * Maintainer: Bryan O'Donoghue <bodonoghue@codhermit.ie>
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

#include "mpc83xx.h"

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init asp834x_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("asp834x_setup_arch()", 0);

	mpc834x_usb_cfg();
}

machine_device_initcall(asp834x, mpc83xx_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init asp834x_probe(void)
{
	return of_machine_is_compatible("analogue-and-micro,asp8347e");
}

define_machine(asp834x) {
	.name			= "ASP8347E",
	.probe			= asp834x_probe,
	.setup_arch		= asp834x_setup_arch,
	.init_IRQ		= mpc83xx_ipic_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
