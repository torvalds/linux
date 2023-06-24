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

#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/perf_event.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/intel-family.h>
#include <asm/resctrl.h>
#include <asm/perf_event.h>

#include "../../events/perf_event.h" /* For X86_CONFIG() */
#include "internal.h"

#define CREATE_TRACE_POINTS
#include "pseudo_lock_event.h"

/*
 * The bits needed to disable hardware prefetching varies based on the
 * platform. During initialization we will discover which bits to use.
 */
static u64 prefetch_disable_bits;

/*
 * Major number assigned to and shared by all devices exposing
 * pseudo-locked regions.
 */
static unsigned int pseudo_lock_major;
static unsigned long pseudo_lock_minor_avail = GENMASK(MINORBITS, 0);
static struct class *pseudo_lock_class;

/**
 * get_prefetch_disable_bits - prefetch disable bits of supported platforms
 * @void: It takes no parameters.
 *
 * Capture the list of platforms that have been validated to support
 * pseudo-locking. This includes testing to ensure pseudo-locked regions
 * with low cache miss rates can be created under variety of load conditions
 * as well as that these pseudo-locked regions can maintain their low cache
 * miss rates under variety of load conditions for significant lengths of time.
 *
 * After a platform has been validated to support pseudo-locking its
 * hardware prefetch disable bits are included here as they are documented
 * in the SDM.
 *
 * When adding a platform here also add support for its cache events to
 * measure_cycles_perf_fn()
 *
 * Return:
 * If platform is supported, the bits to disable hardware prefetchers, 0
 * if platform is not supported.
 */
static u64 get_prefetch_disable_bits(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6)
		return 0;

	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_BROADWELL_X:
		/*
		 * SDM defines bits of MSR_MISC_FEATURE_CONTROL register
		 * as:
		 * 0    L2 Hardware Prefetcher Disable (R/W)
		 * 1    L2 Adjacent Cache Line Prefetcher Disable (R/W)
		 * 2    DCU Hardware Prefetcher Disable (R/W)
		 * 3    DCU IP Prefetcher Disable (R/W)
		 * 63:4 Reserved
		 */
		return 0xF;
	case INTEL_FAM6_ATOM_GOLDMONT:
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
		/*
		 * SDM defines bits of MSR_MISC_FEATURE_CONTROL register
		 * as:
		 * 0     L2 Hardware Prefetcher Disable (R/W)
		 * 1     Reserved
		 * 2     DCU Hardware Prefetcher Disable (R/W)
		 * 63:3  Reserved
		 */
		return 0x5;
	}

	return 0;
}

/**
 * pseudo_lock_minor_get - Obtain available minor number
 * @minor: Pointer to where new minor number will be stored
 *
 * A bitmask is used to track available minor numbers. Here the next free
 * minor number is marked as unavailable and returned.
 *
 * Return: 0 on success, <0 on failure.
 */
static int pseudo_lock_minor_get(unsigned int *minor)
{
	unsigned long first_bit;

	first_bit = find_first_bit(&pseudo_lock_minor_avail, MINORBITS);

	if (first_bit == MINORBITS)
		return -ENOSPC;

	__clear_bit(first_bit, &pseudo_lock_minor_avail);
	*minor = first_bit;

	return 0;
}

/**
 * pseudo_lock_minor_release - Return minor number to available
 * @minor: The minor number made available
 */
static void pseudo_lock_minor_release(unsigned int minor)
{
	__set_bit(minor, &pseudo_lock_minor_avail);
}

/**
 * region_find_by_minor - Locate a pseudo-lock region by inode minor number
 * @minor: The minor number of the device representing pseudo-locked region
 *
 * When the character device is accessed we need to determine which
 * pseudo-locked region it belongs to. This is done by matching the minor
 * number of the device to the pseudo-locked region it belongs.
 *
 * Minor numbers are assigned at the time a pseudo-locked region is associated
 * with a cache instance.
 *
 * Return: On success return pointer to resource group owning the pseudo-locked
 *         region, NULL on failure.
 */
static struct rdtgroup *region_find_by_minor(unsigned int minor)
{
	struct rdtgroup *rdtgrp, *rdtgrp_match = NULL;

	list_for_each_entry(rdtgrp, &rdt_all_groups, rdtgroup_list) {
		if (rdtgrp->plr && rdtgrp->plr->minor == minor) {
			rdtgrp_match = rdtgrp;
			break;
		}
	}
	return rdtgrp_match;
}

/**
 * struct pseudo_lock_pm_req - A power management QoS request list entry
 * @list:	Entry within the @pm_reqs list for a pseudo-locked region
 * @req:	PM QoS request
 */
struct pseudo_lock_pm_req {
	struct list_head list;
	struct dev_pm_qos_request req;
};

static void pseudo_lock_cstates_relax(struct pseudo_lock_region *plr)
{
	struct pseudo_lock_pm_req *pm_req, *next;

	list_for_each_entry_safe(pm_req, next, &plr->pm_reqs, list) {
		dev_pm_qos_remove_request(&pm_req->req);
		list_del(&pm_req->list);
		kfree(pm_req);
	}
}

/**
 * pseudo_lock_cstates_constrain - Restrict cores from entering C6
 * @plr: Pseudo-locked region
 *
 * To prevent the cache from being affected by power management entering
 * C6 has to be avoided. This is accomplished by requesting a latency
 * requirement lower than lowest C6 exit latency of all supported
 * platforms as found in the cpuidle state tables in the intel_idle driver.
 * At this time it is possible to do so with a single latency requirement
 * for all supported platforms.
 *
 * Since Goldmont is supported, which is affected by X86_BUG_MONITOR,
 * the ACPI latencies need to be considered while keeping in mind that C2
 * may be set to map to deeper sleep states. In this case the latency
 * requirement needs to prevent entering C2 also.
 *
 * Return: 0 on success, <0 on failure
 */
