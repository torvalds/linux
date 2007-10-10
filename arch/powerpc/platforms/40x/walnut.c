/*
 * Architecture- / platform-specific boot-time initialization code for
 * IBM PowerPC 4xx based boards. Adapted from original
 * code by Gary Thomas, Cort Dougan <cort@fsmlabs.com>, and Dan Malek
 * <dan@net4x.com>.
 *
 * Copyright(c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *
 * Rewritten and ported to the merged powerpc tree:
 * Copyright 2007 IBM Corporation
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/init.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/of_platform.h>

static struct of_device_id walnut_of_bus[] = {
	{ .compatible = "ibm,plb3", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init walnut_device_probe(void)
{
	if (!machine_is(walnut))
		return 0;

	/* FIXME: do bus probe here */
	of_platform_bus_probe(NULL, walnut_of_bus, NULL);

	return 0;
}
device_initcall(walnut_device_probe);

static int __init walnut_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,walnut"))
		return 0;

	return 1;
}

define_machine(walnut) {
	.name			= "Walnut",
	.probe			= walnut_probe,
	.progress		= udbg_progress,
	.init_IRQ		= uic_init_tree,
	.get_irq		= uic_get_irq,
	.calibrate_decr	= generic_calibrate_decr,
};
