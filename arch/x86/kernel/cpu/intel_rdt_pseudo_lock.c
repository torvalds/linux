// SPDX-License-Identifier: GPL-2.0
/*
 * Resource Director Technology (RDT)
 *
 * Pseudo-locking support built on top of Cache Allocation Technology (CAT)
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Reinette Chatre <reinette.chatre@intel.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include "intel_rdt.h"

/**
 * pseudo_lock_init - Initialize a pseudo-lock region
 * @rdtgrp: resource group to which new pseudo-locked region will belong
 *
 * A pseudo-locked region is associated with a resource group. When this
 * association is created the pseudo-locked region is initialized. The
 * details of the pseudo-locked region are not known at this time so only
 * allocation is done and association established.
 *
 * Return: 0 on success, <0 on failure
 */
static int pseudo_lock_init(struct rdtgroup *rdtgrp)
{
	struct pseudo_lock_region *plr;

	plr = kzalloc(sizeof(*plr), GFP_KERNEL);
	if (!plr)
		return -ENOMEM;

	rdtgrp->plr = plr;
	return 0;
}

/**
 * pseudo_lock_free - Free a pseudo-locked region
 * @rdtgrp: resource group to which pseudo-locked region belonged
 *
 * The pseudo-locked region's resources have already been released, or not
 * yet created at this point. Now it can be freed and disassociated from the
 * resource group.
 *
 * Return: void
 */
static void pseudo_lock_free(struct rdtgroup *rdtgrp)
{
	kfree(rdtgrp->plr);
	rdtgrp->plr = NULL;
}

/**
 * rdtgroup_monitor_in_progress - Test if monitoring in progress
 * @r: resource group being queried
 *
 * Return: 1 if monitor groups have been created for this resource
 * group, 0 otherwise.
 */
static int rdtgroup_monitor_in_progress(struct rdtgroup *rdtgrp)
{
	return !list_empty(&rdtgrp->mon.crdtgrp_list);
}

/**
 * rdtgroup_locksetup_user_restrict - Restrict user access to group
 * @rdtgrp: resource group needing access restricted
 *
 * A resource group used for cache pseudo-locking cannot have cpus or tasks
 * assigned to it. This is communicated to the user by restricting access
 * to all the files that can be used to make such changes.
 *
 * Permissions restored with rdtgroup_locksetup_user_restore()
 *
 * Return: 0 on success, <0 on failure. If a failure occurs during the
 * restriction of access an attempt will be made to restore permissions but
 * the state of the mode of these files will be uncertain when a failure
 * occurs.
 */
static int rdtgroup_locksetup_user_restrict(struct rdtgroup *rdtgrp)
{
	int ret;

	ret = rdtgroup_kn_mode_restrict(rdtgrp, "tasks");
	if (ret)
		return ret;

	ret = rdtgroup_kn_mode_restrict(rdtgrp, "cpus");
	if (ret)
		goto err_tasks;

	ret = rdtgroup_kn_mode_restrict(rdtgrp, "cpus_list");
	if (ret)
		goto err_cpus;

	if (rdt_mon_capable) {
		ret = rdtgroup_kn_mode_restrict(rdtgrp, "mon_groups");
		if (ret)
			goto err_cpus_list;
	}

	ret = 0;
	goto out;

err_cpus_list:
	rdtgroup_kn_mode_restore(rdtgrp, "cpus_list");
err_cpus:
	rdtgroup_kn_mode_restore(rdtgrp, "cpus");
err_tasks:
	rdtgroup_kn_mode_restore(rdtgrp, "tasks");
out:
	return ret;
}

/**
 * rdtgroup_locksetup_user_restore - Restore user access to group
 * @rdtgrp: resource group needing access restored
 *
 * Restore all file access previously removed using
 * rdtgroup_locksetup_user_restrict()
 *
 * Return: 0 on success, <0 on failure.  If a failure occurs during the
 * restoration of access an attempt will be made to restrict permissions
 * again but the state of the mode of these files will be uncertain when
 * a failure occurs.
 */
static int rdtgroup_locksetup_user_restore(struct rdtgroup *rdtgrp)
{
	int ret;

	ret = rdtgroup_kn_mode_restore(rdtgrp, "tasks");
	if (ret)
		return ret;

	ret = rdtgroup_kn_mode_restore(rdtgrp, "cpus");
	if (ret)
		goto err_tasks;

	ret = rdtgroup_kn_mode_restore(rdtgrp, "cpus_list");
	if (ret)
		goto err_cpus;

	if (rdt_mon_capable) {
		ret = rdtgroup_kn_mode_restore(rdtgrp, "mon_groups");
		if (ret)
			goto err_cpus_list;
	}

	ret = 0;
	goto out;

err_cpus_list:
	rdtgroup_kn_mode_restrict(rdtgrp, "cpus_list");
err_cpus:
	rdtgroup_kn_mode_restrict(rdtgrp, "cpus");
err_tasks:
	rdtgroup_kn_mode_restrict(rdtgrp, "tasks");
out:
	return ret;
}