static int pseudo_lock_cstates_constrain(struct pseudo_lock_region *plr)
{
	struct pseudo_lock_pm_req *pm_req;
	int cpu;
	int ret;

	for_each_cpu(cpu, &plr->d->cpu_mask) {
		pm_req = kzalloc(sizeof(*pm_req), GFP_KERNEL);
		if (!pm_req) {
			rdt_last_cmd_puts("Failure to allocate memory for PM QoS\n");
			ret = -ENOMEM;
			goto out_err;
		}
		ret = dev_pm_qos_add_request(get_cpu_device(cpu),
					     &pm_req->req,
					     DEV_PM_QOS_RESUME_LATENCY,
					     30);
		if (ret < 0) {
			rdt_last_cmd_printf("Failed to add latency req CPU%d\n",
					    cpu);
			kfree(pm_req);
			ret = -1;
			goto out_err;
		}
		list_add(&pm_req->list, &plr->pm_reqs);
	}

	return 0;

out_err:
	pseudo_lock_cstates_relax(plr);
	return ret;
}

/**
 * pseudo_lock_region_clear - Reset pseudo-lock region data
 * @plr: pseudo-lock region
 *
 * All content of the pseudo-locked region is reset - any memory allocated
 * freed.
 *
 * Return: void
 */
static void pseudo_lock_region_clear(struct pseudo_lock_region *plr)
{
	plr->size = 0;
	plr->line_size = 0;
	kfree(plr->kmem);
	plr->kmem = NULL;
	plr->s = NULL;
	if (plr->d)
		plr->d->plr = NULL;
	plr->d = NULL;
	plr->cbm = 0;
	plr->debugfs_dir = NULL;
}

/**
 * pseudo_lock_region_init - Initialize pseudo-lock region information
 * @plr: pseudo-lock region
 *
 * Called after user provided a schemata to be pseudo-locked. From the
 * schemata the &struct pseudo_lock_region is on entry already initialized
 * with the resource, domain, and capacity bitmask. Here the information
 * required for pseudo-locking is deduced from this data and &struct
 * pseudo_lock_region initialized further. This information includes:
 * - size in bytes of the region to be pseudo-locked
 * - cache line size to know the stride with which data needs to be accessed
 *   to be pseudo-locked
 * - a cpu associated with the cache instance on which the pseudo-locking
 *   flow can be executed
 *
 * Return: 0 on success, <0 on failure. Descriptive error will be written
 * to last_cmd_status buffer.
 */
static int pseudo_lock_region_init(struct pseudo_lock_region *plr)
{
	struct cpu_cacheinfo *ci;
	int ret;
	int i;

	/* Pick the first cpu we find that is associated with the cache. */
	plr->cpu = cpumask_first(&plr->d->cpu_mask);

	if (!cpu_online(plr->cpu)) {
		rdt_last_cmd_printf("CPU %u associated with cache not online\n",
				    plr->cpu);
		ret = -ENODEV;
		goto out_region;
	}

	ci = get_cpu_cacheinfo(plr->cpu);

	plr->size = rdtgroup_cbm_to_size(plr->s->res, plr->d, plr->cbm);

	for (i = 0; i < ci->num_leaves; i++) {
		if (ci->info_list[i].level == plr->s->res->cache_level) {
			plr->line_size = ci->info_list[i].coherency_line_size;
			return 0;
		}
	}

	ret = -1;
	rdt_last_cmd_puts("Unable to determine cache line size\n");
out_region:
	pseudo_lock_region_clear(plr);
	return ret;
}

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

	init_waitqueue_head(&plr->lock_thread_wq);
	INIT_LIST_HEAD(&plr->pm_reqs);
	rdtgrp->plr = plr;
	return 0;
}

/**
 * pseudo_lock_region_alloc - Allocate kernel memory that will be pseudo-locked
 * @plr: pseudo-lock region
 *
 * Initialize the details required to set up the pseudo-locked region and
 * allocate the contiguous memory that will be pseudo-locked to the cache.
 *
 * Return: 0 on success, <0 on failure.  Descriptive error will be written
 * to last_cmd_status buffer.
 */
static int pseudo_lock_region_alloc(struct pseudo_lock_region *plr)
{
	int ret;

	ret = pseudo_lock_region_init(plr);
	if (ret < 0)
		return ret;

	/*
	 * We do not yet support contiguous regions larger than
	 * KMALLOC_MAX_SIZE.
	 */
	if (plr->size > KMALLOC_MAX_SIZE) {
		rdt_last_cmd_puts("Requested region exceeds maximum size\n");
		ret = -E2BIG;
		goto out_region;
	}

	plr->kmem = kzalloc(plr->size, GFP_KERNEL);
	if (!plr->kmem) {
		rdt_last_cmd_puts("Unable to allocate memory\n");
		ret = -ENOMEM;
		goto out_region;
	}

	ret = 0;
	goto out;
out_region:
	pseudo_lock_region_clear(plr);
out:
	return ret;
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
	pseudo_lock_region_clear(rdtgrp->plr);
	kfree(rdtgrp->plr);
	rdtgrp->plr = NULL;
}

