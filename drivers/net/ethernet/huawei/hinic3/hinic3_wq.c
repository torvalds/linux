// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/dma-mapping.h>

#include "hinic3_hwdev.h"
#include "hinic3_wq.h"

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
