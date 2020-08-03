// SPDX-License-Identifier: GPL-2.0-only
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
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual June 2016, volume 3, section 17.17.
 */

#define pr_fmt(fmt)	"resctrl: " fmt

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/cacheinfo.h>
#include <linux/cpuhotplug.h>

#include <asm/intel-family.h>
#include <asm/resctrl.h>
#include "internal.h"

/* Mutex to protect rdtgroup access. */
DEFINE_MUTEX(rdtgroup_mutex);

/*
 * The cached resctrl_pqr_state is strictly per CPU and can never be
 * updated from a remote CPU. Functions which modify the state
 * are called with interrupts disabled and no preemption, which
 * is sufficient for the protection.
 */
DEFINE_PER_CPU(struct resctrl_pqr_state, pqr_state);

/*
 * Used to store the max resource name width and max resource data width
 * to display the schemata in a tabular format
 */
int max_name_width, max_data_width;

/*
 * Global boolean for rdt_alloc which is true if any
 * resource allocation is enabled.
 */
bool rdt_alloc_capable;

static void
mba_wrmsr_intel(struct rdt_domain *d, struct msr_param *m,
		struct rdt_resource *r);
static void
cat_wrmsr(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r);
static void
mba_wrmsr_amd(struct rdt_domain *d, struct msr_param *m,
	      struct rdt_resource *r);

#define domain_init(id) LIST_HEAD_INIT(rdt_resources_all[id].domains)

