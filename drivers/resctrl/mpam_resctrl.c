// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/arm_mpam.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
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

/*
 * MSC may raise an error interrupt if it sees an out or range partid/pmg,
 * and go on to truncate the value. Regardless of what the hardware supports,
 * only the system wide safe value is safe to use.
 */
u32 resctrl_arch_get_num_closid(struct rdt_resource *ignored)
{
	return mpam_partid_max + 1;
}

struct rdt_resource *resctrl_arch_get_resource(enum resctrl_res_level l)
{
	if (l >= RDT_NUM_RESOURCES)
		return NULL;

	return &mpam_resctrl_controls[l].resctrl_res;
}

static int mpam_resctrl_control_init(struct mpam_resctrl_res *res)
{
	/* TODO: initialise the resctrl resources */

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

	/* TODO: pick MPAM classes to map to resctrl resources */

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
