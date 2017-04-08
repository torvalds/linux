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
#include <linux/cacheinfo.h>
#include <linux/cpuhotplug.h>

#include <asm/intel-family.h>
#include <asm/intel_rdt.h>

#define MAX_MBA_BW	100u
#define MBA_IS_LINEAR	0x4

/* Mutex to protect rdtgroup access. */
DEFINE_MUTEX(rdtgroup_mutex);

DEFINE_PER_CPU_READ_MOSTLY(int, cpu_closid);

/*
 * Used to store the max resource name width and max resource data width
 * to display the schemata in a tabular format
 */
int max_name_width, max_data_width;

static void
mba_wrmsr(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r);
static void
cat_wrmsr(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r);

#define domain_init(id) LIST_HEAD_INIT(rdt_resources_all[id].domains)

struct rdt_resource rdt_resources_all[] = {
	{
		.name			= "L3",
		.domains		= domain_init(RDT_RESOURCE_L3),
		.msr_base		= IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 3,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 1,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
	},
	{
		.name			= "L3DATA",
		.domains		= domain_init(RDT_RESOURCE_L3DATA),
		.msr_base		= IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 3,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 2,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
	},
	{
		.name			= "L3CODE",
		.domains		= domain_init(RDT_RESOURCE_L3CODE),
		.msr_base		= IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 3,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 2,
			.cbm_idx_offset	= 1,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
	},
	{
		.name			= "L2",
		.domains		= domain_init(RDT_RESOURCE_L2),
		.msr_base		= IA32_L2_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 2,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 1,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
	},
	{
		.name			= "MB",
		.domains		= domain_init(RDT_RESOURCE_MBA),
		.msr_base		= IA32_MBA_THRTL_BASE,
		.msr_update		= mba_wrmsr,
		.cache_level		= 3,
	},
};

static unsigned int cbm_idx(struct rdt_resource *r, unsigned int closid)
{
	return closid * r->cache.cbm_idx_mult + r->cache.cbm_idx_offset;
}

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
		r->default_ctrl = max_cbm;
		r->cache.cbm_len = 20;
		r->cache.min_cbm_bits = 2;
		r->capable = true;
		r->enabled = true;

		return true;
	}

	return false;
}

/*
 * rdt_get_mb_table() - get a mapping of bandwidth(b/w) percentage values
 * exposed to user interface and the h/w understandable delay values.
 *
 * The non-linear delay values have the granularity of power of two
 * and also the h/w does not guarantee a curve for configured delay
 * values vs. actual b/w enforced.
 * Hence we need a mapping that is pre calibrated so the user can
 * express the memory b/w as a percentage value.
 */
static inline bool rdt_get_mb_table(struct rdt_resource *r)
{
	/*
	 * There are no Intel SKUs as of now to support non-linear delay.
	 */
	pr_info("MBA b/w map not implemented for cpu:%d, model:%d",
		boot_cpu_data.x86, boot_cpu_data.x86_model);

	return false;
}

static bool rdt_get_mem_config(struct rdt_resource *r)
{
	union cpuid_0x10_3_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx;

	cpuid_count(0x00000010, 3, &eax.full, &ebx, &ecx, &edx.full);
	r->num_closid = edx.split.cos_max + 1;
	r->membw.max_delay = eax.split.max_delay + 1;
	r->default_ctrl = MAX_MBA_BW;
	if (ecx & MBA_IS_LINEAR) {
		r->membw.delay_linear = true;
		r->membw.min_bw = MAX_MBA_BW - r->membw.max_delay;
		r->membw.bw_gran = MAX_MBA_BW - r->membw.max_delay;
	} else {
		if (!rdt_get_mb_table(r))
			return false;
	}
	r->data_width = 3;
	rdt_get_mba_infofile(r);

	r->capable = true;
	r->enabled = true;

	return true;
}

static void rdt_get_cache_config(int idx, struct rdt_resource *r)
{
	union cpuid_0x10_1_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx;

	cpuid_count(0x00000010, idx, &eax.full, &ebx, &ecx, &edx.full);
	r->num_closid = edx.split.cos_max + 1;
	r->cache.cbm_len = eax.split.cbm_len + 1;
	r->default_ctrl = BIT_MASK(eax.split.cbm_len + 1) - 1;
	r->data_width = (r->cache.cbm_len + 3) / 4;
	rdt_get_cache_infofile(r);
	r->capable = true;
	r->enabled = true;
}

