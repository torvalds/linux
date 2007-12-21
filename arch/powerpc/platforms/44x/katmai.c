/*
 * Katmai board specific routines
 *
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * Copyright 2007 IBM Corp.
 *
 * Based on the Bamboo code by
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 * Copyright 2007 IBM Corporation
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>

#include "44x.h"

static struct of_device_id katmai_of_bus[] = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init katmai_device_probe(void)
{
	if (!machine_is(katmai))
		return 0;

	of_platform_bus_probe(NULL, katmai_of_bus, NULL);

	return 0;
}
device_initcall(katmai_device_probe);

static int __init katmai_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "amcc,katmai"))
		return 0;

	return 1;
}

define_machine(katmai) {
	.name 				= "Katmai",
	.probe 				= katmai_probe,
	.progress 			= udbg_progress,
	.init_IRQ 			= uic_init_tree,
	.get_irq 			= uic_get_irq,
	.restart			= ppc44x_reset_system,
	.calibrate_decr			= generic_calibrate_decr,
};
