// SPDX-License-Identifier: GPL-2.0

#ifndef __KVM_X86_MMU_TDP_ITER_H
#define __KVM_X86_MMU_TDP_ITER_H

#include <linux/kvm_host.h>

#include "mmu.h"

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
static inline void kvm_tdp_mmu_write_spte(tdp_ptep_t sptep, u64 val)
{
	WRITE_ONCE(*rcu_dereference(sptep), val);
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
	/* The lowest GFN mapped by the current SPTE */
	gfn_t gfn;
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
#define for_each_tdp_pte_min_level(iter, root, min_level, start, end) \
	for (tdp_iter_start(&iter, root, min_level, start); \
	     iter.valid && iter.gfn < end;		     \
	     tdp_iter_next(&iter))

#define for_each_tdp_pte(iter, root, start, end) \
	for_each_tdp_pte_min_level(iter, root, PG_LEVEL_4K, start, end)

tdp_ptep_t spte_to_child_pt(u64 pte, int level);

void tdp_iter_start(struct tdp_iter *iter, struct kvm_mmu_page *root,
		    int min_level, gfn_t next_last_level_gfn);
void tdp_iter_next(struct tdp_iter *iter);
void tdp_iter_restart(struct tdp_iter *iter);

#endif /* __KVM_X86_MMU_TDP_ITER_H */
