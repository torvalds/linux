// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "mmu_internal.h"
#include "tdp_iter.h"
#include "spte.h"

/*
 * Recalculates the pointer to the SPTE for the current GFN and level and
 * reread the SPTE.
 */
static void tdp_iter_refresh_sptep(struct tdp_iter *iter)
{
	iter->sptep = iter->pt_path[iter->level - 1] +
		SPTE_INDEX((iter->gfn | iter->gfn_bits) << PAGE_SHIFT, iter->level);
	iter->old_spte = kvm_tdp_mmu_read_spte(iter->sptep);
}

/*
 * Return the TDP iterator to the root PT and allow it to continue its
 * traversal over the paging structure from there.
 */
void tdp_iter_restart(struct tdp_iter *iter)
{
	iter->yielded = false;
	iter->yielded_gfn = iter->next_last_level_gfn;
	iter->level = iter->root_level;

	iter->gfn = gfn_round_for_level(iter->next_last_level_gfn, iter->level);
	tdp_iter_refresh_sptep(iter);

	iter->valid = true;
}

/*
 * Sets a TDP iterator to walk a pre-order traversal of the paging structure
 * rooted at root_pt, starting with the walk to translate next_last_level_gfn.
 */
void tdp_iter_start(struct tdp_iter *iter, struct kvm_mmu_page *root,
		    int min_level, gfn_t next_last_level_gfn, gfn_t gfn_bits)
{
	if (WARN_ON_ONCE(!root || (root->role.level < 1) ||
			 (root->role.level > PT64_ROOT_MAX_LEVEL) ||
			 (gfn_bits && next_last_level_gfn >= gfn_bits))) {
		iter->valid = false;
		return;
	}

	iter->next_last_level_gfn = next_last_level_gfn;
	iter->gfn_bits = gfn_bits;
	iter->root_level = root->role.level;
	iter->min_level = min_level;
	iter->pt_path[iter->root_level - 1] = (tdp_ptep_t)root->spt;
	iter->as_id = kvm_mmu_page_as_id(root);

	tdp_iter_restart(iter);
}

/*
 * Given an SPTE and its level, returns a pointer containing the host virtual
 * address of the child page table referenced by the SPTE. Returns null if
 * there is no such entry.
 */
tdp_ptep_t spte_to_child_pt(u64 spte, int level)
{
	/*
	 * There's no child entry if this entry isn't present or is a
	 * last-level entry.
	 */
	if (!is_shadow_present_pte(spte) || is_last_spte(spte, level))
		return NULL;

	return (tdp_ptep_t)__va(spte_to_pfn(spte) << PAGE_SHIFT);
}

/*
 * Steps down one level in the paging structure towards the goal GFN. Returns
 * true if the iterator was able to step down a level, false otherwise.
 */
static bool try_step_down(struct tdp_iter *iter)
{
	tdp_ptep_t child_pt;

	if (iter->level == iter->min_level)
		return false;

	/*
	 * Reread the SPTE before stepping down to avoid traversing into page
	 * tables that are no longer linked from this entry.
	 */
	iter->old_spte = kvm_tdp_mmu_read_spte(iter->sptep);

	child_pt = spte_to_child_pt(iter->old_spte, iter->level);
	if (!child_pt)
		return false;

	iter->level--;
	iter->pt_path[iter->level - 1] = child_pt;
	iter->gfn = gfn_round_for_level(iter->next_last_level_gfn, iter->level);
	tdp_iter_refresh_sptep(iter);

	return true;
}

/*
 * Steps to the next entry in the current page table, at the current page table
 * level. The next entry could point to a page backing guest memory or another
 * page table, or it could be non-present. Returns true if the iterator was
 * able to step to the next entry in the page table, false if the iterator was
 * already at the end of the current page table.
 */
static bool try_step_side(struct tdp_iter *iter)
{
	/*
	 * Check if the iterator is already at the end of the current page
	 * table.
	 */
	if (SPTE_INDEX((iter->gfn | iter->gfn_bits) << PAGE_SHIFT, iter->level) ==
	    (SPTE_ENT_PER_PAGE - 1))
		return false;

	iter->gfn += KVM_PAGES_PER_HPAGE(iter->level);
	iter->next_last_level_gfn = iter->gfn;
	iter->sptep++;
	iter->old_spte = kvm_tdp_mmu_read_spte(iter->sptep);

	return true;
}

/*
 * Tries to traverse back up a level in the paging structure so that the walk
 * can continue from the next entry in the parent page table. Returns true on a
 * successful step up, false if already in the root page.
 */
static bool try_step_up(struct tdp_iter *iter)
{
	if (iter->level == iter->root_level)
		return false;

	iter->level++;
	iter->gfn = gfn_round_for_level(iter->gfn, iter->level);
	tdp_iter_refresh_sptep(iter);

	return true;
}

/*
 * Step to the next SPTE in a pre-order traversal of the paging structure.
 * To get to the next SPTE, the iterator either steps down towards the goal
 * GFN, if at a present, non-last-level SPTE, or over to a SPTE mapping a
 * higher GFN.
 *
 * The basic algorithm is as follows:
 * 1. If the current SPTE is a non-last-level SPTE, step down into the page
 *    table it points to.
 * 2. If the iterator cannot step down, it will try to step to the next SPTE
 *    in the current page of the paging structure.
 * 3. If the iterator cannot step to the next entry in the current page, it will
 *    try to step up to the parent paging structure page. In this case, that
 *    SPTE will have already been visited, and so the iterator must also step
 *    to the side again.
 */
void tdp_iter_next(struct tdp_iter *iter)
{
	if (iter->yielded) {
		tdp_iter_restart(iter);
		return;
	}

	if (try_step_down(iter))
		return;

	do {
		if (try_step_side(iter))
			return;
	} while (try_step_up(iter));
	iter->valid = false;
}