static void rdt_get_cdp_l3_config(int type)
{
	struct rdt_resource *r_l3 = &rdt_resources_all[RDT_RESOURCE_L3];
	struct rdt_resource *r = &rdt_resources_all[type];

	r->num_closid = r_l3->num_closid / 2;
	r->cache.cbm_len = r_l3->cache.cbm_len;
	r->default_ctrl = r_l3->default_ctrl;
	r->data_width = (r->cache.cbm_len + 3) / 4;
	r->capable = true;
	/*
	 * By default, CDP is disabled. CDP can be enabled by mount parameter
	 * "cdp" during resctrl file system mount time.
	 */
	r->enabled = false;
}

static int get_cache_id(int cpu, int level)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(cpu);
	int i;

	for (i = 0; i < ci->num_leaves; i++) {
		if (ci->info_list[i].level == level)
			return ci->info_list[i].id;
	}

	return -1;
}

/*
 * Map the memory b/w percentage value to delay values
 * that can be written to QOS_MSRs.
 * There are currently no SKUs which support non linear delay values.
 */
static u32 delay_bw_map(unsigned long bw, struct rdt_resource *r)
{
	if (r->membw.delay_linear)
		return MAX_MBA_BW - bw;

	pr_warn_once("Non Linear delay-bw map not supported but queried\n");
	return r->default_ctrl;
}

static void
mba_wrmsr(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r)
{
	unsigned int i;

	/*  Write the delay values for mba. */
	for (i = m->low; i < m->high; i++)
		wrmsrl(r->msr_base + i, delay_bw_map(d->ctrl_val[i], r));
}

static void
cat_wrmsr(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r)
{
	unsigned int i;

	for (i = m->low; i < m->high; i++)
		wrmsrl(r->msr_base + cbm_idx(r, i), d->ctrl_val[i]);
}

void rdt_ctrl_update(void *arg)
{
	struct msr_param *m = arg;
	struct rdt_resource *r = m->res;
	int cpu = smp_processor_id();
	struct rdt_domain *d;

	list_for_each_entry(d, &r->domains, list) {
		/* Find the domain that contains this CPU */
		if (cpumask_test_cpu(cpu, &d->cpu_mask)) {
			r->msr_update(d, m, r);
			return;
		}
	}
	pr_warn_once("cpu %d not found in any domain for resource %s\n",
		     cpu, r->name);
}

/*
 * rdt_find_domain - Find a domain in a resource that matches input resource id
 *
 * Search resource r's domain list to find the resource id. If the resource
 * id is found in a domain, return the domain. Otherwise, if requested by
 * caller, return the first domain whose id is bigger than the input id.
 * The domain list is sorted by id in ascending order.
 */
static struct rdt_domain *rdt_find_domain(struct rdt_resource *r, int id,
					  struct list_head **pos)
{
	struct rdt_domain *d;
	struct list_head *l;

	if (id < 0)
		return ERR_PTR(id);

	list_for_each(l, &r->domains) {
		d = list_entry(l, struct rdt_domain, list);
		/* When id is found, return its domain. */
		if (id == d->id)
			return d;
		/* Stop searching when finding id's position in sorted list. */
		if (id < d->id)
			break;
	}

	if (pos)
		*pos = l;

	return NULL;
}

