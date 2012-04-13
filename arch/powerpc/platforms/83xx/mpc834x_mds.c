/*
 * arch/powerpc/platforms/83xx/mpc834x_mds.c
 *
 * MPC834x MDS board specific routines
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
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

#define BCSR5_INT_USB		0x02
static int mpc834xemds_usb_cfg(void)
{
	struct device_node *np;
	void __iomem *bcsr_regs = NULL;
	u8 bcsr5;

	mpc834x_usb_cfg();
	/* Map BCSR area */
	np = of_find_node_by_name(NULL, "bcsr");
	if (np) {
		struct resource res;

		of_address_to_resource(np, 0, &res);
		bcsr_regs = ioremap(res.start, resource_size(&res));
		of_node_put(np);
	}
	if (!bcsr_regs)
		return -1;

	/*
	 * if Processor Board is plugged into PIB board,
	 * force to use the PHY on Processor Board
	 */
	bcsr5 = in_8(bcsr_regs + 5);
	if (!(bcsr5 & BCSR5_INT_USB))
		out_8(bcsr_regs + 5, (bcsr5 | BCSR5_INT_USB));
	iounmap(bcsr_regs);
	return 0;
}

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc834x_mds_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc834x_mds_setup_arch()", 0);

	mpc83xx_setup_pci();

	mpc834xemds_usb_cfg();
}

machine_device_initcall(mpc834x_mds, mpc83xx_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc834x_mds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "MPC834xMDS");
}

define_machine(mpc834x_mds) {
	.name			= "MPC834x MDS",
	.probe			= mpc834x_mds_probe,
	.setup_arch		= mpc834x_mds_setup_arch,
	.init_IRQ		= mpc83xx_ipic_init_IRQ,
	.get_irq		= ipic_get_irq,
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
