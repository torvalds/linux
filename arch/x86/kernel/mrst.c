/*
 * mrst.c: Intel Moorestown platform specific setup code
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/mrst.h>
#include <asm/io.h>
#include <asm/i8259.h>

/*
 * Moorestown specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_mrst_early_setup(void)
{
	x86_init.resources.probe_roms = x86_init_noop;
	x86_init.resources.reserve_resources = x86_init_noop;

	x86_init.pci.init = pci_mrst_init;
	x86_init.pci.fixup_irqs = x86_init_noop;

	legacy_pic = &null_legacy_pic;
}