static int domain_setup_ctrlval(struct rdt_resource *r, struct rdt_domain *d)
{
	struct msr_param m;
	u32 *dc;
	int i;

	dc = kmalloc_array(r->num_closid, sizeof(*d->ctrl_val), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	d->ctrl_val = dc;

	/*
	 * Initialize the Control MSRs to having no control.
	 * For Cache Allocation: Set all bits in cbm
	 * For Memory Allocation: Set b/w requested to 100
	 */
	for (i = 0; i < r->num_closid; i++, dc++)
		*dc = r->default_ctrl;

	m.low = 0;
	m.high = r->num_closid;
	r->msr_update(d, &m, r);
	return 0;
}

/*
 * domain_add_cpu - Add a cpu to a resource's domain list.
 *
 * If an existing domain in the resource r's domain list matches the cpu's
 * resource id, add the cpu in the domain.
 *
 * Otherwise, a new domain is allocated and inserted into the right position
 * in the domain list sorted by id in ascending order.
 *
 * The order in the domain list is visible to users when we print entries
 * in the schemata file and schemata input is validated to have the same order
 * as this list.
 */
static void domain_add_cpu(int cpu, struct rdt_resource *r)
{
	int id = get_cache_id(cpu, r->cache_level);
	struct list_head *add_pos = NULL;
	struct rdt_domain *d;

	d = rdt_find_domain(r, id, &add_pos);
	if (IS_ERR(d)) {
		pr_warn("Could't find cache id for cpu %d\n", cpu);
		return;
	}

	if (d) {
		cpumask_set_cpu(cpu, &d->cpu_mask);
		return;
	}

	d = kzalloc_node(sizeof(*d), GFP_KERNEL, cpu_to_node(cpu));
	if (!d)
		return;

	d->id = id;

	if (domain_setup_ctrlval(r, d)) {
		kfree(d);
		return;
	}

	cpumask_set_cpu(cpu, &d->cpu_mask);
	list_add_tail(&d->list, add_pos);
}

static void domain_remove_cpu(int cpu, struct rdt_resource *r)
{
	int id = get_cache_id(cpu, r->cache_level);
	struct rdt_domain *d;

	d = rdt_find_domain(r, id, NULL);
	if (IS_ERR_OR_NULL(d)) {
		pr_warn("Could't find cache id for cpu %d\n", cpu);
		return;
	}

	cpumask_clear_cpu(cpu, &d->cpu_mask);
	if (cpumask_empty(&d->cpu_mask)) {
		kfree(d->ctrl_val);
		list_del(&d->list);
		kfree(d);
	}
}

static void clear_closid(int cpu)
{
	struct intel_pqr_state *state = this_cpu_ptr(&pqr_state);

	per_cpu(cpu_closid, cpu) = 0;
	state->closid = 0;
	wrmsr(MSR_IA32_PQR_ASSOC, state->rmid, 0);
}

static int intel_rdt_online_cpu(unsigned int cpu)
{
	struct rdt_resource *r;

	mutex_lock(&rdtgroup_mutex);
	for_each_capable_rdt_resource(r)
		domain_add_cpu(cpu, r);
	/* The cpu is set in default rdtgroup after online. */
	cpumask_set_cpu(cpu, &rdtgroup_default.cpu_mask);
	clear_closid(cpu);
	mutex_unlock(&rdtgroup_mutex);

	return 0;
}

static int intel_rdt_offline_cpu(unsigned int cpu)
{
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;

	mutex_lock(&rdtgroup_mutex);
	for_each_capable_rdt_resource(r)
		domain_remove_cpu(cpu, r);
	list_for_each_entry(rdtgrp, &rdt_all_groups, rdtgroup_list) {
		if (cpumask_test_and_clear_cpu(cpu, &rdtgrp->cpu_mask))
			break;
	}
	clear_closid(cpu);
	mutex_unlock(&rdtgroup_mutex);

	return 0;
}

/*
 * Choose a width for the resource name and resource data based on the
 * resource that has widest name and cbm.
 */
static __init void rdt_init_padding(void)
{
	struct rdt_resource *r;
	int cl;

	for_each_enabled_rdt_resource(r) {
		cl = strlen(r->name);
		if (cl > max_name_width)
			max_name_width = cl;

		if (r->data_width > max_data_width)
			max_data_width = r->data_width;
	}
}

static __init bool get_rdt_resources(void)
{
	bool ret = false;

	if (cache_alloc_hsw_probe())
		return true;

	if (!boot_cpu_has(X86_FEATURE_RDT_A))
		return false;

	if (boot_cpu_has(X86_FEATURE_CAT_L3)) {
		rdt_get_cache_config(1, &rdt_resources_all[RDT_RESOURCE_L3]);
		if (boot_cpu_has(X86_FEATURE_CDP_L3)) {
			rdt_get_cdp_l3_config(RDT_RESOURCE_L3DATA);
			rdt_get_cdp_l3_config(RDT_RESOURCE_L3CODE);
		}
		ret = true;
	}
	if (boot_cpu_has(X86_FEATURE_CAT_L2)) {
		/* CPUID 0x10.2 fields are same format at 0x10.1 */
		rdt_get_cache_config(2, &rdt_resources_all[RDT_RESOURCE_L2]);
		ret = true;
	}

	if (boot_cpu_has(X86_FEATURE_MBA)) {
		if (rdt_get_mem_config(&rdt_resources_all[RDT_RESOURCE_MBA]))
			ret = true;
	}

	return ret;
}

static int __init intel_rdt_late_init(void)
{
	struct rdt_resource *r;
	int state, ret;

	if (!get_rdt_resources())
		return -ENODEV;

	rdt_init_padding();

	state = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				  "x86/rdt/cat:online:",
				  intel_rdt_online_cpu, intel_rdt_offline_cpu);
	if (state < 0)
		return state;

	ret = rdtgroup_init();
	if (ret) {
		cpuhp_remove_state(state);
		return ret;
	}

	for_each_capable_rdt_resource(r)
		pr_info("Intel RDT %s allocation detected\n", r->name);

	return 0;
}

late_initcall(intel_rdt_late_init);