/**
 * pseudo_lock_fn - Load kernel memory into cache
 * @_rdtgrp: resource group to which pseudo-lock region belongs
 *
 * This is the core pseudo-locking flow.
 *
 * First we ensure that the kernel memory cannot be found in the cache.
 * Then, while taking care that there will be as little interference as
 * possible, the memory to be loaded is accessed while core is running
 * with class of service set to the bitmask of the pseudo-locked region.
 * After this is complete no future CAT allocations will be allowed to
 * overlap with this bitmask.
 *
 * Local register variables are utilized to ensure that the memory region
 * to be locked is the only memory access made during the critical locking
 * loop.
 *
 * Return: 0. Waiter on waitqueue will be woken on completion.
 */
static int pseudo_lock_fn(void *_rdtgrp)
{
	struct rdtgroup *rdtgrp = _rdtgrp;
	struct pseudo_lock_region *plr = rdtgrp->plr;
	u32 rmid_p, closid_p;
	unsigned long i;
	u64 saved_msr;
#ifdef CONFIG_KASAN
	/*
	 * The registers used for local register variables are also used
	 * when KASAN is active. When KASAN is active we use a regular
	 * variable to ensure we always use a valid pointer, but the cost
	 * is that this variable will enter the cache through evicting the
	 * memory we are trying to lock into the cache. Thus expect lower
	 * pseudo-locking success rate when KASAN is active.
	 */
	unsigned int line_size;
	unsigned int size;
	void *mem_r;
#else
	register unsigned int line_size asm("esi");
	register unsigned int size asm("edi");
	register void *mem_r asm(_ASM_BX);
#endif /* CONFIG_KASAN */

	/*
	 * Make sure none of the allocated memory is cached. If it is we
	 * will get a cache hit in below loop from outside of pseudo-locked
	 * region.
	 * wbinvd (as opposed to clflush/clflushopt) is required to
	 * increase likelihood that allocated cache portion will be filled
	 * with associated memory.
	 */
	native_wbinvd();

	/*
	 * Always called with interrupts enabled. By disabling interrupts
	 * ensure that we will not be preempted during this critical section.
	 */
	local_irq_disable();

	/*
	 * Call wrmsr and rdmsr as directly as possible to avoid tracing
	 * clobbering local register variables or affecting cache accesses.
	 *
	 * Disable the hardware prefetcher so that when the end of the memory
	 * being pseudo-locked is reached the hardware will not read beyond
	 * the buffer and evict pseudo-locked memory read earlier from the
	 * cache.
	 */
	saved_msr = __rdmsr(MSR_MISC_FEATURE_CONTROL);
	__wrmsr(MSR_MISC_FEATURE_CONTROL, prefetch_disable_bits, 0x0);
	closid_p = this_cpu_read(pqr_state.cur_closid);
	rmid_p = this_cpu_read(pqr_state.cur_rmid);
	mem_r = plr->kmem;
	size = plr->size;
	line_size = plr->line_size;
	/*
	 * Critical section begin: start by writing the closid associated
	 * with the capacity bitmask of the cache region being
	 * pseudo-locked followed by reading of kernel memory to load it
	 * into the cache.
	 */
	__wrmsr(IA32_PQR_ASSOC, rmid_p, rdtgrp->closid);
	/*
	 * Cache was flushed earlier. Now access kernel memory to read it
	 * into cache region associated with just activated plr->closid.
	 * Loop over data twice:
	 * - In first loop the cache region is shared with the page walker
	 *   as it populates the paging structure caches (including TLB).
	 * - In the second loop the paging structure caches are used and
	 *   cache region is populated with the memory being referenced.
	 */
	for (i = 0; i < size; i += PAGE_SIZE) {
		/*
		 * Add a barrier to prevent speculative execution of this
		 * loop reading beyond the end of the buffer.
		 */
		rmb();
		asm volatile("mov (%0,%1,1), %%eax\n\t"
			:
			: "r" (mem_r), "r" (i)
			: "%eax", "memory");
	}
	for (i = 0; i < size; i += line_size) {
		/*
		 * Add a barrier to prevent speculative execution of this
		 * loop reading beyond the end of the buffer.
		 */
		rmb();
		asm volatile("mov (%0,%1,1), %%eax\n\t"
			:
			: "r" (mem_r), "r" (i)
			: "%eax", "memory");
	}
	/*
	 * Critical section end: restore closid with capacity bitmask that
	 * does not overlap with pseudo-locked region.
	 */
	__wrmsr(IA32_PQR_ASSOC, rmid_p, closid_p);

	/* Re-enable the hardware prefetcher(s) */
	wrmsrl(MSR_MISC_FEATURE_CONTROL, saved_msr);
	local_irq_enable();

	plr->thread_done = 1;
	wake_up_interruptible(&plr->lock_thread_wq);
	return 0;
}

