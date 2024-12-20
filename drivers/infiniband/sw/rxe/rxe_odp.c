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

#define RXE_PAGEFAULT_RDONLY BIT(1)
#define RXE_PAGEFAULT_SNAPSHOT BIT(2)
static int rxe_odp_do_pagefault_and_lock(struct rxe_mr *mr, u64 user_va, int bcnt, u32 flags)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	bool fault = !(flags & RXE_PAGEFAULT_SNAPSHOT);
	u64 access_mask;
	int np;

	access_mask = ODP_READ_ALLOWED_BIT;
	if (umem_odp->umem.writable && !(flags & RXE_PAGEFAULT_RDONLY))
		access_mask |= ODP_WRITE_ALLOWED_BIT;

	/*
	 * ib_umem_odp_map_dma_and_lock() locks umem_mutex on success.
	 * Callers must release the lock later to let invalidation handler
	 * do its work again.
	 */
	np = ib_umem_odp_map_dma_and_lock(umem_odp, user_va, bcnt,
					  access_mask, fault);
	return np;
}

static int rxe_odp_init_pages(struct rxe_mr *mr)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	int ret;

	ret = rxe_odp_do_pagefault_and_lock(mr, mr->umem->address,
					    mr->umem->length,
					    RXE_PAGEFAULT_SNAPSHOT);

	if (ret >= 0)
		mutex_unlock(&umem_odp->umem_mutex);

	return ret >= 0 ? 0 : ret;
}

int rxe_odp_mr_init_user(struct rxe_dev *rxe, u64 start, u64 length,
			 u64 iova, int access_flags, struct rxe_mr *mr)
{
	struct ib_umem_odp *umem_odp;
	int err;

	if (!IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
		return -EOPNOTSUPP;

	rxe_mr_init(access_flags, mr);

	if (!start && length == U64_MAX) {
		if (iova != 0)
			return -EINVAL;
		if (!(rxe->attr.odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT))
			return -EINVAL;

		/* Never reach here, for implicit ODP is not implemented. */
	}

	umem_odp = ib_umem_odp_get(&rxe->ib_dev, start, length, access_flags,
				   &rxe_mn_ops);
	if (IS_ERR(umem_odp)) {
		rxe_dbg_mr(mr, "Unable to create umem_odp err = %d\n",
			   (int)PTR_ERR(umem_odp));
		return PTR_ERR(umem_odp);
	}

	umem_odp->private = mr;

	mr->umem = &umem_odp->umem;
	mr->access = access_flags;
	mr->ibmr.length = length;
	mr->ibmr.iova = iova;
	mr->page_offset = ib_umem_offset(&umem_odp->umem);

	err = rxe_odp_init_pages(mr);
	if (err) {
		ib_umem_odp_release(umem_odp);
		return err;
	}

	mr->state = RXE_MR_STATE_VALID;
	mr->ibmr.type = IB_MR_TYPE_USER;

	return err;
}
