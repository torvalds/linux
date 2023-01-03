// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/static_key.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_MMU

DEFINE_STATIC_KEY_FALSE(use_asid_allocator);

static unsigned long asid_bits;
static unsigned long num_asids;
static unsigned long asid_mask;

static atomic_long_t current_version;

static DEFINE_RAW_SPINLOCK(context_lock);
static cpumask_t context_tlb_flush_pending;
static unsigned long *context_asid_map;

static DEFINE_PER_CPU(atomic_long_t, active_context);
static DEFINE_PER_CPU(unsigned long, reserved_context);

static bool check_update_reserved_context(unsigned long cntx,
					  unsigned long newcntx)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved CONTEXT looking for a match.
	 * If we find one, then we can update our mm to use new CONTEXT
	 * (i.e. the same CONTEXT in the current_version) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old CONTEXT are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved CONTEXT in a future
	 * version.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_context, cpu) == cntx) {
			hit = true;
			per_cpu(reserved_context, cpu) = newcntx;
		}
	}

	return hit;
}

static void __flush_context(void)
{
	int i;
	unsigned long cntx;

	/* Must be called with context_lock held */
	lockdep_assert_held(&context_lock);

	/* Update the list of reserved ASIDs and the ASID bitmap. */
	bitmap_clear(context_asid_map, 0, num_asids);

	/* Mark already active ASIDs as used */
	for_each_possible_cpu(i) {
		cntx = atomic_long_xchg_relaxed(&per_cpu(active_context, i), 0);
		/*
		 * If this CPU has already been through a rollover, but
		 * hasn't run another task in the meantime, we must preserve
		 * its reserved CONTEXT, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (cntx == 0)
			cntx = per_cpu(reserved_context, i);

		__set_bit(cntx & asid_mask, context_asid_map);
		per_cpu(reserved_context, i) = cntx;
	}

	/* Mark ASID #0 as used because it is used at boot-time */
	__set_bit(0, context_asid_map);

	/* Queue a TLB invalidation for each CPU on next context-switch */
	cpumask_setall(&context_tlb_flush_pending);
}

static unsigned long __new_context(struct mm_struct *mm)
{
	static u32 cur_idx = 1;
	unsigned long cntx = atomic_long_read(&mm->context.id);
	unsigned long asid, ver = atomic_long_read(&current_version);

	/* Must be called with context_lock held */
	lockdep_assert_held(&context_lock);

	if (cntx != 0) {
		unsigned long newcntx = ver | (cntx & asid_mask);

		/*
		 * If our current CONTEXT was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_context(cntx, newcntx))
			return newcntx;

		/*
		 * We had a valid CONTEXT in a previous life, so try to
		 * re-use it if possible.
		 */
		if (!__test_and_set_bit(cntx & asid_mask, context_asid_map))
			return newcntx;
	}

	/*
	 * Allocate a free ASID. If we can't find one then increment
	 * current_version and flush all ASIDs.
	 */
	asid = find_next_zero_bit(context_asid_map, num_asids, cur_idx);
	if (asid != num_asids)
		goto set_asid;

	/* We're out of ASIDs, so increment current_version */
	ver = atomic_long_add_return_relaxed(num_asids, &current_version);

	/* Flush everything  */
	__flush_context();

	/* We have more ASIDs than CPUs, so this will always succeed */
	asid = find_next_zero_bit(context_asid_map, num_asids, 1);

set_asid:
	__set_bit(asid, context_asid_map);
	cur_idx = asid;
	return asid | ver;
}

static void set_mm_asid(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long flags;
	bool need_flush_tlb = false;
	unsigned long cntx, old_active_cntx;

	cntx = atomic_long_read(&mm->context.id);

	/*
	 * If our active_context is non-zero and the context matches the
	 * current_version, then we update the active_context entry with a
	 * relaxed cmpxchg.
	 *
	 * Following is how we handle racing with a concurrent rollover:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated verion.
	 *
	 * - We get a valid context back from the cmpxchg then we continue
	 *   using old ASID because __flush_context() would have marked ASID
	 *   of active_context as used and next context switch we will
	 *   allocate new context.
	 */
	old_active_cntx = atomic_long_read(&per_cpu(active_context, cpu));
	if (old_active_cntx &&
	    ((cntx & ~asid_mask) == atomic_long_read(&current_version)) &&
	    atomic_long_cmpxchg_relaxed(&per_cpu(active_context, cpu),
					old_active_cntx, cntx))
		goto switch_mm_fast;

	raw_spin_lock_irqsave(&context_lock, flags);

	/* Check that our ASID belongs to the current_version. */
	cntx = atomic_long_read(&mm->context.id);
	if ((cntx & ~asid_mask) != atomic_long_read(&current_version)) {
		cntx = __new_context(mm);
		atomic_long_set(&mm->context.id, cntx);
	}

	if (cpumask_test_and_clear_cpu(cpu, &context_tlb_flush_pending))
		need_flush_tlb = true;

	atomic_long_set(&per_cpu(active_context, cpu), cntx);

	raw_spin_unlock_irqrestore(&context_lock, flags);

switch_mm_fast:
	csr_write(CSR_SATP, virt_to_pfn(mm->pgd) |
		  ((cntx & asid_mask) << SATP_ASID_SHIFT) |
		  satp_mode);

	if (need_flush_tlb)
		local_flush_tlb_all();
#ifdef CONFIG_SMP
	else {
		cpumask_t *mask = &mm->context.tlb_stale_mask;

		if (cpumask_test_cpu(cpu, mask)) {
			cpumask_clear_cpu(cpu, mask);
			local_flush_tlb_all_asid(cntx & asid_mask);
		}
	}
#endif
}

static void set_mm_noasid(struct mm_struct *mm)
{
	/* Switch the page table and blindly nuke entire local TLB */
	csr_write(CSR_SATP, virt_to_pfn(mm->pgd) | satp_mode);
	local_flush_tlb_all();
}

static inline void set_mm(struct mm_struct *mm, unsigned int cpu)
{
	if (static_branch_unlikely(&use_asid_allocator))
		set_mm_asid(mm, cpu);
	else
		set_mm_noasid(mm);
}

static int __init asids_init(void)
{
	unsigned long old;

	/* Figure-out number of ASID bits in HW */
	old = csr_read(CSR_SATP);
	asid_bits = old | (SATP_ASID_MASK << SATP_ASID_SHIFT);
	csr_write(CSR_SATP, asid_bits);
	asid_bits = (csr_read(CSR_SATP) >> SATP_ASID_SHIFT)  & SATP_ASID_MASK;
	asid_bits = fls_long(asid_bits);
	csr_write(CSR_SATP, old);

	/*
	 * In the process of determining number of ASID bits (above)
	 * we polluted the TLB of current HART so let's do TLB flushed
	 * to remove unwanted TLB enteries.
	 */
	local_flush_tlb_all();

	/* Pre-compute ASID details */
	if (asid_bits) {
		num_asids = 1 << asid_bits;
		asid_mask = num_asids - 1;
	}

	/*
	 * Use ASID allocator only if number of HW ASIDs are
	 * at-least twice more than CPUs
	 */
	if (num_asids > (2 * num_possible_cpus())) {
		atomic_long_set(&current_version, num_asids);

		context_asid_map = bitmap_zalloc(num_asids, GFP_KERNEL);
		if (!context_asid_map)
			panic("Failed to allocate bitmap for %lu ASIDs\n",
			      num_asids);

		__set_bit(0, context_asid_map);

		static_branch_enable(&use_asid_allocator);

		pr_info("ASID allocator using %lu bits (%lu entries)\n",
			asid_bits, num_asids);
	} else {
		pr_info("ASID allocator disabled (%lu bits)\n", asid_bits);
	}

	return 0;
}
early_initcall(asids_init);
#else
static inline void set_mm(struct mm_struct *mm, unsigned int cpu)
{
	/* Nothing to do here when there is no MMU */
}
#endif

/*
 * When necessary, performs a deferred icache flush for the given MM context,
 * on the local CPU.  RISC-V has no direct mechanism for instruction cache
 * shoot downs, so instead we send an IPI that informs the remote harts they
 * need to flush their local instruction caches.  To avoid pathologically slow
 * behavior in a common case (a bunch of single-hart processes on a many-hart
 * machine, ie 'make -j') we avoid the IPIs for harts that are not currently
 * executing a MM context and instead schedule a deferred local instruction
 * cache flush to be performed before execution resumes on each hart.  This
 * actually performs that local instruction cache flush, which implicitly only
 * refers to the current hart.
 *
 * The "cpu" argument must be the current local CPU number.
 */
static inline void flush_icache_deferred(struct mm_struct *mm, unsigned int cpu)
{
#ifdef CONFIG_SMP
	cpumask_t *mask = &mm->context.icache_stale_mask;

	if (cpumask_test_cpu(cpu, mask)) {
		cpumask_clear_cpu(cpu, mask);
		/*
		 * Ensure the remote hart's writes are visible to this hart.
		 * This pairs with a barrier in flush_icache_mm.
		 */
		smp_mb();
		local_flush_icache_all();
	}

#endif
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	struct task_struct *task)
{
	unsigned int cpu;

	if (unlikely(prev == next))
		return;

	/*
	 * Mark the current MM context as inactive, and the next as
	 * active.  This is at least used by the icache flushing
	 * routines in order to determine who should be flushed.
	 */
	cpu = smp_processor_id();

	cpumask_clear_cpu(cpu, mm_cpumask(prev));
	cpumask_set_cpu(cpu, mm_cpumask(next));

	set_mm(next, cpu);

	flush_icache_deferred(next, cpu);
}