/**
 * rdtgroup_monitor_in_progress - Test if monitoring in progress
 * @rdtgrp: resource group being queried
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
	rdtgroup_kn_mode_restore(rdtgrp, "cpus_list", 0777);
err_cpus:
	rdtgroup_kn_mode_restore(rdtgrp, "cpus", 0777);
err_tasks:
	rdtgroup_kn_mode_restore(rdtgrp, "tasks", 0777);
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

	ret = rdtgroup_kn_mode_restore(rdtgrp, "tasks", 0777);
	if (ret)
		return ret;

	ret = rdtgroup_kn_mode_restore(rdtgrp, "cpus", 0777);
	if (ret)
		goto err_tasks;

	ret = rdtgroup_kn_mode_restore(rdtgrp, "cpus_list", 0777);
	if (ret)
		goto err_cpus;

	if (rdt_mon_capable) {
		ret = rdtgroup_kn_mode_restore(rdtgrp, "mon_groups", 0777);
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
		rdt_last_cmd_puts("Cannot pseudo-lock default group\n");
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
	if (resctrl_arch_get_cdp_enabled(RDT_RESOURCE_L3) ||
	    resctrl_arch_get_cdp_enabled(RDT_RESOURCE_L2)) {
		rdt_last_cmd_puts("CDP enabled\n");
		return -EINVAL;
	}

	/*
	 * Not knowing the bits to disable prefetching implies that this
	 * platform does not support Cache Pseudo-Locking.
	 */
	prefetch_disable_bits = get_prefetch_disable_bits();
	if (prefetch_disable_bits == 0) {
		rdt_last_cmd_puts("Pseudo-locking not supported\n");
		return -EINVAL;
	}

	if (rdtgroup_monitor_in_progress(rdtgrp)) {
		rdt_last_cmd_puts("Monitoring in progress\n");
		return -EINVAL;
	}

	if (rdtgroup_tasks_assigned(rdtgrp)) {
		rdt_last_cmd_puts("Tasks assigned to resource group\n");
		return -EINVAL;
	}

	if (!cpumask_empty(&rdtgrp->cpu_mask)) {
		rdt_last_cmd_puts("CPUs assigned to resource group\n");
		return -EINVAL;
	}

	if (rdtgroup_locksetup_user_restrict(rdtgrp)) {
		rdt_last_cmd_puts("Unable to modify resctrl permissions\n");
		return -EIO;
	}

	ret = pseudo_lock_init(rdtgrp);
	if (ret) {
		rdt_last_cmd_puts("Unable to init pseudo-lock region\n");
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
			rdt_last_cmd_puts("Out of RMIDs\n");
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

/**
 * rdtgroup_cbm_overlaps_pseudo_locked - Test if CBM or portion is pseudo-locked
 * @d: RDT domain
 * @cbm: CBM to test
 *
 * @d represents a cache instance and @cbm a capacity bitmask that is
 * considered for it. Determine if @cbm overlaps with any existing
 * pseudo-locked region on @d.
 *
 * @cbm is unsigned long, even if only 32 bits are used, to make the
 * bitmap functions work correctly.
 *
 * Return: true if @cbm overlaps with pseudo-locked region on @d, false
 * otherwise.
 */
bool rdtgroup_cbm_overlaps_pseudo_locked(struct rdt_domain *d, unsigned long cbm)
{
	unsigned int cbm_len;
	unsigned long cbm_b;

	if (d->plr) {
		cbm_len = d->plr->s->res->cache.cbm_len;
		cbm_b = d->plr->cbm;
		if (bitmap_intersects(&cbm, &cbm_b, cbm_len))
			return true;
	}
	return false;
}

/**
 * rdtgroup_pseudo_locked_in_hierarchy - Pseudo-locked region in cache hierarchy
 * @d: RDT domain under test
 *
 * The setup of a pseudo-locked region affects all cache instances within
 * the hierarchy of the region. It is thus essential to know if any
 * pseudo-locked regions exist within a cache hierarchy to prevent any
 * attempts to create new pseudo-locked regions in the same hierarchy.
 *
 * Return: true if a pseudo-locked region exists in the hierarchy of @d or
 *         if it is not possible to test due to memory allocation issue,
 *         false otherwise.
 */
bool rdtgroup_pseudo_locked_in_hierarchy(struct rdt_domain *d)
{
	cpumask_var_t cpu_with_psl;
	struct rdt_resource *r;
	struct rdt_domain *d_i;
	bool ret = false;

	if (!zalloc_cpumask_var(&cpu_with_psl, GFP_KERNEL))
		return true;

	/*
	 * First determine which cpus have pseudo-locked regions
	 * associated with them.
	 */
	for_each_alloc_capable_rdt_resource(r) {
		list_for_each_entry(d_i, &r->domains, list) {
			if (d_i->plr)
				cpumask_or(cpu_with_psl, cpu_with_psl,
					   &d_i->cpu_mask);
		}
	}

	/*
	 * Next test if new pseudo-locked region would intersect with
	 * existing region.
	 */
	if (cpumask_intersects(&d->cpu_mask, cpu_with_psl))
		ret = true;

	free_cpumask_var(cpu_with_psl);
	return ret;
}

/**
 * measure_cycles_lat_fn - Measure cycle latency to read pseudo-locked memory
 * @_plr: pseudo-lock region to measure
 *
 * There is no deterministic way to test if a memory region is cached. One
 * way is to measure how long it takes to read the memory, the speed of
 * access is a good way to learn how close to the cpu the data was. Even
 * more, if the prefetcher is disabled and the memory is read at a stride
 * of half the cache line, then a cache miss will be easy to spot since the
 * read of the first half would be significantly slower than the read of
 * the second half.
 *
 * Return: 0. Waiter on waitqueue will be woken on completion.
 */
static int measure_cycles_lat_fn(void *_plr)
{
	struct pseudo_lock_region *plr = _plr;
	u32 saved_low, saved_high;
	unsigned long i;
	u64 start, end;
	void *mem_r;

	local_irq_disable();
	/*
	 * Disable hardware prefetchers.
	 */
	rdmsr(MSR_MISC_FEATURE_CONTROL, saved_low, saved_high);
	wrmsr(MSR_MISC_FEATURE_CONTROL, prefetch_disable_bits, 0x0);
	mem_r = READ_ONCE(plr->kmem);
	/*
	 * Dummy execute of the time measurement to load the needed
	 * instructions into the L1 instruction cache.
	 */
	start = rdtsc_ordered();
	for (i = 0; i < plr->size; i += 32) {
		start = rdtsc_ordered();
		asm volatile("mov (%0,%1,1), %%eax\n\t"
			     :
			     : "r" (mem_r), "r" (i)
			     : "%eax", "memory");
		end = rdtsc_ordered();
		trace_pseudo_lock_mem_latency((u32)(end - start));
	}
	wrmsr(MSR_MISC_FEATURE_CONTROL, saved_low, saved_high);
	local_irq_enable();
	plr->thread_done = 1;
	wake_up_interruptible(&plr->lock_thread_wq);
	return 0;
}

/*
 * Create a perf_event_attr for the hit and miss perf events that will
 * be used during the performance measurement. A perf_event maintains
 * a pointer to its perf_event_attr so a unique attribute structure is
 * created for each perf_event.
 *
 * The actual configuration of the event is set right before use in order
 * to use the X86_CONFIG macro.
 */
static struct perf_event_attr perf_miss_attr = {
	.type		= PERF_TYPE_RAW,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 0,
	.exclude_user	= 1,
};

static struct perf_event_attr perf_hit_attr = {
	.type		= PERF_TYPE_RAW,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 0,
	.exclude_user	= 1,
};

struct residency_counts {
	u64 miss_before, hits_before;
	u64 miss_after,  hits_after;
};

static int measure_residency_fn(struct perf_event_attr *miss_attr,
				struct perf_event_attr *hit_attr,
				struct pseudo_lock_region *plr,
				struct residency_counts *counts)
{
	u64 hits_before = 0, hits_after = 0, miss_before = 0, miss_after = 0;
	struct perf_event *miss_event, *hit_event;
	int hit_pmcnum, miss_pmcnum;
	u32 saved_low, saved_high;
	unsigned int line_size;
	unsigned int size;
	unsigned long i;
	void *mem_r;
	u64 tmp;

	miss_event = perf_event_create_kernel_counter(miss_attr, plr->cpu,
						      NULL, NULL, NULL);
	if (IS_ERR(miss_event))
		goto out;

	hit_event = perf_event_create_kernel_counter(hit_attr, plr->cpu,
						     NULL, NULL, NULL);
	if (IS_ERR(hit_event))
		goto out_miss;

	local_irq_disable();
	/*
	 * Check any possible error state of events used by performing
	 * one local read.
	 */
	if (perf_event_read_local(miss_event, &tmp, NULL, NULL)) {
		local_irq_enable();
		goto out_hit;
	}
	if (perf_event_read_local(hit_event, &tmp, NULL, NULL)) {
		local_irq_enable();
		goto out_hit;
	}

	/*
	 * Disable hardware prefetchers.
	 */
	rdmsr(MSR_MISC_FEATURE_CONTROL, saved_low, saved_high);
	wrmsr(MSR_MISC_FEATURE_CONTROL, prefetch_disable_bits, 0x0);

	/* Initialize rest of local variables */
	/*
	 * Performance event has been validated right before this with
	 * interrupts disabled - it is thus safe to read the counter index.
	 */
	miss_pmcnum = x86_perf_rdpmc_index(miss_event);
	hit_pmcnum = x86_perf_rdpmc_index(hit_event);
	line_size = READ_ONCE(plr->line_size);
	mem_r = READ_ONCE(plr->kmem);
	size = READ_ONCE(plr->size);

	/*
	 * Read counter variables twice - first to load the instructions
	 * used in L1 cache, second to capture accurate value that does not
	 * include cache misses incurred because of instruction loads.
	 */
	rdpmcl(hit_pmcnum, hits_before);
	rdpmcl(miss_pmcnum, miss_before);
	/*
	 * From SDM: Performing back-to-back fast reads are not guaranteed
	 * to be monotonic.
	 * Use LFENCE to ensure all previous instructions are retired
	 * before proceeding.
	 */
	rmb();
	rdpmcl(hit_pmcnum, hits_before);
	rdpmcl(miss_pmcnum, miss_before);
	/*
	 * Use LFENCE to ensure all previous instructions are retired
	 * before proceeding.
	 */
	rmb();
	for (i = 0; i < size; i += line_size) {
		/*
		 * Add a barrier to prevent speculative execution of this
		 * loop reading beyond the end of the buffer.
		 */
		rmb();
		asm volatile("mov (%0,%1,1), %%eax\n\t"
			     :
			     : "r" (mem_r), "r" (i)
			     : "%eax", "memory");
	}
	/*
	 * Use LFENCE to ensure all previous instructions are retired
	 * before proceeding.
	 */
	rmb();
	rdpmcl(hit_pmcnum, hits_after);
	rdpmcl(miss_pmcnum, miss_after);
	/*
	 * Use LFENCE to ensure all previous instructions are retired
	 * before proceeding.
	 */
	rmb();
	/* Re-enable hardware prefetchers */
	wrmsr(MSR_MISC_FEATURE_CONTROL, saved_low, saved_high);
	local_irq_enable();
out_hit:
	perf_event_release_kernel(hit_event);
out_miss:
	perf_event_release_kernel(miss_event);
out:
	/*
	 * All counts will be zero on failure.
	 */
	counts->miss_before = miss_before;
	counts->hits_before = hits_before;
	counts->miss_after  = miss_after;
	counts->hits_after  = hits_after;
	return 0;
}

static int measure_l2_residency(void *_plr)
{
	struct pseudo_lock_region *plr = _plr;
	struct residency_counts counts = {0};

	/*
	 * Non-architectural event for the Goldmont Microarchitecture
	 * from Intel x86 Architecture Software Developer Manual (SDM):
	 * MEM_LOAD_UOPS_RETIRED D1H (event number)
	 * Umask values:
	 *     L2_HIT   02H
	 *     L2_MISS  10H
	 */
	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_ATOM_GOLDMONT:
	case INTEL_FAM6_ATOM_GOLDMONT_PLUS:
		perf_miss_attr.config = X86_CONFIG(.event = 0xd1,
						   .umask = 0x10);
		perf_hit_attr.config = X86_CONFIG(.event = 0xd1,
						  .umask = 0x2);
		break;
	default:
		goto out;
	}

	measure_residency_fn(&perf_miss_attr, &perf_hit_attr, plr, &counts);
	/*
	 * If a failure prevented the measurements from succeeding
	 * tracepoints will still be written and all counts will be zero.
	 */
	trace_pseudo_lock_l2(counts.hits_after - counts.hits_before,
			     counts.miss_after - counts.miss_before);
out:
	plr->thread_done = 1;
	wake_up_interruptible(&plr->lock_thread_wq);
	return 0;
}

