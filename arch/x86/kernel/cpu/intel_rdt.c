/*
 * Resource Director Technology(RDT)
 * - Cache Allocation code.
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Authors:
 *    Fenghua Yu <fenghua.yu@intel.com>
 *    Tony Luck <tony.luck@intel.com>
 *    Vikas Shivappa <vikas.shivappa@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual June 2016, volume 3, section 17.17.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/err.h>

static inline bool get_rdt_resources(void)
{
	bool ret = false;

	if (!boot_cpu_has(X86_FEATURE_RDT_A))
		return false;
	if (boot_cpu_has(X86_FEATURE_CAT_L3))
		ret = true;

	return ret;
}

static int __init intel_rdt_late_init(void)
{
	if (!get_rdt_resources())
		return -ENODEV;

	pr_info("Intel RDT cache allocation detected\n");
	if (boot_cpu_has(X86_FEATURE_CDP_L3))
		pr_info("Intel RDT code data prioritization detected\n");

	return 0;
}

late_initcall(intel_rdt_late_init);
