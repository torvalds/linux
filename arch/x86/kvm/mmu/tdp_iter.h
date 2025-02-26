// SPDX-License-Identifier: GPL-2.0

#ifndef __KVM_X86_MMU_TDP_ITER_H
#define __KVM_X86_MMU_TDP_ITER_H

#include <linux/kvm_host.h>

#include "mmu.h"
#include "spte.h"

/*
 * TDP MMU SPTEs are RCU protected to allow paging structures (non-leaf SPTEs)
 * to be zapped while holding mmu_lock for read, and to allow TLB flushes to be
 * batched without having to collect the list of zapped SPs.  Flows that can
 * remove SPs must service pending TLB flushes prior to dropping RCU protection.
 */
static inline u64 kvm_tdp_mmu_read_spte(tdp_ptep_t sptep)
{
	return READ_ONCE(*rcu_dereference(sptep));
}

static inline u64 kvm_tdp_mmu_write_spte_atomic(tdp_ptep_t sptep, u64 new_spte)
{
	KVM_MMU_WARN_ON(is_ept_ve_possible(new_spte));
	return xchg(rcu_dereference(sptep), new_spte);
}

static inline void __kvm_tdp_mmu_write_spte(tdp_ptep_t sptep, u64 new_spte)
{
	KVM_MMU_WARN_ON(is_ept_ve_possible(new_spte));
	WRITE_ONCE(*rcu_dereference(sptep), new_spte);
}

/*
 * SPTEs must be modified atomically if they are shadow-present, leaf
 * SPTEs, and have volatile bits, i.e. has bits that can be set outside
 * of mmu_lock.  The Writable bit can be set by KVM's fast page fault
 * handler, and Accessed and Dirty bits can be set by the CPU.
 *
 * Note, non-leaf SPTEs do have Accessed bits and those bits are
 * technically volatile, but KVM doesn't consume the Accessed bit of
 * non-leaf SPTEs, i.e. KVM doesn't care if it clobbers the bit.  This
 * logic needs to be reassessed if KVM were to use non-leaf Accessed
 * bits, e.g. to skip stepping down into child SPTEs when aging SPTEs.
 */
static inline bool kvm_tdp_mmu_spte_need_atomic_write(u64 old_spte, int level)
{
	return is_shadow_present_pte(old_spte) &&
	       is_last_spte(old_spte, level) &&
	       spte_has_volatile_bits(old_spte);
}

static inline u64 kvm_tdp_mmu_write_spte(tdp_ptep_t sptep, u64 old_spte,
					 u64 new_spte, int level)
{
	if (kvm_tdp_mmu_spte_need_atomic_write(old_spte, level))
		return kvm_tdp_mmu_write_spte_atomic(sptep, new_spte);

	__kvm_tdp_mmu_write_spte(sptep, new_spte);
	return old_spte;
}

static inline u64 tdp_mmu_clear_spte_bits(tdp_ptep_t sptep, u64 old_spte,
					  u64 mask, int level)
{
	atomic64_t *sptep_atomic;

	if (kvm_tdp_mmu_spte_need_atomic_write(old_spte, level)) {
		sptep_atomic = (atomic64_t *)rcu_dereference(sptep);
		return (u64)atomic64_fetch_and(~mask, sptep_atomic);
	}

	__kvm_tdp_mmu_write_spte(sptep, old_spte & ~mask);
	return old_spte;
}

/*
 * A TDP iterator performs a pre-order walk over a TDP paging structure.
 */
struct tdp_iter {
	/*
	 * The iterator will traverse the paging structure towards the mapping
	 * for this GFN.
	 */
	gfn_t next_last_level_gfn;
	/*
	 * The next_last_level_gfn at the time when the thread last
	 * yielded. Only yielding when the next_last_level_gfn !=
	 * yielded_gfn helps ensure forward progress.
	 */
	gfn_t yielded_gfn;
	/* Pointers to the page tables traversed to reach the current SPTE */
	tdp_ptep_t pt_path[PT64_ROOT_MAX_LEVEL];
	/* A pointer to the current SPTE */
	tdp_ptep_t sptep;
	/* The lowest GFN (mask bits excluded) mapped by the current SPTE */
	gfn_t gfn;
	/* Mask applied to convert the GFN to the mapping GPA */
	gfn_t gfn_bits;
	/* The level of the root page given to the iterator */
	int root_level;
	/* The lowest level the iterator should traverse to */
	int min_level;
	/* The iterator's current level within the paging structure */
	int level;
	/* The address space ID, i.e. SMM vs. regular. */
	int as_id;
	/* A snapshot of the value at sptep */
	u64 old_spte;
	/*
	 * Whether the iterator has a valid state. This will be false if the
	 * iterator walks off the end of the paging structure.
	 */
	bool valid;
	/*
	 * True if KVM dropped mmu_lock and yielded in the middle of a walk, in
	 * which case tdp_iter_next() needs to restart the walk at the root
	 * level instead of advancing to the next entry.
	 */
	bool yielded;
};

/*
 * Iterates over every SPTE mapping the GFN range [start, end) in a
 * preorder traversal.
 */
#define for_each_tdp_pte_min_level(iter, kvm, root, min_level, start, end)		  \
	for (tdp_iter_start(&iter, root, min_level, start, kvm_gfn_root_bits(kvm, root)); \
	     iter.valid && iter.gfn < end;						  \
	     tdp_iter_next(&iter))

#define for_each_tdp_pte_min_level_all(iter, root, min_level)		\
	for (tdp_iter_start(&iter, root, min_level, 0, 0);		\
		iter.valid && iter.gfn < tdp_mmu_max_gfn_exclusive();	\
		tdp_iter_next(&iter))

#define for_each_tdp_pte(iter, kvm, root, start, end)				\
	for_each_tdp_pte_min_level(iter, kvm, root, PG_LEVEL_4K, start, end)

tdp_ptep_t spte_to_child_pt(u64 pte, int level);

void tdp_iter_start(struct tdp_iter *iter, struct kvm_mmu_page *root,
		    int min_level, gfn_t next_last_level_gfn, gfn_t gfn_bits);
void tdp_iter_next(struct tdp_iter *iter);
void tdp_iter_restart(struct tdp_iter *iter);

#endif /* __KVM_X86_MMU_TDP_ITER_H */