static int measure_l3_residency(void *_plr)
{
	struct pseudo_lock_region *plr = _plr;
	struct residency_counts counts = {0};

	/*
	 * On Broadwell Microarchitecture the MEM_LOAD_UOPS_RETIRED event
	 * has two "no fix" errata associated with it: BDM35 and BDM100. On
	 * this platform the following events are used instead:
	 * LONGEST_LAT_CACHE 2EH (Documented in SDM)
	 *       REFERENCE 4FH
	 *       MISS      41H
	 */

	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_BROADWELL_X:
		/* On BDW the hit event counts references, not hits */
		perf_hit_attr.config = X86_CONFIG(.event = 0x2e,
						  .umask = 0x4f);
		perf_miss_attr.config = X86_CONFIG(.event = 0x2e,
						   .umask = 0x41);
		break;
	default:
		goto out;
	}

	measure_residency_fn(&perf_miss_attr, &perf_hit_attr, plr, &counts);
	/*
	 * If a failure prevented the measurements from succeeding
	 * tracepoints will still be written and all counts will be zero.
	 */

	counts.miss_after -= counts.miss_before;
	if (boot_cpu_data.x86_model == INTEL_FAM6_BROADWELL_X) {
		/*
		 * On BDW references and misses are counted, need to adjust.
		 * Sometimes the "hits" counter is a bit more than the
		 * references, for example, x references but x + 1 hits.
		 * To not report invalid hit values in this case we treat
		 * that as misses equal to references.
		 */
		/* First compute the number of cache references measured */
		counts.hits_after -= counts.hits_before;
		/* Next convert references to cache hits */
		counts.hits_after -= min(counts.miss_after, counts.hits_after);
	} else {
		counts.hits_after -= counts.hits_before;
	}

	trace_pseudo_lock_l3(counts.hits_after, counts.miss_after);
