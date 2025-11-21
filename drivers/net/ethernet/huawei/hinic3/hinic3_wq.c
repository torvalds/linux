// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/dma-mapping.h>

#include "hinic3_hwdev.h"
#include "hinic3_wq.h"

#define WQ_MIN_DEPTH            64
#define WQ_MAX_DEPTH            65536
#define WQ_PAGE_ADDR_SIZE       sizeof(u64)
#define WQ_MAX_NUM_PAGES        (HINIC3_MIN_PAGE_SIZE / WQ_PAGE_ADDR_SIZE)

static int wq_init_wq_block(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq)
{
	struct hinic3_queue_pages *qpages = &wq->qpages;
	int i;

	if (hinic3_wq_is_0_level_cla(wq)) {
		wq->wq_block_paddr = qpages->pages[0].align_paddr;
		wq->wq_block_vaddr = qpages->pages[0].align_vaddr;

		return 0;
	}

	if (wq->qpages.num_pages > WQ_MAX_NUM_PAGES) {
		dev_err(hwdev->dev, "wq num_pages exceed limit: %lu\n",
			WQ_MAX_NUM_PAGES);
		return -EFAULT;
	}

	wq->wq_block_vaddr = dma_alloc_coherent(hwdev->dev,
						HINIC3_MIN_PAGE_SIZE,
						&wq->wq_block_paddr,
						GFP_KERNEL);
	if (!wq->wq_block_vaddr)
		return -ENOMEM;

	for (i = 0; i < qpages->num_pages; i++)
		wq->wq_block_vaddr[i] = cpu_to_be64(qpages->pages[i].align_paddr);

	return 0;
}

static int wq_alloc_pages(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq)
{
	int err;

	err = hinic3_queue_pages_alloc(hwdev, &wq->qpages, 0);
	if (err)
		return err;

	err = wq_init_wq_block(hwdev, wq);
	if (err) {
		hinic3_queue_pages_free(hwdev, &wq->qpages);
		return err;
	}

	return 0;
}

static void wq_free_pages(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq)
{
	if (!hinic3_wq_is_0_level_cla(wq))
		dma_free_coherent(hwdev->dev,
				  HINIC3_MIN_PAGE_SIZE,
				  wq->wq_block_vaddr,
				  wq->wq_block_paddr);

	hinic3_queue_pages_free(hwdev, &wq->qpages);
}

int hinic3_wq_create(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq,
		     u32 q_depth, u16 wqebb_size)
{
	u32 wq_page_size;

	if (q_depth < WQ_MIN_DEPTH || q_depth > WQ_MAX_DEPTH ||
	    !is_power_of_2(q_depth) || !is_power_of_2(wqebb_size)) {
		dev_err(hwdev->dev, "Invalid WQ: q_depth %u, wqebb_size %u\n",
			q_depth, wqebb_size);
		return -EINVAL;
	}

	wq_page_size = ALIGN(hwdev->wq_page_size, HINIC3_MIN_PAGE_SIZE);

	memset(wq, 0, sizeof(*wq));
	wq->q_depth = q_depth;
	wq->idx_mask = q_depth - 1;

	hinic3_queue_pages_init(&wq->qpages, q_depth, wq_page_size, wqebb_size);

	return wq_alloc_pages(hwdev, wq);
}

void hinic3_wq_destroy(struct hinic3_hwdev *hwdev, struct hinic3_wq *wq)
{
	wq_free_pages(hwdev, wq);
}

void hinic3_wq_reset(struct hinic3_wq *wq)
{
	struct hinic3_queue_pages *qpages = &wq->qpages;
	u16 pg_idx;

	wq->cons_idx = 0;
	wq->prod_idx = 0;

	for (pg_idx = 0; pg_idx < qpages->num_pages; pg_idx++)
		memset(qpages->pages[pg_idx].align_vaddr, 0, qpages->page_size);
}

void hinic3_wq_get_multi_wqebbs(struct hinic3_wq *wq,
				u16 num_wqebbs, u16 *prod_idx,
				struct hinic3_sq_bufdesc **first_part_wqebbs,
				struct hinic3_sq_bufdesc **second_part_wqebbs,
				u16 *first_part_wqebbs_num)
{
	u32 idx, remaining;

	idx = wq->prod_idx & wq->idx_mask;
	wq->prod_idx += num_wqebbs;
	*prod_idx = idx;
	*first_part_wqebbs = get_q_element(&wq->qpages, idx, &remaining);
	if (likely(remaining >= num_wqebbs)) {
		*first_part_wqebbs_num = num_wqebbs;
		*second_part_wqebbs = NULL;
	} else {
		*first_part_wqebbs_num = remaining;
		idx += remaining;
		*second_part_wqebbs = get_q_element(&wq->qpages, idx, NULL);
	}
}

bool hinic3_wq_is_0_level_cla(const struct hinic3_wq *wq)
{
	return wq->qpages.num_pages == 1;
}
