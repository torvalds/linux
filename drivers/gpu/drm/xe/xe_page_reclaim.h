/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PAGE_RECLAIM_H_
#define _XE_PAGE_RECLAIM_H_

#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/bits.h>

#define XE_PAGE_RECLAIM_MAX_ENTRIES	512
#define XE_PAGE_RECLAIM_LIST_MAX_SIZE	SZ_4K

struct xe_tlb_inval;
struct xe_tlb_inval_fence;
struct xe_tile;
struct xe_gt;
struct xe_vma;

struct xe_guc_page_reclaim_entry {
	u64 qw;
/* valid reclaim entry bit */
#define XE_PAGE_RECLAIM_VALID		BIT_ULL(0)
/*
 * offset order of page size to be reclaimed
 * page_size = 1 << (XE_PTE_SHIFT + reclamation_size)
 */
#define XE_PAGE_RECLAIM_SIZE		GENMASK_ULL(6, 1)
#define XE_PAGE_RECLAIM_RSVD_0		GENMASK_ULL(11, 7)
/* lower 20 bits of the physical address */
#define XE_PAGE_RECLAIM_ADDR_LO		GENMASK_ULL(31, 12)
/* upper 20 bits of the physical address */
#define XE_PAGE_RECLAIM_ADDR_HI		GENMASK_ULL(51, 32)
#define XE_PAGE_RECLAIM_RSVD_1		GENMASK_ULL(63, 52)
} __packed;

struct xe_page_reclaim_list {
	/** @entries: array of page reclaim entries, page allocated */
	struct xe_guc_page_reclaim_entry *entries;
	/** @num_entries: number of entries */
	int num_entries;
#define XE_PAGE_RECLAIM_INVALID_LIST	-1
};

/**
 * xe_page_reclaim_list_is_new() - Check if PRL is new allocation
 * @prl: Pointer to page reclaim list
 *
 * PRL indicates it hasn't been allocated through both values being NULL
 */
static inline bool xe_page_reclaim_list_is_new(struct xe_page_reclaim_list *prl)
{
	return !prl->entries && prl->num_entries == 0;
}

/**
 * xe_page_reclaim_list_valid() - Check if the page reclaim list is valid
 * @prl: Pointer to page reclaim list
 *
 * PRL uses the XE_PAGE_RECLAIM_INVALID_LIST to indicate that a PRL
 * is unusable.
 */
static inline bool xe_page_reclaim_list_valid(struct xe_page_reclaim_list *prl)
{
	return !xe_page_reclaim_list_is_new(prl) &&
	       prl->num_entries != XE_PAGE_RECLAIM_INVALID_LIST;
}

bool xe_page_reclaim_skip(struct xe_tile *tile, struct xe_vma *vma);
struct drm_suballoc *xe_page_reclaim_create_prl_bo(struct xe_tlb_inval *tlb_inval,
						   struct xe_page_reclaim_list *prl,
						   struct xe_tlb_inval_fence *fence);
void xe_page_reclaim_list_invalidate(struct xe_page_reclaim_list *prl);

/**
 * xe_page_reclaim_list_abort() - Invalidate a PRL and log an abort reason
 * @gt: GT owning the page reclaim request
 * @prl: Page reclaim list to invalidate
 * @fmt: format string for the log message with args
 *
 * Abort page reclaim process by invalidating PRL and doing any relevant logging.
 */
#define xe_page_reclaim_list_abort(gt, prl, fmt, ...)					\
	do {										\
		struct xe_gt *__gt = (gt);						\
		struct xe_page_reclaim_list *__prl = (prl);				\
											\
		xe_page_reclaim_list_invalidate(__prl);					\
		xe_gt_stats_incr(__gt, XE_GT_STATS_ID_PRL_ABORTED_COUNT, 1);		\
		vm_dbg(&gt_to_xe(__gt)->drm, "PRL aborted: " fmt, ##__VA_ARGS__);	\
	} while (0)

void xe_page_reclaim_list_init(struct xe_page_reclaim_list *prl);
int xe_page_reclaim_list_alloc_entries(struct xe_page_reclaim_list *prl);
/**
 * xe_page_reclaim_entries_get() - Increment the reference count of page reclaim entries.
 * @entries: Pointer to the array of page reclaim entries.
 *
 * This function increments the reference count of the backing page.
 */
static inline void xe_page_reclaim_entries_get(struct xe_guc_page_reclaim_entry *entries)
{
	if (entries)
		get_page(virt_to_page(entries));
}

/**
 * xe_page_reclaim_entries_put() - Decrement the reference count of page reclaim entries.
 * @entries: Pointer to the array of page reclaim entries.
 *
 * This function decrements the reference count of the backing page
 * and frees it if the count reaches zero.
 */
static inline void xe_page_reclaim_entries_put(struct xe_guc_page_reclaim_entry *entries)
{
	if (entries)
		put_page(virt_to_page(entries));
}

#endif	/* _XE_PAGE_RECLAIM_H_ */
