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

#define domain_init(id) LIST_HEAD_INIT(rdt_resources_all[id].r_resctrl.domains)

struct rdt_hw_resource rdt_resources_all[] = {
	[RDT_RESOURCE_L3] =
	{
		.r_resctrl = {
			.rid			= RDT_RESOURCE_L3,
			.name			= "L3",
			.cache_level		= 3,
			.domains		= domain_init(RDT_RESOURCE_L3),
			.parse_ctrlval		= parse_cbm,
			.format_str		= "%d=%0*x",
			.fflags			= RFTYPE_RES_CACHE,
		},
		.msr_base		= MSR_IA32_L3_CBM_BASE,
		.msr_update		= cat_wrmsr,
	},
	[RDT_RESOURCE_L2] =
	{
		.r_resctrl = {
			.rid			= RDT_RESOURCE_L2,
			.name			= "L2",
			.cache_level		= 2,
			.domains		= domain_init(RDT_RESOURCE_L2),
			.parse_ctrlval		= parse_cbm,
			.format_str		= "%d=%0*x",
			.fflags			= RFTYPE_RES_CACHE,
		},
		.msr_base		= MSR_IA32_L2_CBM_BASE,
		.msr_update		= cat_wrmsr,
	},
	[RDT_RESOURCE_MBA] =
	{
		.r_resctrl = {
			.rid			= RDT_RESOURCE_MBA,
			.name			= "MB",
			.cache_level		= 3,
			.domains		= domain_init(RDT_RESOURCE_MBA),
			.parse_ctrlval		= parse_bw,
			.format_str		= "%d=%*u",
			.fflags			= RFTYPE_RES_MB,
		},
	},
	[RDT_RESOURCE_SMBA] =
	{
		.r_resctrl = {
			.rid			= RDT_RESOURCE_SMBA,
			.name			= "SMBA",
			.cache_level		= 3,
			.domains		= domain_init(RDT_RESOURCE_SMBA),
			.parse_ctrlval		= parse_bw,
			.format_str		= "%d=%*u",
			.fflags			= RFTYPE_RES_MB,
		},
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
 * Probe by trying to write the first of the L3 cache mask registers
 * and checking that the bits stick. Max CLOSids is always 4 and max cbm length
 * is always 20 on hsw server parts. The minimum cache bitmask length
 * allowed for HSW server is always 2 bits. Hardcode all of them.
 */
static inline void cache_alloc_hsw_probe(void)
{
	struct rdt_hw_resource *hw_res = &rdt_resources_all[RDT_RESOURCE_L3];
	struct rdt_resource *r  = &hw_res->r_resctrl;
	u32 l, h, max_cbm = BIT_MASK(20) - 1;

	if (wrmsr_safe(MSR_IA32_L3_CBM_BASE, max_cbm, 0))
		return;

	rdmsr(MSR_IA32_L3_CBM_BASE, l, h);

	/* If all the bits were set in MSR, return success */
	if (l != max_cbm)
		return;

	hw_res->num_closid = 4;
	r->default_ctrl = max_cbm;
	r->cache.cbm_len = 20;
	r->cache.shareable_bits = 0xc0000;
	r->cache.min_cbm_bits = 2;
	r->alloc_capable = true;

	rdt_alloc_capable = true;
}

bool is_mba_sc(struct rdt_resource *r)
{
	if (!r)
		return rdt_resources_all[RDT_RESOURCE_MBA].r_resctrl.membw.mba_sc;

	/*
	 * The software controller support is only applicable to MBA resource.
	 * Make sure to check for resource type.
	 */
	if (r->rid != RDT_RESOURCE_MBA)
		return false;

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
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	union cpuid_0x10_3_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx, max_delay;

	cpuid_count(0x00000010, 3, &eax.full, &ebx, &ecx, &edx.full);
	hw_res->num_closid = edx.split.cos_max + 1;
	max_delay = eax.split.max_delay + 1;
	r->default_ctrl = MAX_MBA_BW;
	r->membw.arch_needs_linear = true;
	if (ecx & MBA_IS_LINEAR) {
		r->membw.delay_linear = true;
		r->membw.min_bw = MAX_MBA_BW - max_delay;
		r->membw.bw_gran = MAX_MBA_BW - max_delay;
	} else {
		if (!rdt_get_mb_table(r))
			return false;
		r->membw.arch_needs_linear = false;
	}
	r->data_width = 3;

	if (boot_cpu_has(X86_FEATURE_PER_THREAD_MBA))
		r->membw.throttle_mode = THREAD_THROTTLE_PER_THREAD;
	else
		r->membw.throttle_mode = THREAD_THROTTLE_MAX;
	thread_throttle_mode_init();

	r->alloc_capable = true;

	return true;
}

static bool __rdt_get_mem_config_amd(struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	union cpuid_0x10_3_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx, subleaf;

	/*
	 * Query CPUID_Fn80000020_EDX_x01 for MBA and
	 * CPUID_Fn80000020_EDX_x02 for SMBA
	 */
	subleaf = (r->rid == RDT_RESOURCE_SMBA) ? 2 :  1;

	cpuid_count(0x80000020, subleaf, &eax.full, &ebx, &ecx, &edx.full);
	hw_res->num_closid = edx.split.cos_max + 1;
	r->default_ctrl = MAX_MBA_BW_AMD;

	/* AMD does not use delay */
	r->membw.delay_linear = false;
	r->membw.arch_needs_linear = false;

	/*
	 * AMD does not use memory delay throttle model to control
	 * the allocation like Intel does.
	 */
	r->membw.throttle_mode = THREAD_THROTTLE_UNDEFINED;
	r->membw.min_bw = 0;
	r->membw.bw_gran = 1;
	/* Max value is 2048, Data width should be 4 in decimal */
	r->data_width = 4;

	r->alloc_capable = true;

	return true;
}

static void rdt_get_cache_alloc_cfg(int idx, struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	union cpuid_0x10_1_eax eax;
	union cpuid_0x10_x_edx edx;
	u32 ebx, ecx;

	cpuid_count(0x00000010, idx, &eax.full, &ebx, &ecx, &edx.full);
	hw_res->num_closid = edx.split.cos_max + 1;
	r->cache.cbm_len = eax.split.cbm_len + 1;
	r->default_ctrl = BIT_MASK(eax.split.cbm_len + 1) - 1;
	r->cache.shareable_bits = ebx & r->default_ctrl;
	r->data_width = (r->cache.cbm_len + 3) / 4;
	r->alloc_capable = true;
}

static void rdt_get_cdp_config(int level)
{
	/*
	 * By default, CDP is disabled. CDP can be enabled by mount parameter
	 * "cdp" during resctrl file system mount time.
	 */
	rdt_resources_all[level].cdp_enabled = false;
	rdt_resources_all[level].r_resctrl.cdp_capable = true;
}

static void rdt_get_cdp_l3_config(void)
{
	rdt_get_cdp_config(RDT_RESOURCE_L3);
}

static void rdt_get_cdp_l2_config(void)
{
	rdt_get_cdp_config(RDT_RESOURCE_L2);
}

static void
mba_wrmsr_amd(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r)
{
	unsigned int i;
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	for (i = m->low; i < m->high; i++)
		wrmsrl(hw_res->msr_base + i, hw_dom->ctrl_val[i]);
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
mba_wrmsr_intel(struct rdt_domain *d, struct msr_param *m,
		struct rdt_resource *r)
{
	unsigned int i;
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	/*  Write the delay values for mba. */
	for (i = m->low; i < m->high; i++)
		wrmsrl(hw_res->msr_base + i, delay_bw_map(hw_dom->ctrl_val[i], r));
}

static void
cat_wrmsr(struct rdt_domain *d, struct msr_param *m, struct rdt_resource *r)
{
	unsigned int i;
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	for (i = m->low; i < m->high; i++)
		wrmsrl(hw_res->msr_base + i, hw_dom->ctrl_val[i]);
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

u32 resctrl_arch_get_num_closid(struct rdt_resource *r)
{
	return resctrl_to_arch_res(r)->num_closid;
}

void rdt_ctrl_update(void *arg)
{
	struct msr_param *m = arg;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(m->res);
	struct rdt_resource *r = m->res;
	int cpu = smp_processor_id();
	struct rdt_domain *d;

	d = get_domain_from_cpu(cpu, r);
	if (d) {
		hw_res->msr_update(d, m, r);
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

static void setup_default_ctrlval(struct rdt_resource *r, u32 *dc)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	int i;

	/*
	 * Initialize the Control MSRs to having no control.
	 * For Cache Allocation: Set all bits in cbm
	 * For Memory Allocation: Set b/w requested to 100%
	 */
	for (i = 0; i < hw_res->num_closid; i++, dc++)
		*dc = r->default_ctrl;
}

static void domain_free(struct rdt_hw_domain *hw_dom)
{
	kfree(hw_dom->arch_mbm_total);
	kfree(hw_dom->arch_mbm_local);
	kfree(hw_dom->ctrl_val);
	kfree(hw_dom);
}

static int domain_setup_ctrlval(struct rdt_resource *r, struct rdt_domain *d)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	struct msr_param m;
	u32 *dc;

	dc = kmalloc_array(hw_res->num_closid, sizeof(*hw_dom->ctrl_val),
			   GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	hw_dom->ctrl_val = dc;
	setup_default_ctrlval(r, dc);

	m.low = 0;
	m.high = hw_res->num_closid;
	hw_res->msr_update(d, &m, r);
	return 0;
}

/**
 * arch_domain_mbm_alloc() - Allocate arch private storage for the MBM counters
 * @num_rmid:	The size of the MBM counter array
 * @hw_dom:	The domain that owns the allocated arrays
 */
static int arch_domain_mbm_alloc(u32 num_rmid, struct rdt_hw_domain *hw_dom)
{
	size_t tsize;

	if (is_mbm_total_enabled()) {
		tsize = sizeof(*hw_dom->arch_mbm_total);
		hw_dom->arch_mbm_total = kcalloc(num_rmid, tsize, GFP_KERNEL);
		if (!hw_dom->arch_mbm_total)
			return -ENOMEM;
	}
	if (is_mbm_local_enabled()) {
		tsize = sizeof(*hw_dom->arch_mbm_local);
		hw_dom->arch_mbm_local = kcalloc(num_rmid, tsize, GFP_KERNEL);
		if (!hw_dom->arch_mbm_local) {
			kfree(hw_dom->arch_mbm_total);
			hw_dom->arch_mbm_total = NULL;
			return -ENOMEM;
		}
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
	int id = get_cpu_cacheinfo_id(cpu, r->cache_level);
	struct list_head *add_pos = NULL;
	struct rdt_hw_domain *hw_dom;
	struct rdt_domain *d;
	int err;

	d = rdt_find_domain(r, id, &add_pos);
	if (IS_ERR(d)) {
		pr_warn("Couldn't find cache id for CPU %d\n", cpu);
		return;
	}

	if (d) {
		cpumask_set_cpu(cpu, &d->cpu_mask);
		if (r->cache.arch_has_per_cpu_cfg)
			rdt_domain_reconfigure_cdp(r);
		return;
	}

	hw_dom = kzalloc_node(sizeof(*hw_dom), GFP_KERNEL, cpu_to_node(cpu));
	if (!hw_dom)
		return;

	d = &hw_dom->d_resctrl;
	d->id = id;
	cpumask_set_cpu(cpu, &d->cpu_mask);

	rdt_domain_reconfigure_cdp(r);

	if (r->alloc_capable && domain_setup_ctrlval(r, d)) {
		domain_free(hw_dom);
		return;
	}

	if (r->mon_capable && arch_domain_mbm_alloc(r->num_rmid, hw_dom)) {
		domain_free(hw_dom);
		return;
	}

	list_add_tail(&d->list, add_pos);

	err = resctrl_online_domain(r, d);
	if (err) {
		list_del(&d->list);
		domain_free(hw_dom);
	}
}

static void domain_remove_cpu(int cpu, struct rdt_resource *r)
{
	int id = get_cpu_cacheinfo_id(cpu, r->cache_level);
	struct rdt_hw_domain *hw_dom;
	struct rdt_domain *d;

	d = rdt_find_domain(r, id, NULL);
	if (IS_ERR_OR_NULL(d)) {
		pr_warn("Couldn't find cache id for CPU %d\n", cpu);
		return;
	}
	hw_dom = resctrl_to_arch_dom(d);

	cpumask_clear_cpu(cpu, &d->cpu_mask);
	if (cpumask_empty(&d->cpu_mask)) {
		resctrl_offline_domain(r, d);
		list_del(&d->list);

		/*
		 * rdt_domain "d" is going to be freed below, so clear
		 * its pointer from pseudo_lock_region struct.
		 */
		if (d->plr)
			d->plr->d = NULL;
		domain_free(hw_dom);

		return;
	}

	if (r == &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl) {
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
	wrmsr(MSR_IA32_PQR_ASSOC, 0, 0);
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

	for_each_alloc_capable_rdt_resource(r) {
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
	RDT_FLAG_SMBA,
	RDT_FLAG_BMEC,
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
	RDT_OPT(RDT_FLAG_SMBA,	    "smba",	X86_FEATURE_SMBA),
	RDT_OPT(RDT_FLAG_BMEC,	    "bmec",	X86_FEATURE_BMEC),
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

bool __init rdt_cpu_has(int flag)
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
	struct rdt_hw_resource *hw_res = &rdt_resources_all[RDT_RESOURCE_MBA];

	if (!rdt_cpu_has(X86_FEATURE_MBA))
		return false;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL)
		return __get_mem_config_intel(&hw_res->r_resctrl);
	else if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return __rdt_get_mem_config_amd(&hw_res->r_resctrl);

	return false;
}

static __init bool get_slow_mem_config(void)
{
	struct rdt_hw_resource *hw_res = &rdt_resources_all[RDT_RESOURCE_SMBA];

	if (!rdt_cpu_has(X86_FEATURE_SMBA))
		return false;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return __rdt_get_mem_config_amd(&hw_res->r_resctrl);

	return false;
}

static __init bool get_rdt_alloc_resources(void)
{
	struct rdt_resource *r;
	bool ret = false;

	if (rdt_alloc_capable)
		return true;

	if (!boot_cpu_has(X86_FEATURE_RDT_A))
		return false;

	if (rdt_cpu_has(X86_FEATURE_CAT_L3)) {
		r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
		rdt_get_cache_alloc_cfg(1, r);
		if (rdt_cpu_has(X86_FEATURE_CDP_L3))
			rdt_get_cdp_l3_config();
		ret = true;
	}
	if (rdt_cpu_has(X86_FEATURE_CAT_L2)) {
		/* CPUID 0x10.2 fields are same format at 0x10.1 */
		r = &rdt_resources_all[RDT_RESOURCE_L2].r_resctrl;
		rdt_get_cache_alloc_cfg(2, r);
		if (rdt_cpu_has(X86_FEATURE_CDP_L2))
			rdt_get_cdp_l2_config();
		ret = true;
	}

	if (get_mem_config())
		ret = true;

	if (get_slow_mem_config())
		ret = true;

	return ret;
}

static __init bool get_rdt_mon_resources(void)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;

	if (rdt_cpu_has(X86_FEATURE_CQM_OCCUP_LLC))
		rdt_mon_features |= (1 << QOS_L3_OCCUP_EVENT_ID);
	if (rdt_cpu_has(X86_FEATURE_CQM_MBM_TOTAL))
		rdt_mon_features |= (1 << QOS_L3_MBM_TOTAL_EVENT_ID);
	if (rdt_cpu_has(X86_FEATURE_CQM_MBM_LOCAL))
		rdt_mon_features |= (1 << QOS_L3_MBM_LOCAL_EVENT_ID);

	if (!rdt_mon_features)
		return false;

	return !rdt_get_mon_l3_config(r);
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
		fallthrough;
	case INTEL_FAM6_BROADWELL_X:
		intel_rdt_mbm_apply_quirk();
		break;
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
	struct rdt_hw_resource *hw_res;
	struct rdt_resource *r;

	for_each_rdt_resource(r) {
		hw_res = resctrl_to_arch_res(r);

		if (r->rid == RDT_RESOURCE_L3 ||
		    r->rid == RDT_RESOURCE_L2) {
			r->cache.arch_has_sparse_bitmaps = false;
			r->cache.arch_has_per_cpu_cfg = false;
			r->cache.min_cbm_bits = 1;
		} else if (r->rid == RDT_RESOURCE_MBA) {
			hw_res->msr_base = MSR_IA32_MBA_THRTL_BASE;
			hw_res->msr_update = mba_wrmsr_intel;
		}
	}
}

static __init void rdt_init_res_defs_amd(void)
{
	struct rdt_hw_resource *hw_res;
	struct rdt_resource *r;

	for_each_rdt_resource(r) {
		hw_res = resctrl_to_arch_res(r);

		if (r->rid == RDT_RESOURCE_L3 ||
		    r->rid == RDT_RESOURCE_L2) {
			r->cache.arch_has_sparse_bitmaps = true;
			r->cache.arch_has_per_cpu_cfg = true;
			r->cache.min_cbm_bits = 0;
		} else if (r->rid == RDT_RESOURCE_MBA) {
			hw_res->msr_base = MSR_IA32_MBA_BW_BASE;
			hw_res->msr_update = mba_wrmsr_amd;
		} else if (r->rid == RDT_RESOURCE_SMBA) {
			hw_res->msr_base = MSR_IA32_SMBA_BW_BASE;
			hw_res->msr_update = mba_wrmsr_amd;
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
		c->x86_cache_mbm_width_offset = eax & 0xff;

		if (c->x86_vendor == X86_VENDOR_AMD && !c->x86_cache_mbm_width_offset)
			c->x86_cache_mbm_width_offset = MBM_CNTR_WIDTH_OFFSET_AMD;
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
