// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/arm_mpam.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/resctrl.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/mpam.h>

#include "mpam_internal.h"

/*
 * The classes we've picked to map to resctrl resources, wrapped
 * in with their resctrl structure.
 * Class pointer may be NULL.
 */
static struct mpam_resctrl_res mpam_resctrl_controls[RDT_NUM_RESOURCES];

#define for_each_mpam_resctrl_control(res, rid)					\
	for (rid = 0, res = &mpam_resctrl_controls[rid];			\
	     rid < RDT_NUM_RESOURCES;						\
	     rid++, res = &mpam_resctrl_controls[rid])

/* The lock for modifying resctrl's domain lists from cpuhp callbacks. */
static DEFINE_MUTEX(domain_list_lock);

/*
 * MPAM emulates CDP by setting different PARTID in the I/D fields of MPAM0_EL1.
 * This applies globally to all traffic the CPU generates.
 */
static bool cdp_enabled;

bool resctrl_arch_alloc_capable(void)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	for_each_mpam_resctrl_control(res, rid) {
		if (res->resctrl_res.alloc_capable)
			return true;
	}

	return false;
}

bool resctrl_arch_get_cdp_enabled(enum resctrl_res_level rid)
{
	return mpam_resctrl_controls[rid].cdp_enabled;
}

/**
 * resctrl_reset_task_closids() - Reset the PARTID/PMG values for all tasks.
 *
 * At boot, all existing tasks use partid zero for D and I.
 * To enable/disable CDP emulation, all these tasks need relabelling.
 */
static void resctrl_reset_task_closids(void)
{
	struct task_struct *p, *t;

	read_lock(&tasklist_lock);
	for_each_process_thread(p, t) {
		resctrl_arch_set_closid_rmid(t, RESCTRL_RESERVED_CLOSID,
					     RESCTRL_RESERVED_RMID);
	}
	read_unlock(&tasklist_lock);
}

int resctrl_arch_set_cdp_enabled(enum resctrl_res_level rid, bool enable)
{
	u32 partid_i = RESCTRL_RESERVED_CLOSID, partid_d = RESCTRL_RESERVED_CLOSID;
	int cpu;

	if (!IS_ENABLED(CONFIG_EXPERT) && enable) {
		/*
		 * If the resctrl fs is mounted more than once, sequentially,
		 * then CDP can lead to the use of out of range PARTIDs.
		 */
		pr_warn("CDP not supported\n");
		return -EOPNOTSUPP;
	}

	if (enable)
		pr_warn("CDP is an expert feature and may cause MPAM to malfunction.\n");

	/*
	 * resctrl_arch_set_cdp_enabled() is only called with enable set to
	 * false on error and unmount.
	 */
	cdp_enabled = enable;
	mpam_resctrl_controls[rid].cdp_enabled = enable;

	/* The mbw_max feature can't hide cdp as it's a per-partid maximum. */
	if (cdp_enabled && !mpam_resctrl_controls[RDT_RESOURCE_MBA].cdp_enabled)
		mpam_resctrl_controls[RDT_RESOURCE_MBA].resctrl_res.alloc_capable = false;

	if (mpam_resctrl_controls[RDT_RESOURCE_MBA].cdp_enabled &&
	    mpam_resctrl_controls[RDT_RESOURCE_MBA].class)
		mpam_resctrl_controls[RDT_RESOURCE_MBA].resctrl_res.alloc_capable = true;

	if (enable) {
		if (mpam_partid_max < 1)
			return -EINVAL;

		partid_d = resctrl_get_config_index(RESCTRL_RESERVED_CLOSID, CDP_DATA);
		partid_i = resctrl_get_config_index(RESCTRL_RESERVED_CLOSID, CDP_CODE);
	}

	mpam_set_task_partid_pmg(current, partid_d, partid_i, 0, 0);
	WRITE_ONCE(arm64_mpam_global_default, mpam_get_regval(current));

	resctrl_reset_task_closids();

	for_each_possible_cpu(cpu)
		mpam_set_cpu_defaults(cpu, partid_d, partid_i, 0, 0);
	on_each_cpu(resctrl_arch_sync_cpu_closid_rmid, NULL, 1);

	return 0;
}

static bool mpam_resctrl_hide_cdp(enum resctrl_res_level rid)
{
	return cdp_enabled && !resctrl_arch_get_cdp_enabled(rid);
}

/*
 * MSC may raise an error interrupt if it sees an out or range partid/pmg,
 * and go on to truncate the value. Regardless of what the hardware supports,
 * only the system wide safe value is safe to use.
 */