out:
	plr->thread_done = 1;
	wake_up_interruptible(&plr->lock_thread_wq);
	return 0;
}

/**
 * pseudo_lock_measure_cycles - Trigger latency measure to pseudo-locked region
 * @rdtgrp: Resource group to which the pseudo-locked region belongs.
 * @sel: Selector of which measurement to perform on a pseudo-locked region.
 *
 * The measurement of latency to access a pseudo-locked region should be
 * done from a cpu that is associated with that pseudo-locked region.
 * Determine which cpu is associated with this region and start a thread on
 * that cpu to perform the measurement, wait for that thread to complete.
 *
 * Return: 0 on success, <0 on failure
 */
static int pseudo_lock_measure_cycles(struct rdtgroup *rdtgrp, int sel)
{
	struct pseudo_lock_region *plr = rdtgrp->plr;
	struct task_struct *thread;
	unsigned int cpu;
	int ret = -1;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	if (rdtgrp->flags & RDT_DELETED) {
		ret = -ENODEV;
		goto out;
	}

	if (!plr->d) {
		ret = -ENODEV;
		goto out;
	}

	plr->thread_done = 0;
	cpu = cpumask_first(&plr->d->cpu_mask);
	if (!cpu_online(cpu)) {
		ret = -ENODEV;
		goto out;
	}

	plr->cpu = cpu;

	if (sel == 1)
		thread = kthread_create_on_node(measure_cycles_lat_fn, plr,
						cpu_to_node(cpu),
						"pseudo_lock_measure/%u",
						cpu);
	else if (sel == 2)
		thread = kthread_create_on_node(measure_l2_residency, plr,
						cpu_to_node(cpu),
						"pseudo_lock_measure/%u",
						cpu);
	else if (sel == 3)
		thread = kthread_create_on_node(measure_l3_residency, plr,
						cpu_to_node(cpu),
						"pseudo_lock_measure/%u",
						cpu);
	else
		goto out;

	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		goto out;
	}
	kthread_bind(thread, cpu);
	wake_up_process(thread);

	ret = wait_event_interruptible(plr->lock_thread_wq,
				       plr->thread_done == 1);
	if (ret < 0)
		goto out;

	ret = 0;

out:
	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
	return ret;
}

static ssize_t pseudo_lock_measure_trigger(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct rdtgroup *rdtgrp = file->private_data;
	size_t buf_size;
	char buf[32];
	int ret;
	int sel;

	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';
	ret = kstrtoint(buf, 10, &sel);
	if (ret == 0) {
		if (sel != 1 && sel != 2 && sel != 3)
			return -EINVAL;
		ret = debugfs_file_get(file->f_path.dentry);
		if (ret)
			return ret;
		ret = pseudo_lock_measure_cycles(rdtgrp, sel);
		if (ret == 0)
			ret = count;
		debugfs_file_put(file->f_path.dentry);
	}

	return ret;
}

