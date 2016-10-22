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

#include <asm/intel_rdt_common.h>
#include <asm/intel-family.h>
#include <asm/intel_rdt.h>

/*
 * cache_alloc_hsw_probe() - Have to probe for Intel haswell server CPUs
 * as they do not have CPUID enumeration support for Cache allocation.
 * The check for Vendor/Family/Model is not enough to guarantee that
 * the MSRs won't #GP fault because only the following SKUs support
 * CAT:
 *	Intel(R) Xeon(R)  CPU E5-2658  v3  @  2.20GHz
 *	Intel(R) Xeon(R)  CPU E5-2648L v3  @  1.80GHz
 *	Intel(R) Xeon(R)  CPU E5-2628L v3  @  2.00GHz
 *	Intel(R) Xeon(R)  CPU E5-2618L v3  @  2.30GHz
 *	Intel(R) Xeon(R)  CPU E5-2608L v3  @  2.00GHz
 *	Intel(R) Xeon(R)  CPU E5-2658A v3  @  2.20GHz
 *
 * Probe by trying to write the first of the L3 cach mask registers
 * and checking that the bits stick. Max CLOSids is always 4 and max cbm length
 * is always 20 on hsw server parts. The minimum cache bitmask length
 * allowed for HSW server is always 2 bits. Hardcode all of them.
 */
static inline bool cache_alloc_hsw_probe(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model == INTEL_FAM6_HASWELL_X) {
		u32 l, h, max_cbm = BIT_MASK(20) - 1;

		if (wrmsr_safe(IA32_L3_CBM_BASE, max_cbm, 0))
			return false;
		rdmsr(IA32_L3_CBM_BASE, l, h);

		/* If all the bits were set in MSR, return success */
		return l == max_cbm;
	}

	return false;
}

static inline bool get_rdt_resources(void)
{
	if (cache_alloc_hsw_probe())
		return true;

	if (!boot_cpu_has(X86_FEATURE_RDT_A))
		return false;
	if (!boot_cpu_has(X86_FEATURE_CAT_L3))
		return false;

	return true;
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
