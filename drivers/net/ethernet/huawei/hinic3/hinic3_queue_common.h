/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_QUEUE_COMMON_H_
#define _HINIC3_QUEUE_COMMON_H_

#include <linux/types.h>

#include "hinic3_common.h"

struct hinic3_hwdev;

struct hinic3_queue_pages {
	/* Array of DMA-able pages that actually holds the queue entries. */
	struct hinic3_dma_addr_align  *pages;
	/* Page size in bytes. */
	u32                           page_size;
	/* Number of pages, must be power of 2. */
	u16                           num_pages;
	u8                            elem_size_shift;
	u8                            elem_per_pg_shift;
};

void hinic3_queue_pages_init(struct hinic3_queue_pages *qpages, u32 q_depth,
			     u32 page_size, u32 elem_size);
int hinic3_queue_pages_alloc(struct hinic3_hwdev *hwdev,
			     struct hinic3_queue_pages *qpages, u32 align);
void hinic3_queue_pages_free(struct hinic3_hwdev *hwdev,
			     struct hinic3_queue_pages *qpages);

/* Get pointer to queue entry at the specified index. Index does not have to be
 * masked to queue depth, only least significant bits will be used. Also
 * provides remaining elements in same page (including the first one) in case
 * caller needs multiple entries.
 */
static inline void *get_q_element(const struct hinic3_queue_pages *qpages,
				  u32 idx, u32 *remaining_in_page)
{
	const struct hinic3_dma_addr_align *page;
	u32 page_idx, elem_idx, elem_per_pg, ofs;
	u8 shift;

	shift = qpages->elem_per_pg_shift;
	page_idx = (idx >> shift) & (qpages->num_pages - 1);
	elem_per_pg = 1 << shift;
	elem_idx = idx & (elem_per_pg - 1);
	if (remaining_in_page)
		*remaining_in_page = elem_per_pg - elem_idx;
	ofs = elem_idx << qpages->elem_size_shift;
	page = qpages->pages + page_idx;
	return (char *)page->align_vaddr + ofs;
}

#endif