static const struct file_operations pseudo_measure_fops = {
	.write = pseudo_lock_measure_trigger,
	.open = simple_open,
	.llseek = default_llseek,
};

/**
 * rdtgroup_pseudo_lock_create - Create a pseudo-locked region
 * @rdtgrp: resource group to which pseudo-lock region belongs
 *
 * Called when a resource group in the pseudo-locksetup mode receives a
 * valid schemata that should be pseudo-locked. Since the resource group is
 * in pseudo-locksetup mode the &struct pseudo_lock_region has already been
 * allocated and initialized with the essential information. If a failure
 * occurs the resource group remains in the pseudo-locksetup mode with the
 * &struct pseudo_lock_region associated with it, but cleared from all
 * information and ready for the user to re-attempt pseudo-locking by
 * writing the schemata again.
 *
 * Return: 0 if the pseudo-locked region was successfully pseudo-locked, <0
 * on failure. Descriptive error will be written to last_cmd_status buffer.
 */
int rdtgroup_pseudo_lock_create(struct rdtgroup *rdtgrp)
{
	struct pseudo_lock_region *plr = rdtgrp->plr;
	struct task_struct *thread;
	unsigned int new_minor;
	struct device *dev;
	int ret;

	ret = pseudo_lock_region_alloc(plr);
	if (ret < 0)
		return ret;

	ret = pseudo_lock_cstates_constrain(plr);
	if (ret < 0) {
		ret = -EINVAL;
		goto out_region;
	}

	plr->thread_done = 0;

	thread = kthread_create_on_node(pseudo_lock_fn, rdtgrp,
					cpu_to_node(plr->cpu),
					"pseudo_lock/%u", plr->cpu);
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		rdt_last_cmd_printf("Locking thread returned error %d\n", ret);
		goto out_cstates;
	}

	kthread_bind(thread, plr->cpu);
	wake_up_process(thread);

	ret = wait_event_interruptible(plr->lock_thread_wq,
				       plr->thread_done == 1);
	if (ret < 0) {
		/*
		 * If the thread does not get on the CPU for whatever
		 * reason and the process which sets up the region is
		 * interrupted then this will leave the thread in runnable
		 * state and once it gets on the CPU it will dereference
		 * the cleared, but not freed, plr struct resulting in an
		 * empty pseudo-locking loop.
		 */
		rdt_last_cmd_puts("Locking thread interrupted\n");
		goto out_cstates;
	}

	ret = pseudo_lock_minor_get(&new_minor);
	if (ret < 0) {
		rdt_last_cmd_puts("Unable to obtain a new minor number\n");
		goto out_cstates;
	}

	/*
	 * Unlock access but do not release the reference. The
	 * pseudo-locked region will still be here on return.
	 *
	 * The mutex has to be released temporarily to avoid a potential
	 * deadlock with the mm->mmap_lock which is obtained in the
	 * device_create() and debugfs_create_dir() callpath below as well as
	 * before the mmap() callback is called.
	 */
	mutex_unlock(&rdtgroup_mutex);

	if (!IS_ERR_OR_NULL(debugfs_resctrl)) {
		plr->debugfs_dir = debugfs_create_dir(rdtgrp->kn->name,
						      debugfs_resctrl);
		if (!IS_ERR_OR_NULL(plr->debugfs_dir))
			debugfs_create_file("pseudo_lock_measure", 0200,
					    plr->debugfs_dir, rdtgrp,
					    &pseudo_measure_fops);
	}

	dev = device_create(pseudo_lock_class, NULL,
			    MKDEV(pseudo_lock_major, new_minor),
			    rdtgrp, "%s", rdtgrp->kn->name);

	mutex_lock(&rdtgroup_mutex);

	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		rdt_last_cmd_printf("Failed to create character device: %d\n",
				    ret);
		goto out_debugfs;
	}

	/* We released the mutex - check if group was removed while we did so */
	if (rdtgrp->flags & RDT_DELETED) {
		ret = -ENODEV;
		goto out_device;
	}

	plr->minor = new_minor;

	rdtgrp->mode = RDT_MODE_PSEUDO_LOCKED;
	closid_free(rdtgrp->closid);
	rdtgroup_kn_mode_restore(rdtgrp, "cpus", 0444);
	rdtgroup_kn_mode_restore(rdtgrp, "cpus_list", 0444);

	ret = 0;
	goto out;

out_device:
	device_destroy(pseudo_lock_class, MKDEV(pseudo_lock_major, new_minor));
out_debugfs:
	debugfs_remove_recursive(plr->debugfs_dir);
	pseudo_lock_minor_release(new_minor);
out_cstates:
	pseudo_lock_cstates_relax(plr);
out_region:
	pseudo_lock_region_clear(plr);
out:
	return ret;
}

/**
 * rdtgroup_pseudo_lock_remove - Remove a pseudo-locked region
 * @rdtgrp: resource group to which the pseudo-locked region belongs
 *
 * The removal of a pseudo-locked region can be initiated when the resource
 * group is removed from user space via a "rmdir" from userspace or the
 * unmount of the resctrl filesystem. On removal the resource group does
 * not go back to pseudo-locksetup mode before it is removed, instead it is
 * removed directly. There is thus asymmetry with the creation where the
 * &struct pseudo_lock_region is removed here while it was not created in
 * rdtgroup_pseudo_lock_create().
 *
 * Return: void
 */
