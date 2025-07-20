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

#include <linux/cacheflush.h>
#include <linux/cpu.h>
#include <linux/perf_event.h>
#include <linux/pm_qos.h>
#include <linux/resctrl.h>

#include <asm/cpu_device_id.h>
#include <asm/perf_event.h>
#include <asm/msr.h>

#include "../../events/perf_event.h" /* For X86_CONFIG() */
#include "internal.h"

#define CREATE_TRACE_POINTS

#include "pseudo_lock_trace.h"

/*
 * The bits needed to disable hardware prefetching varies based on the
 * platform. During initialization we will discover which bits to use.
 */
static u64 prefetch_disable_bits;

/**
 * resctrl_arch_get_prefetch_disable_bits - prefetch disable bits of supported
 *                                          platforms
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
 * resctrl_arch_measure_l*_residency()
 *
 * Return:
 * If platform is supported, the bits to disable hardware prefetchers, 0
 * if platform is not supported.
 */
u64 resctrl_arch_get_prefetch_disable_bits(void)
{
	prefetch_disable_bits = 0;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL ||
	    boot_cpu_data.x86 != 6)
		return 0;

	switch (boot_cpu_data.x86_vfm) {
	case INTEL_BROADWELL_X:
		/*
		 * SDM defines bits of MSR_MISC_FEATURE_CONTROL register
		 * as:
		 * 0    L2 Hardware Prefetcher Disable (R/W)
		 * 1    L2 Adjacent Cache Line Prefetcher Disable (R/W)
		 * 2    DCU Hardware Prefetcher Disable (R/W)
		 * 3    DCU IP Prefetcher Disable (R/W)
		 * 63:4 Reserved
		 */
		prefetch_disable_bits = 0xF;
		break;
	case INTEL_ATOM_GOLDMONT:
	case INTEL_ATOM_GOLDMONT_PLUS:
		/*
		 * SDM defines bits of MSR_MISC_FEATURE_CONTROL register
		 * as:
		 * 0     L2 Hardware Prefetcher Disable (R/W)
		 * 1     Reserved
		 * 2     DCU Hardware Prefetcher Disable (R/W)
		 * 63:3  Reserved
		 */
		prefetch_disable_bits = 0x5;
		break;
	}

	return prefetch_disable_bits;
}

/**
 * resctrl_arch_pseudo_lock_fn - Load kernel memory into cache
 * @_plr: the pseudo-lock region descriptor
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
int resctrl_arch_pseudo_lock_fn(void *_plr)
{
	struct pseudo_lock_region *plr = _plr;
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
	wbinvd();

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
	saved_msr = native_rdmsrq(MSR_MISC_FEATURE_CONTROL);
	native_wrmsrq(MSR_MISC_FEATURE_CONTROL, prefetch_disable_bits);
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
	native_wrmsr(MSR_IA32_PQR_ASSOC, rmid_p, plr->closid);

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
	native_wrmsr(MSR_IA32_PQR_ASSOC, rmid_p, closid_p);

	/* Re-enable the hardware prefetcher(s) */
	wrmsrq(MSR_MISC_FEATURE_CONTROL, saved_msr);
	local_irq_enable();

	plr->thread_done = 1;
	wake_up_interruptible(&plr->lock_thread_wq);
	return 0;
}

/**
 * resctrl_arch_measure_cycles_lat_fn - Measure cycle latency to read
 *                                      pseudo-locked memory
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
int resctrl_arch_measure_cycles_lat_fn(void *_plr)
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
	wrmsrq(MSR_MISC_FEATURE_CONTROL, prefetch_disable_bits);
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
	wrmsrq(MSR_MISC_FEATURE_CONTROL, prefetch_disable_bits);

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
	hits_before = rdpmc(hit_pmcnum);
	miss_before = rdpmc(miss_pmcnum);
	/*
	 * From SDM: Performing back-to-back fast reads are not guaranteed
	 * to be monotonic.
	 * Use LFENCE to ensure all previous instructions are retired
	 * before proceeding.
	 */
	rmb();
	hits_before = rdpmc(hit_pmcnum);
	miss_before = rdpmc(miss_pmcnum);
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
	hits_after = rdpmc(hit_pmcnum);
	miss_after = rdpmc(miss_pmcnum);
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

int resctrl_arch_measure_l2_residency(void *_plr)
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
	switch (boot_cpu_data.x86_vfm) {
	case INTEL_ATOM_GOLDMONT:
	case INTEL_ATOM_GOLDMONT_PLUS:
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

int resctrl_arch_measure_l3_residency(void *_plr)
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

	switch (boot_cpu_data.x86_vfm) {
	case INTEL_BROADWELL_X:
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
	if (boot_cpu_data.x86_vfm == INTEL_BROADWELL_X) {
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
