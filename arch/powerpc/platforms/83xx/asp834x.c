// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/platforms/83xx/asp834x.c
 *
 * Analogue & Micro ASP8347 board specific routines
 * clone of mpc834x_itx
 *
 * Copyright 2008 Codehermit
 *
 * Maintainer: Bryan O'Donoghue <bodonoghue@codhermit.ie>
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
	mpc83xx_setup_arch();
	mpc834x_usb_cfg();
}

machine_device_initcall(asp834x, mpc83xx_declare_of_platform_devices);

define_machine(asp834x) {
	.name			= "ASP8347E",
	.compatible		= "analogue-and-micro,asp8347e",
	.setup_arch		= asp834x_setup_arch,
	.discover_phbs		= mpc83xx_setup_pci,
	.init_IRQ		= mpc83xx_ipic_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.progress		= udbg_progress,
};
