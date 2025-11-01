/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_WQ_H_
#define _HINIC3_WQ_H_

#include <linux/io.h>

#include "hinic3_queue_common.h"

struct hinic3_sq_bufdesc {
	/* 31-bits Length, L2NIC only uses length[17:0] */
	__le32 len;
	__le32 rsvd;
	__le32 hi_addr;
	__le32 lo_addr;
};

/* Work queue is used to submit elements (tx, rx, cmd) to hw.
 * Driver is the producer that advances prod_idx. cons_idx is advanced when
 * HW reports completions of previously submitted elements.
 */
struct hinic3_wq {
	struct hinic3_queue_pages qpages;
	/* Unmasked producer/consumer indices that are advanced to natural
	 * integer overflow regardless of queue depth.
	 */
	u16                       cons_idx;
	u16                       prod_idx;

	u32                       q_depth;
	u16                       idx_mask;

	/* Work Queue (logical WQEBB array) is mapped to hw via Chip Logical
	 * Address (CLA) using 1 of 2 levels:
	 *     level 0 - direct mapping of single wq page
	 *     level 1 - indirect mapping of multiple pages via additional page
	 *               table.
	 * When wq uses level 1, wq_block will hold the allocated indirection
	 * table.
	 */
	dma_addr_t                wq_block_paddr;
	__be64                    *wq_block_vaddr;
} ____cacheline_aligned;

/* Get number of elements in work queue that are in-use. */
static inline u16 hinic3_wq_get_used(const struct hinic3_wq *wq)
{
	return READ_ONCE(wq->prod_idx) - READ_ONCE(wq->cons_idx);
}

static inline u16 hinic3_wq_free_wqebbs(struct hinic3_wq *wq)
{
	/* Don't allow queue to become completely full, report (free - 1). */
	return wq->q_depth - hinic3_wq_get_used(wq) - 1;
}

static inline void *hinic3_wq_get_one_wqebb(struct hinic3_wq *wq, u16 *pi)
{
	*pi = wq->prod_idx & wq->idx_mask;
	wq->prod_idx++;

	return get_q_element(&wq->qpages, *pi, NULL);
}

static inline void hinic3_wq_put_wqebbs(struct hinic3_wq *wq, u16 num_wqebbs)
{
	wq->cons_idx += num_wqebbs;
}

static inline u64 hinic3_wq_get_first_wqe_page_addr(const struct hinic3_wq *wq)
{
	return wq->qpages.pages[0].align_paddr;
}

int hinic3_wq_create(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq,
		     u32 q_depth, u16 wqebb_size);
void hinic3_wq_destroy(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq);
void hinic3_wq_reset(struct hinic3_wq *wq);
void hinic3_wq_get_multi_wqebbs(struct hinic3_wq *wq,
				u16 num_wqebbs, u16 *prod_idx,
				struct hinic3_sq_bufdesc **first_part_wqebbs,
				struct hinic3_sq_bufdesc **second_part_wqebbs,
				u16 *first_part_wqebbs_num);
bool hinic3_wq_is_0_level_cla(const struct hinic3_wq *wq);

#endif