/**
 * rdtgroup_locksetup_enter - Resource group enters locksetup mode
 * @rdtgrp: resource group requested to enter locksetup mode
 *
 * A resource group enters locksetup mode to reflect that it would be used
 * to represent a pseudo-locked region and is in the process of being set
 * up to do so. A resource group used for a pseudo-locked region would
 * lose the closid associated with it so we cannot allow it to have any
 * tasks or cpus assigned nor permit tasks or cpus to be assigned in the
 * future. Monitoring of a pseudo-locked region is not allowed either.
 *
 * The above and more restrictions on a pseudo-locked region are checked
 * for and enforced before the resource group enters the locksetup mode.
 *
 * Returns: 0 if the resource group successfully entered locksetup mode, <0
 * on failure. On failure the last_cmd_status buffer is updated with text to
 * communicate details of failure to the user.
 */
int rdtgroup_locksetup_enter(struct rdtgroup *rdtgrp)
{
	int ret;

	/*
	 * The default resource group can neither be removed nor lose the
	 * default closid associated with it.
	 */
	if (rdtgrp == &rdtgroup_default) {
		rdt_last_cmd_puts("cannot pseudo-lock default group\n");
		return -EINVAL;
	}

	/*
	 * Cache Pseudo-locking not supported when CDP is enabled.
	 *
	 * Some things to consider if you would like to enable this
	 * support (using L3 CDP as example):
	 * - When CDP is enabled two separate resources are exposed,
	 *   L3DATA and L3CODE, but they are actually on the same cache.
	 *   The implication for pseudo-locking is that if a
	 *   pseudo-locked region is created on a domain of one
	 *   resource (eg. L3CODE), then a pseudo-locked region cannot
	 *   be created on that same domain of the other resource
	 *   (eg. L3DATA). This is because the creation of a
	 *   pseudo-locked region involves a call to wbinvd that will
	 *   affect all cache allocations on particular domain.
	 * - Considering the previous, it may be possible to only
	 *   expose one of the CDP resources to pseudo-locking and
	 *   hide the other. For example, we could consider to only
	 *   expose L3DATA and since the L3 cache is unified it is
	 *   still possible to place instructions there are execute it.
	 * - If only one region is exposed to pseudo-locking we should
	 *   still keep in mind that availability of a portion of cache
	 *   for pseudo-locking should take into account both resources.
	 *   Similarly, if a pseudo-locked region is created in one
	 *   resource, the portion of cache used by it should be made
	 *   unavailable to all future allocations from both resources.
	 */
	if (rdt_resources_all[RDT_RESOURCE_L3DATA].alloc_enabled ||
	    rdt_resources_all[RDT_RESOURCE_L2DATA].alloc_enabled) {
		rdt_last_cmd_puts("CDP enabled\n");
		return -EINVAL;
	}

	if (rdtgroup_monitor_in_progress(rdtgrp)) {
		rdt_last_cmd_puts("monitoring in progress\n");
		return -EINVAL;
	}

	if (rdtgroup_tasks_assigned(rdtgrp)) {
		rdt_last_cmd_puts("tasks assigned to resource group\n");
		return -EINVAL;
	}

	if (!cpumask_empty(&rdtgrp->cpu_mask)) {
		rdt_last_cmd_puts("CPUs assigned to resource group\n");
		return -EINVAL;
	}

	if (rdtgroup_locksetup_user_restrict(rdtgrp)) {
		rdt_last_cmd_puts("unable to modify resctrl permissions\n");
		return -EIO;
	}

	ret = pseudo_lock_init(rdtgrp);
	if (ret) {
		rdt_last_cmd_puts("unable to init pseudo-lock region\n");
		goto out_release;
	}

	/*
	 * If this system is capable of monitoring a rmid would have been
	 * allocated when the control group was created. This is not needed
	 * anymore when this group would be used for pseudo-locking. This
	 * is safe to call on platforms not capable of monitoring.
	 */
	free_rmid(rdtgrp->mon.rmid);

	ret = 0;
	goto out;

out_release:
	rdtgroup_locksetup_user_restore(rdtgrp);
out:
	return ret;
}

/**
 * rdtgroup_locksetup_exit - resource group exist locksetup mode
 * @rdtgrp: resource group
 *
 * When a resource group exits locksetup mode the earlier restrictions are
 * lifted.
 *
 * Return: 0 on success, <0 on failure
 */
int rdtgroup_locksetup_exit(struct rdtgroup *rdtgrp)
{
	int ret;

	if (rdt_mon_capable) {
		ret = alloc_rmid();
		if (ret < 0) {
			rdt_last_cmd_puts("out of RMIDs\n");
			return ret;
		}
		rdtgrp->mon.rmid = ret;
	}

	ret = rdtgroup_locksetup_user_restore(rdtgrp);
	if (ret) {
		free_rmid(rdtgrp->mon.rmid);
		return ret;
	}

	pseudo_lock_free(rdtgrp);
	return 0;
}
