/*
 * Resource Director Technology(RDT)
 * - Monitoring code
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author:
 *    Vikas Shivappa <vikas.shivappa@intel.com>
 *
 * This replaces the cqm.c based on perf but we reuse a lot of
 * code and datastructures originally from Peter Zijlstra and Matt Fleming.
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

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>
#include "intel_rdt.h"

#define MSR_IA32_QM_CTR		0x0c8e
#define MSR_IA32_QM_EVTSEL		0x0c8d

struct rmid_entry {
	u32				rmid;
	atomic_t			busy;
	struct list_head		list;
};

/**
 * @rmid_free_lru    A least recently used list of free RMIDs
 *     These RMIDs are guaranteed to have an occupancy less than the
 *     threshold occupancy
 */
static LIST_HEAD(rmid_free_lru);

/**
 * @rmid_limbo_lru       list of currently unused but (potentially)
 *     dirty RMIDs.
 *     This list contains RMIDs that no one is currently using but that
 *     may have a occupancy value > intel_cqm_threshold. User can change
 *     the threshold occupancy value.
 */
static LIST_HEAD(rmid_limbo_lru);

/**
 * @rmid_entry - The entry in the limbo and free lists.
 */
static struct rmid_entry	*rmid_ptrs;

/*
 * Global boolean for rdt_monitor which is true if any
 * resource monitoring is enabled.
 */
bool rdt_mon_capable;

/*
 * Global to indicate which monitoring events are enabled.
 */
unsigned int rdt_mon_features;

/*
 * This is the threshold cache occupancy at which we will consider an
 * RMID available for re-allocation.
 */
unsigned int intel_cqm_threshold;

static inline struct rmid_entry *__rmid_entry(u32 rmid)
{
	struct rmid_entry *entry;

	entry = &rmid_ptrs[rmid];
	WARN_ON(entry->rmid != rmid);

	return entry;
}

static u64 __rmid_read(u32 rmid, u32 eventid)
{
	u64 val;

	/*
	 * As per the SDM, when IA32_QM_EVTSEL.EvtID (bits 7:0) is configured
	 * with a valid event code for supported resource type and the bits
	 * IA32_QM_EVTSEL.RMID (bits 41:32) are configured with valid RMID,
	 * IA32_QM_CTR.data (bits 61:0) reports the monitored data.
	 * IA32_QM_CTR.Error (bit 63) and IA32_QM_CTR.Unavailable (bit 62)
	 * are error bits.
	 */
	wrmsr(MSR_IA32_QM_EVTSEL, eventid, rmid);
	rdmsrl(MSR_IA32_QM_CTR, val);

	return val;
}

/*
 * Walk the limbo list looking at any RMIDs that are flagged in the
 * domain rmid_busy_llc bitmap as busy. If the reported LLC occupancy
 * is below the threshold clear the busy bit and decrement the count.
 * If the busy count gets to zero on an RMID we stop looking.
 * This can be called from an IPI.
 * We need an atomic for the busy count because multiple CPUs may check
 * the same RMID at the same time.
 */
static bool __check_limbo(struct rdt_domain *d)
{
	struct rmid_entry *entry;
	u64 val;

	list_for_each_entry(entry, &rmid_limbo_lru, list) {
		if (!test_bit(entry->rmid, d->rmid_busy_llc))
			continue;
		val = __rmid_read(entry->rmid, QOS_L3_OCCUP_EVENT_ID);
		if (val <= intel_cqm_threshold) {
			clear_bit(entry->rmid, d->rmid_busy_llc);
			if (atomic_dec_and_test(&entry->busy))
				return true;
		}
	}
	return false;
}

static void check_limbo(void *arg)
{
	struct rdt_domain *d;

	d = get_domain_from_cpu(smp_processor_id(),
				&rdt_resources_all[RDT_RESOURCE_L3]);

	if (d)
		__check_limbo(d);
}

static bool has_busy_rmid(struct rdt_resource *r, struct rdt_domain *d)
{
	return find_first_bit(d->rmid_busy_llc, r->num_rmid) != r->num_rmid;
}

