// SPDX-License-Identifier: GPL-2.0

#ifndef __KVM_X86_MMU_TDP_ITER_H
#define __KVM_X86_MMU_TDP_ITER_H

#include <linux/kvm_host.h>

#include "mmu.h"

typedef u64 __rcu *tdp_ptep_t;

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
	/* A snapshot of the value at sptep */
	u64 old_spte;
	/*
	 * Whether the iterator has a valid state. This will be false if the
	 * iterator walks off the end of the paging structure.
	 */
	bool valid;
};

/*
 * Iterates over every SPTE mapping the GFN range [start, end) in a
 * preorder traversal.
 */
#define for_each_tdp_pte_min_level(iter, root, root_level, min_level, start, end) \
	for (tdp_iter_start(&iter, root, root_level, min_level, start); \
	     iter.valid && iter.gfn < end;		     \
	     tdp_iter_next(&iter))

#define for_each_tdp_pte(iter, root, root_level, start, end) \
	for_each_tdp_pte_min_level(iter, root, root_level, PG_LEVEL_4K, start, end)

tdp_ptep_t spte_to_child_pt(u64 pte, int level);

void tdp_iter_start(struct tdp_iter *iter, u64 *root_pt, int root_level,
		    int min_level, gfn_t next_last_level_gfn);
void tdp_iter_next(struct tdp_iter *iter);
tdp_ptep_t tdp_iter_root_pt(struct tdp_iter *iter);

#endif /* __KVM_X86_MMU_TDP_ITER_H */
