/*
 * Sequoia board specific routines
 *
 * Valentine Barshak <vbarshak@ru.mvista.com>
 * Copyright 2007 MontaVista Software Inc.
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

static struct of_device_id sequoia_of_bus[] = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init sequoia_device_probe(void)
{
	if (!machine_is(sequoia))
		return 0;

	of_platform_bus_probe(NULL, sequoia_of_bus, NULL);

	return 0;
}
device_initcall(sequoia_device_probe);

static int __init sequoia_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "amcc,sequoia"))
		return 0;

	return 1;
}

define_machine(sequoia) {
	.name 				= "Sequoia",
	.probe 				= sequoia_probe,
	.progress 			= udbg_progress,
	.init_IRQ 			= uic_init_tree,
	.get_irq 			= uic_get_irq,
	.restart			= ppc44x_reset_system,
	.calibrate_decr			= generic_calibrate_decr,
};
