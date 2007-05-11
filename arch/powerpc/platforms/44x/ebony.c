/*
 * Ebony board specific routines
 *
 * Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003-2005 Zultys Technologies
 *
 * Rewritten and ported to the merged powerpc tree:
 * Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/of_platform.h>

#include "44x.h"

static struct of_device_id ebony_of_bus[] = {
	{ .type = "ibm,plb", },
	{ .type = "ibm,opb", },
	{ .type = "ibm,ebc", },
	{},
};

static int __init ebony_device_probe(void)
{
	if (!machine_is(ebony))
		return 0;

	of_platform_bus_probe(NULL, ebony_of_bus, NULL);

	return 0;
}
device_initcall(ebony_device_probe);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init ebony_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,ebony"))
		return 0;

	return 1;
}

static void __init ebony_setup_arch(void)
{
}

define_machine(ebony) {
	.name			= "Ebony",
	.probe			= ebony_probe,
	.setup_arch		= ebony_setup_arch,
	.progress		= udbg_progress,
	.init_IRQ		= uic_init_tree,
	.get_irq		= uic_get_irq,
	.restart		= ppc44x_reset_system,
	.calibrate_decr		= generic_calibrate_decr,
};
