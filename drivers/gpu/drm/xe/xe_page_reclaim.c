// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "xe_page_reclaim.h"

#include "xe_gt_stats.h"
#include "xe_macros.h"
#include "xe_pat.h"
#include "xe_sa.h"
#include "xe_tlb_inval_types.h"

/**
 * xe_page_reclaim_skip() - Decide whether PRL should be skipped for a VMA
 * @tile: Tile owning the VMA
 * @vma: VMA under consideration
 *
 * PPC flushing may be handled by HW for specific PAT encodings.
 * Skip PPC flushing/Page Reclaim for scenarios below due to redundant
 * flushes.
 * - pat_index is transient display (1)
 *
 * Return: true when page reclamation is unnecessary, false otherwise.
 */
bool xe_page_reclaim_skip(struct xe_tile *tile, struct xe_vma *vma)
{
	u8 l3_policy;

	l3_policy = xe_pat_index_get_l3_policy(tile->xe, vma->attr.pat_index);

	/*
	 *   - l3_policy:   0=WB, 1=XD ("WB - Transient Display"), 3=UC
	 * Transient display flushes is taken care by HW, l3_policy = 1.
	 *
	 * HW will sequence these transient flushes at various sync points so
	 * any event of page reclamation will hit these sync points before
	 * page reclamation could execute.
	 */
	return (l3_policy == XE_L3_POLICY_XD);
}

/**
 * xe_page_reclaim_create_prl_bo() - Back a PRL with a suballocated GGTT BO
 * @tlb_inval: TLB invalidation frontend associated with the request
 * @prl: page reclaim list data that bo will copy from
 * @fence: tlb invalidation fence that page reclaim action is paired to
 *
 * Suballocates a 4K BO out of the tile reclaim pool, copies the PRL CPU
 * copy into the BO and queues the buffer for release when @fence signals.
 *
 * Return: struct drm_suballoc pointer on success or ERR_PTR on failure.
 */
struct drm_suballoc *xe_page_reclaim_create_prl_bo(struct xe_tlb_inval *tlb_inval,
						   struct xe_page_reclaim_list *prl,
						   struct xe_tlb_inval_fence *fence)
{
	struct xe_gt *gt = container_of(tlb_inval, struct xe_gt, tlb_inval);
	struct xe_tile *tile = gt_to_tile(gt);
	/* (+1) for NULL page_reclaim_entry to indicate end of list */
	int prl_size = min(prl->num_entries + 1, XE_PAGE_RECLAIM_MAX_ENTRIES) *
		sizeof(struct xe_guc_page_reclaim_entry);
	struct drm_suballoc *prl_sa;

	/* Maximum size of PRL is 1 4K-page */
	prl_sa = __xe_sa_bo_new(tile->mem.reclaim_pool,
				prl_size, GFP_ATOMIC);
	if (IS_ERR(prl_sa))
		return prl_sa;

	memcpy(xe_sa_bo_cpu_addr(prl_sa), prl->entries,
	       prl_size);
	xe_sa_bo_flush_write(prl_sa);
	/* Queue up sa_bo_free on tlb invalidation fence signal */
	xe_sa_bo_free(prl_sa, &fence->base);

	return prl_sa;
}

/**
 * xe_page_reclaim_list_invalidate() - Mark a PRL as invalid
 * @prl: Page reclaim list to reset
 *
 * Clears the entries pointer and marks the list as invalid so
 * future use knows PRL is unusable. It is expected that the entries
 * have already been released.
 */
void xe_page_reclaim_list_invalidate(struct xe_page_reclaim_list *prl)
{
	xe_page_reclaim_entries_put(prl->entries);
	prl->entries = NULL;
	prl->num_entries = XE_PAGE_RECLAIM_INVALID_LIST;
}

/**
 * xe_page_reclaim_list_init() - Initialize a page reclaim list
 * @prl: Page reclaim list to initialize
 *
 * NULLs both values in list to prepare on initalization.
 */
void xe_page_reclaim_list_init(struct xe_page_reclaim_list *prl)
{
	prl->entries = NULL;
	prl->num_entries = 0;
}

/**
 * xe_page_reclaim_list_alloc_entries() - Allocate page reclaim list entries
 * @prl: Page reclaim list to allocate entries for
 *
 * Allocate one 4K page for the PRL entries, otherwise assign prl->entries to NULL.
 */
int xe_page_reclaim_list_alloc_entries(struct xe_page_reclaim_list *prl)
{
	struct page *page;

	if (XE_WARN_ON(prl->entries))
		return 0;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (page) {
		prl->entries = page_address(page);
		prl->num_entries = 0;
	}

	return page ? 0 : -ENOMEM;
}
