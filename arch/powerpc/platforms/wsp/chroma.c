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

#include <asm/machdep.h>
#include <asm/system.h>
#include <asm/udbg.h>

#include "ics.h"
#include "wsp.h"

void __init chroma_setup_arch(void)
{
	wsp_setup_arch();
	wsp_setup_h8();

}

static int __init chroma_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,wsp-chroma"))
		return 0;

	return 1;
}

define_machine(chroma_md) {
	.name			= "Chroma PCIe",
	.probe			= chroma_probe,
	.setup_arch		= chroma_setup_arch,
	.restart		= wsp_h8_restart,
	.power_off		= wsp_h8_power_off,
	.halt			= wsp_halt,
	.calibrate_decr		= generic_calibrate_decr,
	.init_IRQ		= wsp_setup_irq,
	.progress		= udbg_progress,
	.power_save		= book3e_idle,
};

machine_arch_initcall(chroma_md, wsp_probe_devices);