u32 resctrl_arch_get_num_closid(struct rdt_resource *ignored)
{
	return mpam_partid_max + 1;
}

void resctrl_arch_sched_in(struct task_struct *tsk)
{
	lockdep_assert_preemption_disabled();

	mpam_thread_switch(tsk);
}

void resctrl_arch_set_cpu_default_closid_rmid(int cpu, u32 closid, u32 rmid)
{
	WARN_ON_ONCE(closid > U16_MAX);
	WARN_ON_ONCE(rmid > U8_MAX);

	if (!cdp_enabled) {
		mpam_set_cpu_defaults(cpu, closid, closid, rmid, rmid);
	} else {
		/*
		 * When CDP is enabled, resctrl halves the closid range and we
		 * use odd/even partid for one closid.
		 */
		u32 partid_d = resctrl_get_config_index(closid, CDP_DATA);
		u32 partid_i = resctrl_get_config_index(closid, CDP_CODE);

		mpam_set_cpu_defaults(cpu, partid_d, partid_i, rmid, rmid);
	}
}

void resctrl_arch_sync_cpu_closid_rmid(void *info)
{
	struct resctrl_cpu_defaults *r = info;

	lockdep_assert_preemption_disabled();

	if (r) {
		resctrl_arch_set_cpu_default_closid_rmid(smp_processor_id(),
							 r->closid, r->rmid);
	}

	resctrl_arch_sched_in(current);
}

void resctrl_arch_set_closid_rmid(struct task_struct *tsk, u32 closid, u32 rmid)
{
	WARN_ON_ONCE(closid > U16_MAX);
	WARN_ON_ONCE(rmid > U8_MAX);

	if (!cdp_enabled) {
		mpam_set_task_partid_pmg(tsk, closid, closid, rmid, rmid);
	} else {
		u32 partid_d = resctrl_get_config_index(closid, CDP_DATA);
		u32 partid_i = resctrl_get_config_index(closid, CDP_CODE);

		mpam_set_task_partid_pmg(tsk, partid_d, partid_i, rmid, rmid);
	}
}

bool resctrl_arch_match_closid(struct task_struct *tsk, u32 closid)
{
	u64 regval = mpam_get_regval(tsk);
	u32 tsk_closid = FIELD_GET(MPAM0_EL1_PARTID_D, regval);

	if (cdp_enabled)
		tsk_closid >>= 1;

	return tsk_closid == closid;
}

/* The task's pmg is not unique, the partid must be considered too */
bool resctrl_arch_match_rmid(struct task_struct *tsk, u32 closid, u32 rmid)
{
	u64 regval = mpam_get_regval(tsk);
	u32 tsk_closid = FIELD_GET(MPAM0_EL1_PARTID_D, regval);
	u32 tsk_rmid = FIELD_GET(MPAM0_EL1_PMG_D, regval);

	if (cdp_enabled)
		tsk_closid >>= 1;

	return (tsk_closid == closid) && (tsk_rmid == rmid);
}

struct rdt_resource *resctrl_arch_get_resource(enum resctrl_res_level l)
{
	if (l >= RDT_NUM_RESOURCES)
		return NULL;

	return &mpam_resctrl_controls[l].resctrl_res;
}

static bool cache_has_usable_cpor(struct mpam_class *class)
{
	struct mpam_props *cprops = &class->props;

	if (!mpam_has_feature(mpam_feat_cpor_part, cprops))
		return false;

	/* resctrl uses u32 for all bitmap configurations */
	return class->props.cpbm_wd <= 32;
}

/* Test whether we can export MPAM_CLASS_CACHE:{2,3}? */
static void mpam_resctrl_pick_caches(void)
{
	struct mpam_class *class;
	struct mpam_resctrl_res *res;

	lockdep_assert_cpus_held();

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(class, &mpam_classes, classes_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (class->type != MPAM_CLASS_CACHE) {
			pr_debug("class %u is not a cache\n", class->level);
			continue;
		}

		if (class->level != 2 && class->level != 3) {
			pr_debug("class %u is not L2 or L3\n", class->level);
			continue;
		}

		if (!cache_has_usable_cpor(class)) {
			pr_debug("class %u cache misses CPOR\n", class->level);
			continue;
		}

		if (!cpumask_equal(&class->affinity, cpu_possible_mask)) {
			pr_debug("class %u has missing CPUs, mask %*pb != %*pb\n", class->level,
				 cpumask_pr_args(&class->affinity),
				 cpumask_pr_args(cpu_possible_mask));
			continue;
		}

		if (class->level == 2)
			res = &mpam_resctrl_controls[RDT_RESOURCE_L2];
		else
			res = &mpam_resctrl_controls[RDT_RESOURCE_L3];
		res->class = class;
	}
}