/*
 * Scan the limbo list and move all entries that are below the
 * intel_cqm_threshold to the free list.
 * Return "true" if the limbo list is empty, "false" if there are
 * still some RMIDs there.
 */
static bool try_freeing_limbo_rmid(void)
{
	struct rmid_entry *entry, *tmp;
	struct rdt_resource *r;
	cpumask_var_t cpu_mask;
	struct rdt_domain *d;
	bool ret = true;
	int cpu;

	if (list_empty(&rmid_limbo_lru))
		return ret;

	r = &rdt_resources_all[RDT_RESOURCE_L3];

	cpu = get_cpu();

	/*
	 * First see if we can free up an RMID by checking busy values
	 * on the local package.
	 */
	d = get_domain_from_cpu(cpu, r);
	if (d && has_busy_rmid(r, d) && __check_limbo(d)) {
		list_for_each_entry_safe(entry, tmp, &rmid_limbo_lru, list) {
			if (atomic_read(&entry->busy) == 0) {
				list_del(&entry->list);
				list_add_tail(&entry->list, &rmid_free_lru);
				goto done;
			}
		}
	}

	if (!zalloc_cpumask_var(&cpu_mask, GFP_KERNEL)) {
		ret = false;
		goto done;
	}

	/*
	 * Build a mask of other domains that have busy RMIDs
	 */
	list_for_each_entry(d, &r->domains, list) {
		if (!cpumask_test_cpu(cpu, &d->cpu_mask) &&
		    has_busy_rmid(r, d))
			cpumask_set_cpu(cpumask_any(&d->cpu_mask), cpu_mask);
	}
	if (cpumask_empty(cpu_mask)) {
		ret = false;
		goto free_mask;
	}

	/*
	 * Scan domains with busy RMIDs to check if they still are busy
	 */
	on_each_cpu_mask(cpu_mask, check_limbo, NULL, true);

	/* Walk limbo list moving all free RMIDs to the &rmid_free_lru list */
	list_for_each_entry_safe(entry, tmp, &rmid_limbo_lru, list) {
		if (atomic_read(&entry->busy) != 0) {
			ret = false;
			continue;
		}
		list_del(&entry->list);
		list_add_tail(&entry->list, &rmid_free_lru);
	}

free_mask:
	free_cpumask_var(cpu_mask);
done:
	put_cpu();
	return ret;
}

/*
 * As of now the RMIDs allocation is global.
 * However we keep track of which packages the RMIDs
 * are used to optimize the limbo list management.
 */
int alloc_rmid(void)
{
	struct rmid_entry *entry;
	bool ret;

	lockdep_assert_held(&rdtgroup_mutex);

	if (list_empty(&rmid_free_lru)) {
		ret = try_freeing_limbo_rmid();
		if (list_empty(&rmid_free_lru))
			return ret ? -ENOSPC : -EBUSY;
	}

	entry = list_first_entry(&rmid_free_lru,
				 struct rmid_entry, list);
	list_del(&entry->list);

	return entry->rmid;
}

static void add_rmid_to_limbo(struct rmid_entry *entry)
{
	struct rdt_resource *r;
	struct rdt_domain *d;
	int cpu, nbusy = 0;
	u64 val;

	r = &rdt_resources_all[RDT_RESOURCE_L3];

	cpu = get_cpu();
	list_for_each_entry(d, &r->domains, list) {
		if (cpumask_test_cpu(cpu, &d->cpu_mask)) {
			val = __rmid_read(entry->rmid, QOS_L3_OCCUP_EVENT_ID);
			if (val <= intel_cqm_threshold)
				continue;
		}
		set_bit(entry->rmid, d->rmid_busy_llc);
		nbusy++;
	}
	put_cpu();

	if (nbusy) {
		atomic_set(&entry->busy, nbusy);
		list_add_tail(&entry->list, &rmid_limbo_lru);
	} else {
		list_add_tail(&entry->list, &rmid_free_lru);
	}
}