struct rdt_resource rdt_resources_all[] = {
	[RDT_RESOURCE_L3] =
	{
		.rid			= RDT_RESOURCE_L3,
		.name			= "L3",
		.domains		= domain_init(RDT_RESOURCE_L3),
		.msr_base		= MSR_IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 3,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 1,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
		.fflags			= RFTYPE_RES_CACHE,
	},
	[RDT_RESOURCE_L3DATA] =
	{
		.rid			= RDT_RESOURCE_L3DATA,
		.name			= "L3DATA",
		.domains		= domain_init(RDT_RESOURCE_L3DATA),
		.msr_base		= MSR_IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 3,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 2,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
		.fflags			= RFTYPE_RES_CACHE,
	},
	[RDT_RESOURCE_L3CODE] =
	{
		.rid			= RDT_RESOURCE_L3CODE,
		.name			= "L3CODE",
		.domains		= domain_init(RDT_RESOURCE_L3CODE),
		.msr_base		= MSR_IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 3,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 2,
			.cbm_idx_offset	= 1,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
		.fflags			= RFTYPE_RES_CACHE,
	},
	[RDT_RESOURCE_L2] =
	{
		.rid			= RDT_RESOURCE_L2,
		.name			= "L2",
		.domains		= domain_init(RDT_RESOURCE_L2),
		.msr_base		= MSR_IA32_L2_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 2,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 1,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
		.fflags			= RFTYPE_RES_CACHE,
	},
	[RDT_RESOURCE_L2DATA] =
	{
		.rid			= RDT_RESOURCE_L2DATA,
		.name			= "L2DATA",
		.domains		= domain_init(RDT_RESOURCE_L2DATA),
		.msr_base		= MSR_IA32_L2_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 2,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 2,
			.cbm_idx_offset	= 0,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
		.fflags			= RFTYPE_RES_CACHE,
	},
	[RDT_RESOURCE_L2CODE] =
	{
		.rid			= RDT_RESOURCE_L2CODE,
		.name			= "L2CODE",
		.domains		= domain_init(RDT_RESOURCE_L2CODE),
		.msr_base		= MSR_IA32_L2_CBM_BASE,
		.msr_update		= cat_wrmsr,
		.cache_level		= 2,
		.cache = {
			.min_cbm_bits	= 1,
			.cbm_idx_mult	= 2,
			.cbm_idx_offset	= 1,
		},
		.parse_ctrlval		= parse_cbm,
		.format_str		= "%d=%0*x",
		.fflags			= RFTYPE_RES_CACHE,
	},
	[RDT_RESOURCE_MBA] =
	{
		.rid			= RDT_RESOURCE_MBA,
		.name			= "MB",
		.domains		= domain_init(RDT_RESOURCE_MBA),
		.cache_level		= 3,
		.format_str		= "%d=%*u",
		.fflags			= RFTYPE_RES_MB,
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
static inline void cache_alloc_hsw_probe(void)
{
	struct rdt_resource *r  = &rdt_resources_all[RDT_RESOURCE_L3];
	u32 l, h, max_cbm = BIT_MASK(20) - 1;

	if (wrmsr_safe(MSR_IA32_L3_CBM_BASE, max_cbm, 0))
		return;

	rdmsr(MSR_IA32_L3_CBM_BASE, l, h);

	/* If all the bits were set in MSR, return success */
	if (l != max_cbm)
		return;

	r->num_closid = 4;
	r->default_ctrl = max_cbm;
	r->cache.cbm_len = 20;
	r->cache.shareable_bits = 0xc0000;
	r->cache.min_cbm_bits = 2;
	r->alloc_capable = true;
	r->alloc_enabled = true;

	rdt_alloc_capable = true;
}

bool is_mba_sc(struct rdt_resource *r)
{
	if (!r)
		return rdt_resources_all[RDT_RESOURCE_MBA].membw.mba_sc;

	return r->membw.mba_sc;
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

static bool __get_mem_config_intel(struct rdt_resource *r)
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

	r->alloc_capable = true;
	r->alloc_enabled = true;

	return true;
}

static bool __rdt_get_mem_config_amd(struct rdt_resource *r)
{
	union cpuid_0x10_3_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx;

	cpuid_count(0x80000020, 1, &eax.full, &ebx, &ecx, &edx.full);
	r->num_closid = edx.split.cos_max + 1;
	r->default_ctrl = MAX_MBA_BW_AMD;

	/* AMD does not use delay */
	r->membw.delay_linear = false;

	r->membw.min_bw = 0;
	r->membw.bw_gran = 1;
	/* Max value is 2048, Data width should be 4 in decimal */
	r->data_width = 4;

	r->alloc_capable = true;
	r->alloc_enabled = true;

	return true;
}

static void rdt_get_cache_alloc_cfg(int idx, struct rdt_resource *r)
{
	union cpuid_0x10_1_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx;

	cpuid_count(0x00000010, idx, &eax.full, &ebx, &ecx, &edx.full);
	r->num_closid = edx.split.cos_max + 1;
	r->cache.cbm_len = eax.split.cbm_len + 1;
	r->default_ctrl = BIT_MASK(eax.split.cbm_len + 1) - 1;
	r->cache.shareable_bits = ebx & r->default_ctrl;
	r->data_width = (r->cache.cbm_len + 3) / 4;
	r->alloc_capable = true;
	r->alloc_enabled = true;
}

static void rdt_get_cdp_config(int level, int type)
{
	struct rdt_resource *r_l = &rdt_resources_all[level];
	struct rdt_resource *r = &rdt_resources_all[type];

	r->num_closid = r_l->num_closid / 2;
	r->cache.cbm_len = r_l->cache.cbm_len;
	r->default_ctrl = r_l->default_ctrl;
	r->cache.shareable_bits = r_l->cache.shareable_bits;
	r->data_width = (r->cache.cbm_len + 3) / 4;
	r->alloc_capable = true;
	/*
	 * By default, CDP is disabled. CDP can be enabled by mount parameter
	 * "cdp" during resctrl file system mount time.
	 */
	r->alloc_enabled = false;
}

static void rdt_get_cdp_l3_config(void)
{
	rdt_get_cdp_config(RDT_RESOURCE_L3, RDT_RESOURCE_L3DATA);
	rdt_get_cdp_config(RDT_RESOURCE_L3, RDT_RESOURCE_L3CODE);
}

static void rdt_get_cdp_l2_config(void)
{
	rdt_get_cdp_config(RDT_RESOURCE_L2, RDT_RESOURCE_L2DATA);
	rdt_get_cdp_config(RDT_RESOURCE_L2, RDT_RESOURCE_L2CODE);
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

static void
mba_wrmsr_amd(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r)
{
	unsigned int i;

	for (i = m->low; i < m->high; i++)
		wrmsrl(r->msr_base + i, d->ctrl_val[i]);
}

/*
 * Map the memory b/w percentage value to delay values
 * that can be written to QOS_MSRs.
 * There are currently no SKUs which support non linear delay values.
 */
u32 delay_bw_map(unsigned long bw, struct rdt_resource *r)
{
	if (r->membw.delay_linear)
		return MAX_MBA_BW - bw;

	pr_warn_once("Non Linear delay-bw map not supported but queried\n");
	return r->default_ctrl;
}

static void
mba_wrmsr_intel(struct rdt_domain *d, struct msr_param *m,
		struct rdt_resource *r)
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

struct rdt_domain *get_domain_from_cpu(int cpu, struct rdt_resource *r)
{
	struct rdt_domain *d;

	list_for_each_entry(d, &r->domains, list) {
		/* Find the domain that contains this CPU */
		if (cpumask_test_cpu(cpu, &d->cpu_mask))
			return d;
	}

	return NULL;
}

void rdt_ctrl_update(void *arg)
{
	struct msr_param *m = arg;
	struct rdt_resource *r = m->res;
	int cpu = smp_processor_id();
	struct rdt_domain *d;

	d = get_domain_from_cpu(cpu, r);
	if (d) {
		r->msr_update(d, m, r);
		return;
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
struct rdt_domain *rdt_find_domain(struct rdt_resource *r, int id,
				   struct list_head **pos)
{
	struct rdt_domain *d;
	struct list_head *l;

	if (id < 0)
		return ERR_PTR(-ENODEV);

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

void setup_default_ctrlval(struct rdt_resource *r, u32 *dc, u32 *dm)
{
	int i;

	/*
	 * Initialize the Control MSRs to having no control.
	 * For Cache Allocation: Set all bits in cbm
	 * For Memory Allocation: Set b/w requested to 100%
	 * and the bandwidth in MBps to U32_MAX
	 */
	for (i = 0; i < r->num_closid; i++, dc++, dm++) {
		*dc = r->default_ctrl;
		*dm = MBA_MAX_MBPS;
	}
}

static int domain_setup_ctrlval(struct rdt_resource *r, struct rdt_domain *d)
{
	struct msr_param m;
	u32 *dc, *dm;

	dc = kmalloc_array(r->num_closid, sizeof(*d->ctrl_val), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	dm = kmalloc_array(r->num_closid, sizeof(*d->mbps_val), GFP_KERNEL);
	if (!dm) {
		kfree(dc);
		return -ENOMEM;
	}

	d->ctrl_val = dc;
	d->mbps_val = dm;
	setup_default_ctrlval(r, dc, dm);

	m.low = 0;
	m.high = r->num_closid;
	r->msr_update(d, &m, r);
	return 0;
}

static int domain_setup_mon_state(struct rdt_resource *r, struct rdt_domain *d)
{
	size_t tsize;

	if (is_llc_occupancy_enabled()) {
		d->rmid_busy_llc = bitmap_zalloc(r->num_rmid, GFP_KERNEL);
		if (!d->rmid_busy_llc)
			return -ENOMEM;
		INIT_DELAYED_WORK(&d->cqm_limbo, cqm_handle_limbo);
	}
	if (is_mbm_total_enabled()) {
		tsize = sizeof(*d->mbm_total);
		d->mbm_total = kcalloc(r->num_rmid, tsize, GFP_KERNEL);
		if (!d->mbm_total) {
			bitmap_free(d->rmid_busy_llc);
			return -ENOMEM;
		}
	}
	if (is_mbm_local_enabled()) {
		tsize = sizeof(*d->mbm_local);
		d->mbm_local = kcalloc(r->num_rmid, tsize, GFP_KERNEL);
		if (!d->mbm_local) {
			bitmap_free(d->rmid_busy_llc);
			kfree(d->mbm_total);
			return -ENOMEM;
		}
	}

	if (is_mbm_enabled()) {
		INIT_DELAYED_WORK(&d->mbm_over, mbm_handle_overflow);
		mbm_setup_overflow_handler(d, MBM_OVERFLOW_INTERVAL);
	}

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
	cpumask_set_cpu(cpu, &d->cpu_mask);

	rdt_domain_reconfigure_cdp(r);

	if (r->alloc_capable && domain_setup_ctrlval(r, d)) {
		kfree(d);
		return;
	}

	if (r->mon_capable && domain_setup_mon_state(r, d)) {
		kfree(d);
		return;
	}

	list_add_tail(&d->list, add_pos);

	/*
	 * If resctrl is mounted, add
	 * per domain monitor data directories.
	 */
	if (static_branch_unlikely(&rdt_mon_enable_key))
		mkdir_mondata_subdir_allrdtgrp(r, d);
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
		/*
		 * If resctrl is mounted, remove all the
		 * per domain monitor data directories.
		 */
		if (static_branch_unlikely(&rdt_mon_enable_key))
			rmdir_mondata_subdir_allrdtgrp(r, d->id);
		list_del(&d->list);
		if (r->mon_capable && is_mbm_enabled())
			cancel_delayed_work(&d->mbm_over);
		if (is_llc_occupancy_enabled() &&  has_busy_rmid(r, d)) {
			/*
			 * When a package is going down, forcefully
			 * decrement rmid->ebusy. There is no way to know
			 * that the L3 was flushed and hence may lead to
			 * incorrect counts in rare scenarios, but leaving
			 * the RMID as busy creates RMID leaks if the
			 * package never comes back.
			 */
			__check_limbo(d, true);
			cancel_delayed_work(&d->cqm_limbo);
		}

		/*
		 * rdt_domain "d" is going to be freed below, so clear
		 * its pointer from pseudo_lock_region struct.
		 */
		if (d->plr)
			d->plr->d = NULL;

		kfree(d->ctrl_val);
		kfree(d->mbps_val);
		bitmap_free(d->rmid_busy_llc);
		kfree(d->mbm_total);
		kfree(d->mbm_local);
		kfree(d);
		return;
	}

	if (r == &rdt_resources_all[RDT_RESOURCE_L3]) {
		if (is_mbm_enabled() && cpu == d->mbm_work_cpu) {
			cancel_delayed_work(&d->mbm_over);
			mbm_setup_overflow_handler(d, 0);
		}
		if (is_llc_occupancy_enabled() && cpu == d->cqm_work_cpu &&
		    has_busy_rmid(r, d)) {
			cancel_delayed_work(&d->cqm_limbo);
			cqm_setup_limbo_handler(d, 0);
		}
	}
}

static void clear_closid_rmid(int cpu)
{
	struct resctrl_pqr_state *state = this_cpu_ptr(&pqr_state);

	state->default_closid = 0;
	state->default_rmid = 0;
	state->cur_closid = 0;
	state->cur_rmid = 0;
	wrmsr(IA32_PQR_ASSOC, 0, 0);
}

static int resctrl_online_cpu(unsigned int cpu)
{
	struct rdt_resource *r;

	mutex_lock(&rdtgroup_mutex);
	for_each_capable_rdt_resource(r)
		domain_add_cpu(cpu, r);
	/* The cpu is set in default rdtgroup after online. */
	cpumask_set_cpu(cpu, &rdtgroup_default.cpu_mask);
	clear_closid_rmid(cpu);
	mutex_unlock(&rdtgroup_mutex);

	return 0;
}

static void clear_childcpus(struct rdtgroup *r, unsigned int cpu)
{
	struct rdtgroup *cr;

	list_for_each_entry(cr, &r->mon.crdtgrp_list, mon.crdtgrp_list) {
		if (cpumask_test_and_clear_cpu(cpu, &cr->cpu_mask)) {
			break;
		}
	}
}

static int resctrl_offline_cpu(unsigned int cpu)
{
	struct rdtgroup *rdtgrp;
	struct rdt_resource *r;

	mutex_lock(&rdtgroup_mutex);
	for_each_capable_rdt_resource(r)
		domain_remove_cpu(cpu, r);
	list_for_each_entry(rdtgrp, &rdt_all_groups, rdtgroup_list) {
		if (cpumask_test_and_clear_cpu(cpu, &rdtgrp->cpu_mask)) {
			clear_childcpus(rdtgrp, cpu);
			break;
		}
	}
	clear_closid_rmid(cpu);
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

	for_each_alloc_capable_rdt_resource(r) {
		cl = strlen(r->name);
		if (cl > max_name_width)
			max_name_width = cl;

		if (r->data_width > max_data_width)
			max_data_width = r->data_width;
	}
}

enum {
	RDT_FLAG_CMT,
	RDT_FLAG_MBM_TOTAL,
	RDT_FLAG_MBM_LOCAL,
	RDT_FLAG_L3_CAT,
	RDT_FLAG_L3_CDP,
	RDT_FLAG_L2_CAT,
	RDT_FLAG_L2_CDP,
	RDT_FLAG_MBA,
};

#define RDT_OPT(idx, n, f)	\
[idx] = {			\
	.name = n,		\
	.flag = f		\
}

struct rdt_options {
	char	*name;
	int	flag;
	bool	force_off, force_on;
};

static struct rdt_options rdt_options[]  __initdata = {
	RDT_OPT(RDT_FLAG_CMT,	    "cmt",	X86_FEATURE_CQM_OCCUP_LLC),
	RDT_OPT(RDT_FLAG_MBM_TOTAL, "mbmtotal", X86_FEATURE_CQM_MBM_TOTAL),
	RDT_OPT(RDT_FLAG_MBM_LOCAL, "mbmlocal", X86_FEATURE_CQM_MBM_LOCAL),
	RDT_OPT(RDT_FLAG_L3_CAT,    "l3cat",	X86_FEATURE_CAT_L3),
	RDT_OPT(RDT_FLAG_L3_CDP,    "l3cdp",	X86_FEATURE_CDP_L3),
	RDT_OPT(RDT_FLAG_L2_CAT,    "l2cat",	X86_FEATURE_CAT_L2),
	RDT_OPT(RDT_FLAG_L2_CDP,    "l2cdp",	X86_FEATURE_CDP_L2),
	RDT_OPT(RDT_FLAG_MBA,	    "mba",	X86_FEATURE_MBA),
};
#define NUM_RDT_OPTIONS ARRAY_SIZE(rdt_options)

static int __init set_rdt_options(char *str)
{
	struct rdt_options *o;
	bool force_off;
	char *tok;

	if (*str == '=')
		str++;
	while ((tok = strsep(&str, ",")) != NULL) {
		force_off = *tok == '!';
		if (force_off)
			tok++;
		for (o = rdt_options; o < &rdt_options[NUM_RDT_OPTIONS]; o++) {
			if (strcmp(tok, o->name) == 0) {
				if (force_off)
					o->force_off = true;
				else
					o->force_on = true;
				break;
			}
		}
	}
	return 1;
}
__setup("rdt", set_rdt_options);

static bool __init rdt_cpu_has(int flag)
{
	bool ret = boot_cpu_has(flag);
	struct rdt_options *o;

	if (!ret)
		return ret;

	for (o = rdt_options; o < &rdt_options[NUM_RDT_OPTIONS]; o++) {
		if (flag == o->flag) {
			if (o->force_off)
				ret = false;
			if (o->force_on)
				ret = true;
			break;
		}
	}
	return ret;
}

static __init bool get_mem_config(void)
{
	if (!rdt_cpu_has(X86_FEATURE_MBA))
		return false;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		return __get_mem_config_intel(&rdt_resources_all[RDT_RESOURCE_MBA]);
	else if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return __rdt_get_mem_config_amd(&rdt_resources_all[RDT_RESOURCE_MBA]);

	return false;
}

static __init bool get_rdt_alloc_resources(void)
{
	bool ret = false;

	if (rdt_alloc_capable)
		return true;

	if (!boot_cpu_has(X86_FEATURE_RDT_A))
		return false;

	if (rdt_cpu_has(X86_FEATURE_CAT_L3)) {
		rdt_get_cache_alloc_cfg(1, &rdt_resources_all[RDT_RESOURCE_L3]);
		if (rdt_cpu_has(X86_FEATURE_CDP_L3))
			rdt_get_cdp_l3_config();
		ret = true;
	}
	if (rdt_cpu_has(X86_FEATURE_CAT_L2)) {
		/* CPUID 0x10.2 fields are same format at 0x10.1 */
		rdt_get_cache_alloc_cfg(2, &rdt_resources_all[RDT_RESOURCE_L2]);
		if (rdt_cpu_has(X86_FEATURE_CDP_L2))
			rdt_get_cdp_l2_config();
		ret = true;
	}

	if (get_mem_config())
		ret = true;

	return ret;
}

static __init bool get_rdt_mon_resources(void)
{
	if (rdt_cpu_has(X86_FEATURE_CQM_OCCUP_LLC))
		rdt_mon_features |= (1 << QOS_L3_OCCUP_EVENT_ID);
	if (rdt_cpu_has(X86_FEATURE_CQM_MBM_TOTAL))
		rdt_mon_features |= (1 << QOS_L3_MBM_TOTAL_EVENT_ID);
	if (rdt_cpu_has(X86_FEATURE_CQM_MBM_LOCAL))
		rdt_mon_features |= (1 << QOS_L3_MBM_LOCAL_EVENT_ID);

	if (!rdt_mon_features)
		return false;

	return !rdt_get_mon_l3_config(&rdt_resources_all[RDT_RESOURCE_L3]);
}

static __init void __check_quirks_intel(void)
{
	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_HASWELL_X:
		if (!rdt_options[RDT_FLAG_L3_CAT].force_off)
			cache_alloc_hsw_probe();
		break;
	case INTEL_FAM6_SKYLAKE_X:
		if (boot_cpu_data.x86_stepping <= 4)
			set_rdt_options("!cmt,!mbmtotal,!mbmlocal,!l3cat");
		else
			set_rdt_options("!l3cat");
	}
}

static __init void check_quirks(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		__check_quirks_intel();
}

static __init bool get_rdt_resources(void)
{
	rdt_alloc_capable = get_rdt_alloc_resources();
	rdt_mon_capable = get_rdt_mon_resources();

	return (rdt_mon_capable || rdt_alloc_capable);
}

static __init void rdt_init_res_defs_intel(void)
{
	struct rdt_resource *r;

	for_each_rdt_resource(r) {
		if (r->rid == RDT_RESOURCE_L3 ||
		    r->rid == RDT_RESOURCE_L3DATA ||
		    r->rid == RDT_RESOURCE_L3CODE ||
		    r->rid == RDT_RESOURCE_L2 ||
		    r->rid == RDT_RESOURCE_L2DATA ||
		    r->rid == RDT_RESOURCE_L2CODE)
			r->cbm_validate = cbm_validate_intel;
		else if (r->rid == RDT_RESOURCE_MBA) {
			r->msr_base = MSR_IA32_MBA_THRTL_BASE;
			r->msr_update = mba_wrmsr_intel;
			r->parse_ctrlval = parse_bw_intel;
		}
	}
}

static __init void rdt_init_res_defs_amd(void)
{
	struct rdt_resource *r;

	for_each_rdt_resource(r) {
		if (r->rid == RDT_RESOURCE_L3 ||
		    r->rid == RDT_RESOURCE_L3DATA ||
		    r->rid == RDT_RESOURCE_L3CODE ||
		    r->rid == RDT_RESOURCE_L2 ||
		    r->rid == RDT_RESOURCE_L2DATA ||
		    r->rid == RDT_RESOURCE_L2CODE)
			r->cbm_validate = cbm_validate_amd;
		else if (r->rid == RDT_RESOURCE_MBA) {
			r->msr_base = MSR_IA32_MBA_BW_BASE;
			r->msr_update = mba_wrmsr_amd;
			r->parse_ctrlval = parse_bw_amd;
		}
	}
}

static __init void rdt_init_res_defs(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		rdt_init_res_defs_intel();
	else if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		rdt_init_res_defs_amd();
}

static enum cpuhp_state rdt_online;

/* Runs once on the BSP during boot. */
void resctrl_cpu_detect(struct cpuinfo_x86 *c)
{
	if (!cpu_has(c, X86_FEATURE_CQM_LLC)) {
		c->x86_cache_max_rmid  = -1;
		c->x86_cache_occ_scale = -1;
		c->x86_cache_mbm_width_offset = -1;
		return;
	}

	/* will be overridden if occupancy monitoring exists */
	c->x86_cache_max_rmid = cpuid_ebx(0xf);

	if (cpu_has(c, X86_FEATURE_CQM_OCCUP_LLC) ||
	    cpu_has(c, X86_FEATURE_CQM_MBM_TOTAL) ||
	    cpu_has(c, X86_FEATURE_CQM_MBM_LOCAL)) {
		u32 eax, ebx, ecx, edx;

		/* QoS sub-leaf, EAX=0Fh, ECX=1 */
		cpuid_count(0xf, 1, &eax, &ebx, &ecx, &edx);

		c->x86_cache_max_rmid  = ecx;
		c->x86_cache_occ_scale = ebx;
		if (c->x86_vendor == X86_VENDOR_INTEL)
			c->x86_cache_mbm_width_offset = eax & 0xff;
		else
			c->x86_cache_mbm_width_offset = -1;
	}
}

static int __init resctrl_late_init(void)
{
	struct rdt_resource *r;
	int state, ret;

	/*
	 * Initialize functions(or definitions) that are different
	 * between vendors here.
	 */
	rdt_init_res_defs();

	check_quirks();

	if (!get_rdt_resources())
		return -ENODEV;

	rdt_init_padding();

	state = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				  "x86/resctrl/cat:online:",
				  resctrl_online_cpu, resctrl_offline_cpu);
	if (state < 0)
		return state;

	ret = rdtgroup_init();
	if (ret) {
		cpuhp_remove_state(state);
		return ret;
	}
	rdt_online = state;

	for_each_alloc_capable_rdt_resource(r)
		pr_info("%s allocation detected\n", r->name);

	for_each_mon_capable_rdt_resource(r)
		pr_info("%s monitoring detected\n", r->name);

	return 0;
}

late_initcall(resctrl_late_init);

static void __exit resctrl_exit(void)
{
	cpuhp_remove_state(rdt_online);
	rdtgroup_exit();
}

__exitcall(resctrl_exit);
