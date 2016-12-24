/*
 * ppa8548 setup and early boot code.
 *
 * Copyright 2009 Prodrive B.V..
 *
 * By Stef van Os (see MAINTAINERS for contact information)
 *
 * Based on the SBC8548 support - Copyright 2007 Wind River Systems Inc.
 * Based on the MPC8548CDS support - Copyright 2005 Freescale Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>

static void __init ppa8548_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);
}

/*
 * Setup the architecture
 */
static void __init ppa8548_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("ppa8548_setup_arch()", 0);
}

static void ppa8548_show_cpuinfo(struct seq_file *m)
{
	uint32_t svid, phid1;

	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Prodrive B.V.\n");
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));
}

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .name = "soc", },
	{ .type = "soc", },
	{ .compatible = "simple-bus", },
	{ .compatible = "gianfar", },
	{ .compatible = "fsl,srio", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(ppa8548, declare_of_platform_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init ppa8548_probe(void)
{
	return of_machine_is_compatible("ppa8548");
}

define_machine(ppa8548) {
	.name		= "ppa8548",
	.probe		= ppa8548_probe,
	.setup_arch	= ppa8548_setup_arch,
	.init_IRQ	= ppa8548_pic_init,
	.show_cpuinfo	= ppa8548_show_cpuinfo,
	.get_irq	= mpic_get_irq,
	.calibrate_decr = generic_calibrate_decr,
	.progress	= udbg_progress,
};
