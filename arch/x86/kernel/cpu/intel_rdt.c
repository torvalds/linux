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

#define domain_init(id) LIST_HEAD_INIT(rdt_resources_all[id].domains)

struct rdt_resource rdt_resources_all[] = {
	{
		.name		= "L3",
		.domains	= domain_init(RDT_RESOURCE_L3),
		.msr_base	= IA32_L3_CBM_BASE,
		.min_cbm_bits	= 1,
		.cache_level	= 3,
		.cbm_idx_multi	= 1,
		.cbm_idx_offset	= 0
	},
	{
		.name		= "L3DATA",
		.domains	= domain_init(RDT_RESOURCE_L3DATA),
		.msr_base	= IA32_L3_CBM_BASE,
		.min_cbm_bits	= 1,
		.cache_level	= 3,
		.cbm_idx_multi	= 2,
		.cbm_idx_offset	= 0
	},
	{
		.name		= "L3CODE",
		.domains	= domain_init(RDT_RESOURCE_L3CODE),
		.msr_base	= IA32_L3_CBM_BASE,
		.min_cbm_bits	= 1,
		.cache_level	= 3,
		.cbm_idx_multi	= 2,
		.cbm_idx_offset	= 1
	},
	{
		.name		= "L2",
		.domains	= domain_init(RDT_RESOURCE_L2),
		.msr_base	= IA32_L2_CBM_BASE,
		.min_cbm_bits	= 1,
		.cache_level	= 2,
		.cbm_idx_multi	= 1,
		.cbm_idx_offset	= 0
	},
};

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
		struct rdt_resource *r  = &rdt_resources_all[RDT_RESOURCE_L3];
		u32 l, h, max_cbm = BIT_MASK(20) - 1;

		if (wrmsr_safe(IA32_L3_CBM_BASE, max_cbm, 0))
			return false;
		rdmsr(IA32_L3_CBM_BASE, l, h);

		/* If all the bits were set in MSR, return success */
		if (l != max_cbm)
			return false;

		r->num_closid = 4;
		r->cbm_len = 20;
		r->max_cbm = max_cbm;
		r->min_cbm_bits = 2;
		r->capable = true;
		r->enabled = true;

		return true;
	}

	return false;
}

static void rdt_get_config(int idx, struct rdt_resource *r)
{
	union cpuid_0x10_1_eax eax;
	union cpuid_0x10_1_edx edx;
	u32 ebx, ecx;

	cpuid_count(0x00000010, idx, &eax.full, &ebx, &ecx, &edx.full);
	r->num_closid = edx.split.cos_max + 1;
	r->cbm_len = eax.split.cbm_len + 1;
	r->max_cbm = BIT_MASK(eax.split.cbm_len + 1) - 1;
	r->capable = true;
	r->enabled = true;
}

static void rdt_get_cdp_l3_config(int type)
{
	struct rdt_resource *r_l3 = &rdt_resources_all[RDT_RESOURCE_L3];
	struct rdt_resource *r = &rdt_resources_all[type];

	r->num_closid = r_l3->num_closid / 2;
	r->cbm_len = r_l3->cbm_len;
	r->max_cbm = r_l3->max_cbm;
	r->capable = true;
	/*
	 * By default, CDP is disabled. CDP can be enabled by mount parameter
	 * "cdp" during resctrl file system mount time.
	 */
	r->enabled = false;
}

static inline bool get_rdt_resources(void)
{
	bool ret = false;

	if (cache_alloc_hsw_probe())
		return true;

	if (!boot_cpu_has(X86_FEATURE_RDT_A))
		return false;

	if (boot_cpu_has(X86_FEATURE_CAT_L3)) {
		rdt_get_config(1, &rdt_resources_all[RDT_RESOURCE_L3]);
		if (boot_cpu_has(X86_FEATURE_CDP_L3)) {
			rdt_get_cdp_l3_config(RDT_RESOURCE_L3DATA);
			rdt_get_cdp_l3_config(RDT_RESOURCE_L3CODE);
		}
		ret = true;
	}
	if (boot_cpu_has(X86_FEATURE_CAT_L2)) {
		/* CPUID 0x10.2 fields are same format at 0x10.1 */
		rdt_get_config(2, &rdt_resources_all[RDT_RESOURCE_L2]);
		ret = true;
	}

	return ret;
}

static int __init intel_rdt_late_init(void)
{
	struct rdt_resource *r;

	if (!get_rdt_resources())
		return -ENODEV;

	for_each_capable_rdt_resource(r)
		pr_info("Intel RDT %s allocation detected\n", r->name);

	return 0;
}

late_initcall(intel_rdt_late_init);
