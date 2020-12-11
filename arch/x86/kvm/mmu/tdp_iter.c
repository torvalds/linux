// SPDX-License-Identifier: GPL-2.0

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
		SHADOW_PT_INDEX(iter->gfn << PAGE_SHIFT, iter->level);
	iter->old_spte = READ_ONCE(*iter->sptep);
}

static gfn_t round_gfn_for_level(gfn_t gfn, int level)
{
	return gfn & -KVM_PAGES_PER_HPAGE(level);
}

/*
 * Sets a TDP iterator to walk a pre-order traversal of the paging structure
 * rooted at root_pt, starting with the walk to translate goal_gfn.
 */
void tdp_iter_start(struct tdp_iter *iter, u64 *root_pt, int root_level,
		    int min_level, gfn_t goal_gfn)
{
	WARN_ON(root_level < 1);
	WARN_ON(root_level > PT64_ROOT_MAX_LEVEL);

	iter->goal_gfn = goal_gfn;
	iter->root_level = root_level;
	iter->min_level = min_level;
	iter->level = root_level;
	iter->pt_path[iter->level - 1] = root_pt;

	iter->gfn = round_gfn_for_level(iter->goal_gfn, iter->level);
	tdp_iter_refresh_sptep(iter);

	iter->valid = true;
}

/*
 * Given an SPTE and its level, returns a pointer containing the host virtual
 * address of the child page table referenced by the SPTE. Returns null if
 * there is no such entry.
 */
u64 *spte_to_child_pt(u64 spte, int level)
{
	/*
	 * There's no child entry if this entry isn't present or is a
	 * last-level entry.
	 */
	if (!is_shadow_present_pte(spte) || is_last_spte(spte, level))
		return NULL;

	return __va(spte_to_pfn(spte) << PAGE_SHIFT);
}

/*
 * Steps down one level in the paging structure towards the goal GFN. Returns
 * true if the iterator was able to step down a level, false otherwise.
 */
static bool try_step_down(struct tdp_iter *iter)
{
	u64 *child_pt;

	if (iter->level == iter->min_level)
		return false;

	/*
	 * Reread the SPTE before stepping down to avoid traversing into page
	 * tables that are no longer linked from this entry.
	 */
	iter->old_spte = READ_ONCE(*iter->sptep);

	child_pt = spte_to_child_pt(iter->old_spte, iter->level);
	if (!child_pt)
		return false;

	iter->level--;
	iter->pt_path[iter->level - 1] = child_pt;
	iter->gfn = round_gfn_for_level(iter->goal_gfn, iter->level);
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
	if (SHADOW_PT_INDEX(iter->gfn << PAGE_SHIFT, iter->level) ==
            (PT64_ENT_PER_PAGE - 1))
		return false;

	iter->gfn += KVM_PAGES_PER_HPAGE(iter->level);
	iter->goal_gfn = iter->gfn;
	iter->sptep++;
	iter->old_spte = READ_ONCE(*iter->sptep);

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
	iter->gfn = round_gfn_for_level(iter->gfn, iter->level);
	tdp_iter_refresh_sptep(iter);

	return true;
}

/*
 * Step to the next SPTE in a pre-order traversal of the paging structure.
 * To get to the next SPTE, the iterator either steps down towards the goal
 * GFN, if at a present, non-last-level SPTE, or over to a SPTE mapping a
 * highter GFN.
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
	if (try_step_down(iter))
		return;

	do {
		if (try_step_side(iter))
			return;
	} while (try_step_up(iter));
	iter->valid = false;
}

/*
 * Restart the walk over the paging structure from the root, starting from the
 * highest gfn the iterator had previously reached. Assumes that the entire
 * paging structure, except the root page, may have been completely torn down
 * and rebuilt.
 */
void tdp_iter_refresh_walk(struct tdp_iter *iter)
{
	gfn_t goal_gfn = iter->goal_gfn;

	if (iter->gfn > goal_gfn)
		goal_gfn = iter->gfn;

	tdp_iter_start(iter, iter->pt_path[iter->root_level - 1],
		       iter->root_level, iter->min_level, goal_gfn);
}

u64 *tdp_iter_root_pt(struct tdp_iter *iter)
{
	return iter->pt_path[iter->root_level - 1];
}