void rdtgroup_pseudo_lock_remove(struct rdtgroup *rdtgrp)
{
	struct pseudo_lock_region *plr = rdtgrp->plr;

	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP) {
		/*
		 * Default group cannot be a pseudo-locked region so we can
		 * free closid here.
		 */
		closid_free(rdtgrp->closid);
		goto free;
	}

	pseudo_lock_cstates_relax(plr);
	debugfs_remove_recursive(rdtgrp->plr->debugfs_dir);
	device_destroy(pseudo_lock_class, MKDEV(pseudo_lock_major, plr->minor));
	pseudo_lock_minor_release(plr->minor);

free:
	pseudo_lock_free(rdtgrp);
}

static int pseudo_lock_dev_open(struct inode *inode, struct file *filp)
{
	struct rdtgroup *rdtgrp;

	mutex_lock(&rdtgroup_mutex);

	rdtgrp = region_find_by_minor(iminor(inode));
	if (!rdtgrp) {
		mutex_unlock(&rdtgroup_mutex);
		return -ENODEV;
	}

	filp->private_data = rdtgrp;
	atomic_inc(&rdtgrp->waitcount);
	/* Perform a non-seekable open - llseek is not supported */
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);

	mutex_unlock(&rdtgroup_mutex);

	return 0;
}

static int pseudo_lock_dev_release(struct inode *inode, struct file *filp)
{
	struct rdtgroup *rdtgrp;

	mutex_lock(&rdtgroup_mutex);
	rdtgrp = filp->private_data;
	WARN_ON(!rdtgrp);
	if (!rdtgrp) {
		mutex_unlock(&rdtgroup_mutex);
		return -ENODEV;
	}
	filp->private_data = NULL;
	atomic_dec(&rdtgrp->waitcount);
	mutex_unlock(&rdtgroup_mutex);
	return 0;
}

static int pseudo_lock_dev_mremap(struct vm_area_struct *area)
{
	/* Not supported */
	return -EINVAL;
}

static const struct vm_operations_struct pseudo_mmap_ops = {
	.mremap = pseudo_lock_dev_mremap,
};

static int pseudo_lock_dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
	struct pseudo_lock_region *plr;
	struct rdtgroup *rdtgrp;
	unsigned long physical;
	unsigned long psize;

	mutex_lock(&rdtgroup_mutex);

	rdtgrp = filp->private_data;
	WARN_ON(!rdtgrp);
	if (!rdtgrp) {
		mutex_unlock(&rdtgroup_mutex);
		return -ENODEV;
	}

	plr = rdtgrp->plr;

	if (!plr->d) {
		mutex_unlock(&rdtgroup_mutex);
		return -ENODEV;
	}

	/*
	 * Task is required to run with affinity to the cpus associated
	 * with the pseudo-locked region. If this is not the case the task
	 * may be scheduled elsewhere and invalidate entries in the
	 * pseudo-locked region.
	 */
	if (!cpumask_subset(current->cpus_ptr, &plr->d->cpu_mask)) {
		mutex_unlock(&rdtgroup_mutex);
		return -EINVAL;
	}

	physical = __pa(plr->kmem) >> PAGE_SHIFT;
	psize = plr->size - off;

	if (off > plr->size) {
		mutex_unlock(&rdtgroup_mutex);
		return -ENOSPC;
	}

	/*
	 * Ensure changes are carried directly to the memory being mapped,
	 * do not allow copy-on-write mapping.
	 */
	if (!(vma->vm_flags & VM_SHARED)) {
		mutex_unlock(&rdtgroup_mutex);
		return -EINVAL;
	}

	if (vsize > psize) {
		mutex_unlock(&rdtgroup_mutex);
		return -ENOSPC;
	}

	memset(plr->kmem + off, 0, vsize);

	if (remap_pfn_range(vma, vma->vm_start, physical + vma->vm_pgoff,
			    vsize, vma->vm_page_prot)) {
		mutex_unlock(&rdtgroup_mutex);
		return -EAGAIN;
	}
	vma->vm_ops = &pseudo_mmap_ops;
	mutex_unlock(&rdtgroup_mutex);
	return 0;
}

static const struct file_operations pseudo_lock_dev_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		NULL,
	.write =	NULL,
	.open =		pseudo_lock_dev_open,
	.release =	pseudo_lock_dev_release,
	.mmap =		pseudo_lock_dev_mmap,
};

static char *pseudo_lock_devnode(struct device *dev, umode_t *mode)
{
	struct rdtgroup *rdtgrp;

	rdtgrp = dev_get_drvdata(dev);
	if (mode)
		*mode = 0600;
	return kasprintf(GFP_KERNEL, "pseudo_lock/%s", rdtgrp->kn->name);
}

int rdt_pseudo_lock_init(void)
{
	int ret;

	ret = register_chrdev(0, "pseudo_lock", &pseudo_lock_dev_fops);
	if (ret < 0)
		return ret;

	pseudo_lock_major = ret;

	pseudo_lock_class = class_create(THIS_MODULE, "pseudo_lock");
	if (IS_ERR(pseudo_lock_class)) {
		ret = PTR_ERR(pseudo_lock_class);
		unregister_chrdev(pseudo_lock_major, "pseudo_lock");
		return ret;
	}

	pseudo_lock_class->devnode = pseudo_lock_devnode;
	return 0;
}

void rdt_pseudo_lock_release(void)
{
	class_destroy(pseudo_lock_class);
	pseudo_lock_class = NULL;
	unregister_chrdev(pseudo_lock_major, "pseudo_lock");
	pseudo_lock_major = 0;
}
