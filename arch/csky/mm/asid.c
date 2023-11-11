// SPDX-License-Identifier: GPL-2.0
/*
 * Generic ASID allocator.
 *
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/slab.h>
#include <linux/mm_types.h>

#include <asm/asid.h>

#define reserved_asid(info, cpu) *per_cpu_ptr((info)->reserved, cpu)

#define ASID_MASK(info)			(~GENMASK((info)->bits - 1, 0))
#define ASID_FIRST_VERSION(info)	(1UL << ((info)->bits))

#define asid2idx(info, asid)		(((asid) & ~ASID_MASK(info)) >> (info)->ctxt_shift)
#define idx2asid(info, idx)		(((idx) << (info)->ctxt_shift) & ~ASID_MASK(info))

static void flush_context(struct asid_info *info)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
	bitmap_zero(info->map, NUM_CTXT_ASIDS(info));

	for_each_possible_cpu(i) {
		asid = atomic64_xchg_relaxed(&active_asid(info, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * ASID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (asid == 0)
			asid = reserved_asid(info, i);
		__set_bit(asid2idx(info, asid), info->map);
		reserved_asid(info, i) = asid;
	}

	/*
	 * Queue a TLB invalidation for each CPU to perform on next
	 * context-switch
	 */
	cpumask_setall(&info->flush_pending);
}

static bool check_update_reserved_asid(struct asid_info *info, u64 asid,
				       u64 newasid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved ASIDs looking for a match.
	 * If we find one, then we can update our mm to use newasid
	 * (i.e. the same ASID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old ASID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved ASID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (reserved_asid(info, cpu) == asid) {
			hit = true;
			reserved_asid(info, cpu) = newasid;
		}
	}

	return hit;
}

static u64 new_context(struct asid_info *info, atomic64_t *pasid,
		       struct mm_struct *mm)
{
	static u32 cur_idx = 1;
	u64 asid = atomic64_read(pasid);
	u64 generation = atomic64_read(&info->generation);

	if (asid != 0) {
		u64 newasid = generation | (asid & ~ASID_MASK(info));

		/*
		 * If our current ASID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_asid(info, asid, newasid))
			return newasid;

		/*
		 * We had a valid ASID in a previous life, so try to re-use
		 * it if possible.
		 */
		if (!__test_and_set_bit(asid2idx(info, asid), info->map))
			return newasid;
	}

	/*
	 * Allocate a free ASID. If we can't find one, take a note of the
	 * currently active ASIDs and mark the TLBs as requiring flushes.  We
	 * always count from ASID #2 (index 1), as we use ASID #0 when setting
	 * a reserved TTBR0 for the init_mm and we allocate ASIDs in even/odd
	 * pairs.
	 */
	asid = find_next_zero_bit(info->map, NUM_CTXT_ASIDS(info), cur_idx);
	if (asid != NUM_CTXT_ASIDS(info))
		goto set_asid;

	/* We're out of ASIDs, so increment the global generation count */
	generation = atomic64_add_return_relaxed(ASID_FIRST_VERSION(info),
						 &info->generation);
	flush_context(info);

	/* We have more ASIDs than CPUs, so this will always succeed */
	asid = find_next_zero_bit(info->map, NUM_CTXT_ASIDS(info), 1);

set_asid:
	__set_bit(asid, info->map);
	cur_idx = asid;
	cpumask_clear(mm_cpumask(mm));
	return idx2asid(info, asid) | generation;
}

/*
 * Generate a new ASID for the context.
 *
 * @pasid: Pointer to the current ASID batch allocated. It will be updated
 * with the new ASID batch.
 * @cpu: current CPU ID. Must have been acquired through get_cpu()
 */
void asid_new_context(struct asid_info *info, atomic64_t *pasid,
		      unsigned int cpu, struct mm_struct *mm)
{
	unsigned long flags;
	u64 asid;

	raw_spin_lock_irqsave(&info->lock, flags);
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(pasid);
	if ((asid ^ atomic64_read(&info->generation)) >> info->bits) {
		asid = new_context(info, pasid, mm);
		atomic64_set(pasid, asid);
	}

	if (cpumask_test_and_clear_cpu(cpu, &info->flush_pending))
		info->flush_cpu_ctxt_cb();

	atomic64_set(&active_asid(info, cpu), asid);
	cpumask_set_cpu(cpu, mm_cpumask(mm));
	raw_spin_unlock_irqrestore(&info->lock, flags);
}

/*
 * Initialize the ASID allocator
 *
 * @info: Pointer to the asid allocator structure
 * @bits: Number of ASIDs available
 * @asid_per_ctxt: Number of ASIDs to allocate per-context. ASIDs are
 * allocated contiguously for a given context. This value should be a power of
 * 2.
 */
int asid_allocator_init(struct asid_info *info,
			u32 bits, unsigned int asid_per_ctxt,
			void (*flush_cpu_ctxt_cb)(void))
{
	info->bits = bits;
	info->ctxt_shift = ilog2(asid_per_ctxt);
	info->flush_cpu_ctxt_cb = flush_cpu_ctxt_cb;
	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more ASID than CPUs. ASID #0 is always reserved.
	 */
	WARN_ON(NUM_CTXT_ASIDS(info) - 1 <= num_possible_cpus());
	atomic64_set(&info->generation, ASID_FIRST_VERSION(info));
	info->map = bitmap_zalloc(NUM_CTXT_ASIDS(info), GFP_KERNEL);
	if (!info->map)
		return -ENOMEM;

	raw_spin_lock_init(&info->lock);

	return 0;
}
