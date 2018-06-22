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
#include <linux/kthread.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/intel-family.h>
#include <asm/intel_rdt_sched.h>
#include "intel_rdt.h"

/*
 * MSR_MISC_FEATURE_CONTROL register enables the modification of hardware
 * prefetcher state. Details about this register can be found in the MSR
 * tables for specific platforms found in Intel's SDM.
 */
#define MSR_MISC_FEATURE_CONTROL	0x000001a4

/*
 * The bits needed to disable hardware prefetching varies based on the
 * platform. During initialization we will discover which bits to use.
 */
static u64 prefetch_disable_bits;

/**
 * get_prefetch_disable_bits - prefetch disable bits of supported platforms
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
	case INTEL_FAM6_ATOM_GEMINI_LAKE:
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
	int i;

	/* Pick the first cpu we find that is associated with the cache. */
	plr->cpu = cpumask_first(&plr->d->cpu_mask);

	if (!cpu_online(plr->cpu)) {
		rdt_last_cmd_printf("cpu %u associated with cache not online\n",
				    plr->cpu);
		return -ENODEV;
	}

	ci = get_cpu_cacheinfo(plr->cpu);

	plr->size = rdtgroup_cbm_to_size(plr->r, plr->d, plr->cbm);

	for (i = 0; i < ci->num_leaves; i++) {
		if (ci->info_list[i].level == plr->r->cache_level) {
			plr->line_size = ci->info_list[i].coherency_line_size;
			return 0;
		}
	}

	rdt_last_cmd_puts("unable to determine cache line size\n");
	return -1;
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
	rdtgrp->plr = plr;
	return 0;
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
	plr->r = NULL;
	if (plr->d)
		plr->d->plr = NULL;
	plr->d = NULL;
	plr->cbm = 0;
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
		rdt_last_cmd_puts("requested region exceeds maximum size\n");
		return -E2BIG;
	}

	plr->kmem = kzalloc(plr->size, GFP_KERNEL);
	if (!plr->kmem) {
		rdt_last_cmd_puts("unable to allocate memory\n");
		return -ENOMEM;
	}

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
#ifdef CONFIG_X86_64
	register void *mem_r asm("rbx");
#else
	register void *mem_r asm("ebx");
#endif /* CONFIG_X86_64 */
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
	wrmsr(MSR_MISC_FEATURE_CONTROL, 0x0, 0x0);
	local_irq_enable();

	plr->thread_done = 1;
	wake_up_interruptible(&plr->lock_thread_wq);
	return 0;
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

	/*
	 * Not knowing the bits to disable prefetching implies that this
	 * platform does not support Cache Pseudo-Locking.
	 */
	prefetch_disable_bits = get_prefetch_disable_bits();
	if (prefetch_disable_bits == 0) {
		rdt_last_cmd_puts("pseudo-locking not supported\n");
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

/**
 * rdtgroup_cbm_overlaps_pseudo_locked - Test if CBM or portion is pseudo-locked
 * @d: RDT domain
 * @_cbm: CBM to test
 *
 * @d represents a cache instance and @_cbm a capacity bitmask that is
 * considered for it. Determine if @_cbm overlaps with any existing
 * pseudo-locked region on @d.
 *
 * Return: true if @_cbm overlaps with pseudo-locked region on @d, false
 * otherwise.
 */
bool rdtgroup_cbm_overlaps_pseudo_locked(struct rdt_domain *d, u32 _cbm)
{
	unsigned long *cbm = (unsigned long *)&_cbm;
	unsigned long *cbm_b;
	unsigned int cbm_len;

	if (d->plr) {
		cbm_len = d->plr->r->cache.cbm_len;
		cbm_b = (unsigned long *)&d->plr->cbm;
		if (bitmap_intersects(cbm, cbm_b, cbm_len))
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
	for_each_alloc_enabled_rdt_resource(r) {
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
	int ret;

	ret = pseudo_lock_region_alloc(plr);
	if (ret < 0)
		return ret;

	plr->thread_done = 0;

	thread = kthread_create_on_node(pseudo_lock_fn, rdtgrp,
					cpu_to_node(plr->cpu),
					"pseudo_lock/%u", plr->cpu);
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		rdt_last_cmd_printf("locking thread returned error %d\n", ret);
		goto out_region;
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
		 * state and once it gets on the CPU it will derefence
		 * the cleared, but not freed, plr struct resulting in an
		 * empty pseudo-locking loop.
		 */
		rdt_last_cmd_puts("locking thread interrupted\n");
		goto out_region;
	}

	rdtgrp->mode = RDT_MODE_PSEUDO_LOCKED;
	closid_free(rdtgrp->closid);
	ret = 0;
	goto out;

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
 * removed directly. There is thus assymmetry with the creation where the
 * &struct pseudo_lock_region is removed here while it was not created in
 * rdtgroup_pseudo_lock_create().
 *
 * Return: void
 */
void rdtgroup_pseudo_lock_remove(struct rdtgroup *rdtgrp)
{
	if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP)
		/*
		 * Default group cannot be a pseudo-locked region so we can
		 * free closid here.
		 */
		closid_free(rdtgrp->closid);

	pseudo_lock_free(rdtgrp);
}