void free_rmid(u32 rmid)
{
	struct rmid_entry *entry;

	if (!rmid)
		return;

	lockdep_assert_held(&rdtgroup_mutex);

	entry = __rmid_entry(rmid);

	if (is_llc_occupancy_enabled())
		add_rmid_to_limbo(entry);
	else
		list_add_tail(&entry->list, &rmid_free_lru);
}

static int __mon_event_count(u32 rmid, struct rmid_read *rr)
{
	u64 tval;

	tval = __rmid_read(rmid, rr->evtid);
	if (tval & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL)) {
		rr->val = tval;
		return -EINVAL;
	}
	switch (rr->evtid) {
	case QOS_L3_OCCUP_EVENT_ID:
		rr->val += tval;
		return 0;
	default:
		/*
		 * Code would never reach here because
		 * an invalid event id would fail the __rmid_read.
		 */
		return -EINVAL;
	}
}

/*
 * This is called via IPI to read the CQM/MBM counters
 * on a domain.
 */
void mon_event_count(void *info)
{
	struct rdtgroup *rdtgrp, *entry;
	struct rmid_read *rr = info;
	struct list_head *head;

	rdtgrp = rr->rgrp;

	if (__mon_event_count(rdtgrp->mon.rmid, rr))
		return;

	/*
	 * For Ctrl groups read data from child monitor groups.
	 */
	head = &rdtgrp->mon.crdtgrp_list;

	if (rdtgrp->type == RDTCTRL_GROUP) {
		list_for_each_entry(entry, head, mon.crdtgrp_list) {
			if (__mon_event_count(entry->mon.rmid, rr))
				return;
		}
	}
}
static int dom_data_init(struct rdt_resource *r)
{
	struct rmid_entry *entry = NULL;
	int i, nr_rmids;

	nr_rmids = r->num_rmid;
	rmid_ptrs = kcalloc(nr_rmids, sizeof(struct rmid_entry), GFP_KERNEL);
	if (!rmid_ptrs)
		return -ENOMEM;

	for (i = 0; i < nr_rmids; i++) {
		entry = &rmid_ptrs[i];
		INIT_LIST_HEAD(&entry->list);

		entry->rmid = i;
		list_add_tail(&entry->list, &rmid_free_lru);
	}

	/*
	 * RMID 0 is special and is always allocated. It's used for all
	 * tasks that are not monitored.
	 */
	entry = __rmid_entry(0);
	list_del(&entry->list);

	return 0;
}

static struct mon_evt llc_occupancy_event = {
	.name		= "llc_occupancy",
	.evtid		= QOS_L3_OCCUP_EVENT_ID,
};

/*
 * Initialize the event list for the resource.
 *
 * Note that MBM events are also part of RDT_RESOURCE_L3 resource
 * because as per the SDM the total and local memory bandwidth
 * are enumerated as part of L3 monitoring.
 */
static void l3_mon_evt_init(struct rdt_resource *r)
{
	INIT_LIST_HEAD(&r->evt_list);

	if (is_llc_occupancy_enabled())
		list_add_tail(&llc_occupancy_event.list, &r->evt_list);
}

int rdt_get_mon_l3_config(struct rdt_resource *r)
{
	int ret;

	r->mon_scale = boot_cpu_data.x86_cache_occ_scale;
	r->num_rmid = boot_cpu_data.x86_cache_max_rmid + 1;

	/*
	 * A reasonable upper limit on the max threshold is the number
	 * of lines tagged per RMID if all RMIDs have the same number of
	 * lines tagged in the LLC.
	 *
	 * For a 35MB LLC and 56 RMIDs, this is ~1.8% of the LLC.
	 */
	intel_cqm_threshold = boot_cpu_data.x86_cache_size * 1024 / r->num_rmid;

	/* h/w works in units of "boot_cpu_data.x86_cache_occ_scale" */
	intel_cqm_threshold /= r->mon_scale;

	ret = dom_data_init(r);
	if (ret)
		return ret;

	l3_mon_evt_init(r);

	r->mon_capable = true;
	r->mon_enabled = true;

	return 0;
}