static int mpam_resctrl_control_init(struct mpam_resctrl_res *res)
{
	struct mpam_class *class = res->class;
	struct rdt_resource *r = &res->resctrl_res;

	switch (r->rid) {
	case RDT_RESOURCE_L2:
	case RDT_RESOURCE_L3:
		r->schema_fmt = RESCTRL_SCHEMA_BITMAP;
		r->cache.arch_has_sparse_bitmasks = true;

		r->cache.cbm_len = class->props.cpbm_wd;
		/* mpam_devices will reject empty bitmaps */
		r->cache.min_cbm_bits = 1;

		if (r->rid == RDT_RESOURCE_L2) {
			r->name = "L2";
			r->ctrl_scope = RESCTRL_L2_CACHE;
			r->cdp_capable = true;
		} else {
			r->name = "L3";
			r->ctrl_scope = RESCTRL_L3_CACHE;
			r->cdp_capable = true;
		}

		/*
		 * Which bits are shared with other ...things...  Unknown
		 * devices use partid-0 which uses all the bitmap fields. Until
		 * we have configured the SMMU and GIC not to do this 'all the
		 * bits' is the correct answer here.
		 */
		r->cache.shareable_bits = resctrl_get_default_ctrl(r);
		r->alloc_capable = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mpam_resctrl_pick_domain_id(int cpu, struct mpam_component *comp)
{
	struct mpam_class *class = comp->class;

	if (class->type == MPAM_CLASS_CACHE)
		return comp->comp_id;

	/* TODO: repaint domain ids to match the L3 domain ids */
	/* Otherwise, expose the ID used by the firmware table code. */
	return comp->comp_id;
}

u32 resctrl_arch_get_config(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type type)
{
	u32 partid;
	struct mpam_config *cfg;
	struct mpam_props *cprops;
	struct mpam_resctrl_res *res;
	struct mpam_resctrl_dom *dom;
	enum mpam_device_features configured_by;

	lockdep_assert_cpus_held();

	if (!mpam_is_enabled())
		return resctrl_get_default_ctrl(r);

	res = container_of(r, struct mpam_resctrl_res, resctrl_res);
	dom = container_of(d, struct mpam_resctrl_dom, resctrl_ctrl_dom);
	cprops = &res->class->props;

	/*
	 * When CDP is enabled, but the resource doesn't support it,
	 * the control is cloned across both partids.
	 * Pick one at random to read:
	 */
	if (mpam_resctrl_hide_cdp(r->rid))
		type = CDP_DATA;

	partid = resctrl_get_config_index(closid, type);
	cfg = &dom->ctrl_comp->cfg[partid];

	switch (r->rid) {
	case RDT_RESOURCE_L2:
	case RDT_RESOURCE_L3:
		configured_by = mpam_feat_cpor_part;
		break;
	default:
		return resctrl_get_default_ctrl(r);
	}

	if (!r->alloc_capable || partid >= resctrl_arch_get_num_closid(r) ||
	    !mpam_has_feature(configured_by, cfg))
		return resctrl_get_default_ctrl(r);

	switch (configured_by) {
	case mpam_feat_cpor_part:
		return cfg->cpbm;
	default:
		return resctrl_get_default_ctrl(r);
	}
}

int resctrl_arch_update_one(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type t, u32 cfg_val)
{
	int err;
	u32 partid;
	struct mpam_config cfg;
	struct mpam_props *cprops;
	struct mpam_resctrl_res *res;
	struct mpam_resctrl_dom *dom;

	lockdep_assert_cpus_held();
	lockdep_assert_irqs_enabled();

	/*
	 * No need to check the CPU as mpam_apply_config() doesn't care, and
	 * resctrl_arch_update_domains() relies on this.
	 */
	res = container_of(r, struct mpam_resctrl_res, resctrl_res);
	dom = container_of(d, struct mpam_resctrl_dom, resctrl_ctrl_dom);
	cprops = &res->class->props;

	if (mpam_resctrl_hide_cdp(r->rid))
		t = CDP_DATA;

	partid = resctrl_get_config_index(closid, t);
	if (!r->alloc_capable || partid >= resctrl_arch_get_num_closid(r)) {
		pr_debug("Not alloc capable or computed PARTID out of range\n");
		return -EINVAL;
	}

	/*
	 * Copy the current config to avoid clearing other resources when the
	 * same component is exposed multiple times through resctrl.
	 */
	cfg = dom->ctrl_comp->cfg[partid];

	switch (r->rid) {
	case RDT_RESOURCE_L2:
	case RDT_RESOURCE_L3:
		cfg.cpbm = cfg_val;
		mpam_set_feature(mpam_feat_cpor_part, &cfg);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When CDP is enabled, but the resource doesn't support it, we need to
	 * apply the same configuration to the other partid.
	 */
	if (mpam_resctrl_hide_cdp(r->rid)) {
		partid = resctrl_get_config_index(closid, CDP_CODE);
		err = mpam_apply_config(dom->ctrl_comp, partid, &cfg);
		if (err)
			return err;

		partid = resctrl_get_config_index(closid, CDP_DATA);
		return mpam_apply_config(dom->ctrl_comp, partid, &cfg);
	}

	return mpam_apply_config(dom->ctrl_comp, partid, &cfg);
}

int resctrl_arch_update_domains(struct rdt_resource *r, u32 closid)
{
	int err;
	struct rdt_ctrl_domain *d;

	lockdep_assert_cpus_held();
	lockdep_assert_irqs_enabled();

	list_for_each_entry_rcu(d, &r->ctrl_domains, hdr.list) {
		for (enum resctrl_conf_type t = 0; t < CDP_NUM_TYPES; t++) {
			struct resctrl_staged_config *cfg = &d->staged_config[t];

			if (!cfg->have_new_ctrl)
				continue;

			err = resctrl_arch_update_one(r, d, closid, t,
						      cfg->new_ctrl);
			if (err)
				return err;
		}
	}

	return 0;
}

void resctrl_arch_reset_all_ctrls(struct rdt_resource *r)
{
	struct mpam_resctrl_res *res;

	lockdep_assert_cpus_held();

	if (!mpam_is_enabled())
		return;

	res = container_of(r, struct mpam_resctrl_res, resctrl_res);
	mpam_reset_class_locked(res->class);
}

static void mpam_resctrl_domain_hdr_init(int cpu, struct mpam_component *comp,
					 enum resctrl_res_level rid,
					 struct rdt_domain_hdr *hdr)
{
	lockdep_assert_cpus_held();

	INIT_LIST_HEAD(&hdr->list);
	hdr->id = mpam_resctrl_pick_domain_id(cpu, comp);
	hdr->rid = rid;
	cpumask_set_cpu(cpu, &hdr->cpu_mask);
}

static void mpam_resctrl_online_domain_hdr(unsigned int cpu,
					   struct rdt_domain_hdr *hdr)
{
	lockdep_assert_cpus_held();

	cpumask_set_cpu(cpu, &hdr->cpu_mask);
}

/**
 * mpam_resctrl_offline_domain_hdr() - Update the domain header to remove a CPU.
 * @cpu:	The CPU to remove from the domain.
 * @hdr:	The domain's header.
 *
 * Removes @cpu from the header mask. If this was the last CPU in the domain,
 * the domain header is removed from its parent list and true is returned,
 * indicating the parent structure can be freed.
 * If there are other CPUs in the domain, returns false.
 */
static bool mpam_resctrl_offline_domain_hdr(unsigned int cpu,
					    struct rdt_domain_hdr *hdr)
{
	lockdep_assert_held(&domain_list_lock);

	cpumask_clear_cpu(cpu, &hdr->cpu_mask);
	if (cpumask_empty(&hdr->cpu_mask)) {
		list_del_rcu(&hdr->list);
		synchronize_rcu();
		return true;
	}

	return false;
}

static void mpam_resctrl_domain_insert(struct list_head *list,
				       struct rdt_domain_hdr *new)
{
	struct rdt_domain_hdr *err;
	struct list_head *pos = NULL;

	lockdep_assert_held(&domain_list_lock);

	err = resctrl_find_domain(list, new->id, &pos);
	if (WARN_ON_ONCE(err))
		return;

	list_add_tail_rcu(&new->list, pos);
}

static struct mpam_resctrl_dom *
mpam_resctrl_alloc_domain(unsigned int cpu, struct mpam_resctrl_res *res)
{
	int err;
	struct mpam_resctrl_dom *dom;
	struct rdt_ctrl_domain *ctrl_d;
	struct mpam_class *class = res->class;
	struct mpam_component *comp_iter, *ctrl_comp;
	struct rdt_resource *r = &res->resctrl_res;

	lockdep_assert_held(&domain_list_lock);

	ctrl_comp = NULL;
	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(comp_iter, &class->components, class_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (cpumask_test_cpu(cpu, &comp_iter->affinity)) {
			ctrl_comp = comp_iter;
			break;
		}
	}

	/* class has no component for this CPU */
	if (WARN_ON_ONCE(!ctrl_comp))
		return ERR_PTR(-EINVAL);

	dom = kzalloc_node(sizeof(*dom), GFP_KERNEL, cpu_to_node(cpu));
	if (!dom)
		return ERR_PTR(-ENOMEM);

	if (r->alloc_capable) {
		dom->ctrl_comp = ctrl_comp;

		ctrl_d = &dom->resctrl_ctrl_dom;
		mpam_resctrl_domain_hdr_init(cpu, ctrl_comp, r->rid, &ctrl_d->hdr);
		ctrl_d->hdr.type = RESCTRL_CTRL_DOMAIN;
		err = resctrl_online_ctrl_domain(r, ctrl_d);
		if (err)
			goto free_domain;

		mpam_resctrl_domain_insert(&r->ctrl_domains, &ctrl_d->hdr);
	} else {
		pr_debug("Skipped control domain online - no controls\n");
	}
	return dom;

free_domain:
	kfree(dom);
	dom = ERR_PTR(err);

	return dom;
}

static struct mpam_resctrl_dom *
mpam_resctrl_get_domain_from_cpu(int cpu, struct mpam_resctrl_res *res)
{
	struct mpam_resctrl_dom *dom;
	struct rdt_resource *r = &res->resctrl_res;

	lockdep_assert_cpus_held();

	list_for_each_entry_rcu(dom, &r->ctrl_domains, resctrl_ctrl_dom.hdr.list) {
		if (cpumask_test_cpu(cpu, &dom->ctrl_comp->affinity))
			return dom;
	}

	return NULL;
}

int mpam_resctrl_online_cpu(unsigned int cpu)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	guard(mutex)(&domain_list_lock);
	for_each_mpam_resctrl_control(res, rid) {
		struct mpam_resctrl_dom *dom;
		struct rdt_resource *r = &res->resctrl_res;

		if (!res->class)
			continue;	// dummy_resource;

		dom = mpam_resctrl_get_domain_from_cpu(cpu, res);
		if (!dom) {
			dom = mpam_resctrl_alloc_domain(cpu, res);
			if (IS_ERR(dom))
				return PTR_ERR(dom);
		} else {
			if (r->alloc_capable) {
				struct rdt_ctrl_domain *ctrl_d = &dom->resctrl_ctrl_dom;

				mpam_resctrl_online_domain_hdr(cpu, &ctrl_d->hdr);
			}
		}
	}

	resctrl_online_cpu(cpu);

	return 0;
}

void mpam_resctrl_offline_cpu(unsigned int cpu)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	resctrl_offline_cpu(cpu);

	guard(mutex)(&domain_list_lock);
	for_each_mpam_resctrl_control(res, rid) {
		struct mpam_resctrl_dom *dom;
		struct rdt_ctrl_domain *ctrl_d;
		bool ctrl_dom_empty;
		struct rdt_resource *r = &res->resctrl_res;

		if (!res->class)
			continue;	// dummy resource

		dom = mpam_resctrl_get_domain_from_cpu(cpu, res);
		if (WARN_ON_ONCE(!dom))
			continue;

		if (r->alloc_capable) {
			ctrl_d = &dom->resctrl_ctrl_dom;
			ctrl_dom_empty = mpam_resctrl_offline_domain_hdr(cpu, &ctrl_d->hdr);
			if (ctrl_dom_empty)
				resctrl_offline_ctrl_domain(&res->resctrl_res, ctrl_d);
		} else {
			ctrl_dom_empty = true;
		}

		if (ctrl_dom_empty)
			kfree(dom);
	}
}

int mpam_resctrl_setup(void)
{
	int err = 0;
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	cpus_read_lock();
	for_each_mpam_resctrl_control(res, rid) {
		INIT_LIST_HEAD_RCU(&res->resctrl_res.ctrl_domains);
		res->resctrl_res.rid = rid;
	}

	/* Find some classes to use for controls */
	mpam_resctrl_pick_caches();

	/* Initialise the resctrl structures from the classes */
	for_each_mpam_resctrl_control(res, rid) {
		if (!res->class)
			continue;	// dummy resource

		err = mpam_resctrl_control_init(res);
		if (err) {
			pr_debug("Failed to initialise rid %u\n", rid);
			break;
		}
	}
	cpus_read_unlock();

	if (err) {
		pr_debug("Internal error %d - resctrl not supported\n", err);
		return err;
	}

	if (!resctrl_arch_alloc_capable()) {
		pr_debug("No alloc(%u) found - resctrl not supported\n",
			 resctrl_arch_alloc_capable());
		return -EOPNOTSUPP;
	}

	/* TODO: call resctrl_init() */

	return 0;
}
