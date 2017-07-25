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

struct rmid_entry {
	u32				rmid;
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
