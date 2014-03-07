/*
 * Copyright 2008-2011, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/time.h>
#include <linux/of_fdt.h>

#include <asm/machdep.h>
#include <asm/udbg.h>

#include "ics.h"
#include "wsp.h"


static void psr2_spin(void)
{
	hard_irq_disable();
	for (;;)
		continue;
}

static void psr2_restart(char *cmd)
{
	psr2_spin();
}

static int __init psr2_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "ibm,wsp-chroma")) {
		/* chroma systems also claim they are psr2s */
		return 0;
	}

	if (!of_flat_dt_is_compatible(root, "ibm,psr2"))
		return 0;

	return 1;
}

define_machine(psr2_md) {
	.name			= "PSR2 A2",
	.probe			= psr2_probe,
	.setup_arch		= wsp_setup_arch,
	.restart		= psr2_restart,
	.power_off		= psr2_spin,
	.halt			= psr2_spin,
	.calibrate_decr		= generic_calibrate_decr,
	.init_IRQ		= wsp_setup_irq,
	.progress		= udbg_progress,
	.power_save		= book3e_idle,
};

machine_arch_initcall(psr2_md, wsp_probe_devices);
