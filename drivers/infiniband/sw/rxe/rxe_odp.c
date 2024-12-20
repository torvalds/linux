// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2022-2023 Fujitsu Ltd. All rights reserved.
 */

#include <linux/hmm.h>

#include <rdma/ib_umem_odp.h>

#include "rxe.h"

static bool rxe_ib_invalidate_range(struct mmu_interval_notifier *mni,
				    const struct mmu_notifier_range *range,
				    unsigned long cur_seq)
{
	struct ib_umem_odp *umem_odp =
		container_of(mni, struct ib_umem_odp, notifier);
	unsigned long start, end;

	if (!mmu_notifier_range_blockable(range))
		return false;

	mutex_lock(&umem_odp->umem_mutex);
	mmu_interval_set_seq(mni, cur_seq);

	start = max_t(u64, ib_umem_start(umem_odp), range->start);
	end = min_t(u64, ib_umem_end(umem_odp), range->end);

	/* update umem_odp->dma_list */
	ib_umem_odp_unmap_dma_pages(umem_odp, start, end);

	mutex_unlock(&umem_odp->umem_mutex);
	return true;
}

const struct mmu_interval_notifier_ops rxe_mn_ops = {
	.invalidate = rxe_ib_invalidate_range,
};
