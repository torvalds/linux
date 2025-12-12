// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "xe_page_reclaim.h"

#include "regs/xe_gt_regs.h"
#include "xe_assert.h"
#include "xe_macros.h"

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
	// xe_page_reclaim_list_invalidate(prl);
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
