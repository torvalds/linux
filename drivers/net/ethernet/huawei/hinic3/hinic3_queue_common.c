// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/device.h>

#include "hinic3_hwdev.h"
#include "hinic3_queue_common.h"

void hinic3_queue_pages_init(struct hinic3_queue_pages *qpages, u32 q_depth,
			     u32 page_size, u32 elem_size)
{
	u32 elem_per_page;

	elem_per_page = min(page_size / elem_size, q_depth);

	qpages->pages = NULL;
	qpages->page_size = page_size;
	qpages->num_pages = max(q_depth / elem_per_page, 1);
	qpages->elem_size_shift = ilog2(elem_size);
	qpages->elem_per_pg_shift = ilog2(elem_per_page);
}

static void __queue_pages_free(struct hinic3_hwdev *hwdev,
			       struct hinic3_queue_pages *qpages, u32 pg_cnt)
{
	while (pg_cnt > 0) {
		pg_cnt--;
		hinic3_dma_free_coherent_align(hwdev->dev,
					       qpages->pages + pg_cnt);
	}
	kfree(qpages->pages);
	qpages->pages = NULL;
}

void hinic3_queue_pages_free(struct hinic3_hwdev *hwdev,
			     struct hinic3_queue_pages *qpages)
{
	__queue_pages_free(hwdev, qpages, qpages->num_pages);
}

int hinic3_queue_pages_alloc(struct hinic3_hwdev *hwdev,
			     struct hinic3_queue_pages *qpages, u32 align)
{
	u32 pg_idx;
	int err;

	qpages->pages = kcalloc(qpages->num_pages, sizeof(qpages->pages[0]),
				GFP_KERNEL);
	if (!qpages->pages)
		return -ENOMEM;

	if (align == 0)
		align = qpages->page_size;

	for (pg_idx = 0; pg_idx < qpages->num_pages; pg_idx++) {
		err = hinic3_dma_zalloc_coherent_align(hwdev->dev,
						       qpages->page_size,
						       align,
						       GFP_KERNEL,
						       qpages->pages + pg_idx);
		if (err) {
			__queue_pages_free(hwdev, qpages, pg_idx);
			return err;
		}
	}

	return 0;
}
