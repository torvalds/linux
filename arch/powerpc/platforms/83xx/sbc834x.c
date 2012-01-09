/*
 * arch/powerpc/platforms/83xx/sbc834x.c
 *
 * Wind River SBC834x board specific routines
 *
 * By Paul Gortmaker (see MAINTAINERS for contact information)
 *
 * Based largely on the mpc834x_mds.c support by Kumar Gala.
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
#include <linux/of_platform.h>

#include <asm/system.h>
#include <linux/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc83xx.h"

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init sbc834x_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("sbc834x_setup_arch()", 0);

	mpc83xx_setup_pci();
}

machine_device_initcall(sbc834x, mpc83xx_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init sbc834x_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "SBC834xE");
}

define_machine(sbc834x) {
	.name			= "SBC834xE",
	.probe			= sbc834x_probe,
	.setup_arch		= sbc834x_setup_arch,
	.init_IRQ		= mpc83xx_ipic_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
