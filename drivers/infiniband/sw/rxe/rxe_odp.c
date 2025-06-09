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

#define RXE_PAGEFAULT_DEFAULT 0
#define RXE_PAGEFAULT_RDONLY BIT(0)
#define RXE_PAGEFAULT_SNAPSHOT BIT(1)
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

static inline bool rxe_check_pagefault(struct ib_umem_odp *umem_odp,
				       u64 iova, int length, u32 perm)
{
	bool need_fault = false;
	u64 addr;
	int idx;

	addr = iova & (~(BIT(umem_odp->page_shift) - 1));

	/* Skim through all pages that are to be accessed. */
	while (addr < iova + length) {
		idx = (addr - ib_umem_start(umem_odp)) >> umem_odp->page_shift;

		if (!(umem_odp->dma_list[idx] & perm)) {
			need_fault = true;
			break;
		}

		addr += BIT(umem_odp->page_shift);
	}
	return need_fault;
}

static int rxe_odp_map_range_and_lock(struct rxe_mr *mr, u64 iova, int length, u32 flags)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	bool need_fault;
	u64 perm;
	int err;

	if (unlikely(length < 1))
		return -EINVAL;

	perm = ODP_READ_ALLOWED_BIT;
	if (!(flags & RXE_PAGEFAULT_RDONLY))
		perm |= ODP_WRITE_ALLOWED_BIT;

	mutex_lock(&umem_odp->umem_mutex);

	need_fault = rxe_check_pagefault(umem_odp, iova, length, perm);
	if (need_fault) {
		mutex_unlock(&umem_odp->umem_mutex);

		/* umem_mutex is locked on success. */
		err = rxe_odp_do_pagefault_and_lock(mr, iova, length,
						    flags);
		if (err < 0)
			return err;

		need_fault = rxe_check_pagefault(umem_odp, iova, length, perm);
		if (need_fault)
			return -EFAULT;
	}

	return 0;
}

static int __rxe_odp_mr_copy(struct rxe_mr *mr, u64 iova, void *addr,
			     int length, enum rxe_mr_copy_dir dir)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	struct page *page;
	int idx, bytes;
	size_t offset;
	u8 *user_va;

	idx = (iova - ib_umem_start(umem_odp)) >> umem_odp->page_shift;
	offset = iova & (BIT(umem_odp->page_shift) - 1);

	while (length > 0) {
		u8 *src, *dest;

		page = hmm_pfn_to_page(umem_odp->pfn_list[idx]);
		user_va = kmap_local_page(page);
		if (!user_va)
			return -EFAULT;

		src = (dir == RXE_TO_MR_OBJ) ? addr : user_va;
		dest = (dir == RXE_TO_MR_OBJ) ? user_va : addr;

		bytes = BIT(umem_odp->page_shift) - offset;
		if (bytes > length)
			bytes = length;

		memcpy(dest, src, bytes);
		kunmap_local(user_va);

		length  -= bytes;
		idx++;
		offset = 0;
	}

	return 0;
}

int rxe_odp_mr_copy(struct rxe_mr *mr, u64 iova, void *addr, int length,
		    enum rxe_mr_copy_dir dir)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	u32 flags = RXE_PAGEFAULT_DEFAULT;
	int err;

	if (length == 0)
		return 0;

	if (unlikely(!mr->umem->is_odp))
		return -EOPNOTSUPP;

	switch (dir) {
	case RXE_TO_MR_OBJ:
		break;

	case RXE_FROM_MR_OBJ:
		flags |= RXE_PAGEFAULT_RDONLY;
		break;

	default:
		return -EINVAL;
	}

	err = rxe_odp_map_range_and_lock(mr, iova, length, flags);
	if (err)
		return err;

	err =  __rxe_odp_mr_copy(mr, iova, addr, length, dir);

	mutex_unlock(&umem_odp->umem_mutex);

	return err;
}

static int rxe_odp_do_atomic_op(struct rxe_mr *mr, u64 iova, int opcode,
				u64 compare, u64 swap_add, u64 *orig_val)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	unsigned int page_offset;
	struct page *page;
	unsigned int idx;
	u64 value;
	u64 *va;
	int err;

	if (unlikely(mr->state != RXE_MR_STATE_VALID)) {
		rxe_dbg_mr(mr, "mr not in valid state\n");
		return RESPST_ERR_RKEY_VIOLATION;
	}

	err = mr_check_range(mr, iova, sizeof(value));
	if (err) {
		rxe_dbg_mr(mr, "iova out of range\n");
		return RESPST_ERR_RKEY_VIOLATION;
	}

	idx = (iova - ib_umem_start(umem_odp)) >> umem_odp->page_shift;
	page_offset = iova & (BIT(umem_odp->page_shift) - 1);
	page = hmm_pfn_to_page(umem_odp->pfn_list[idx]);
	if (!page)
		return RESPST_ERR_RKEY_VIOLATION;

	if (unlikely(page_offset & 0x7)) {
		rxe_dbg_mr(mr, "iova not aligned\n");
		return RESPST_ERR_MISALIGNED_ATOMIC;
	}

	va = kmap_local_page(page);

	spin_lock_bh(&atomic_ops_lock);
	value = *orig_val = va[page_offset >> 3];

	if (opcode == IB_OPCODE_RC_COMPARE_SWAP) {
		if (value == compare)
			va[page_offset >> 3] = swap_add;
	} else {
		value += swap_add;
		va[page_offset >> 3] = value;
	}
	spin_unlock_bh(&atomic_ops_lock);

	kunmap_local(va);

	return 0;
}

int rxe_odp_atomic_op(struct rxe_mr *mr, u64 iova, int opcode,
			 u64 compare, u64 swap_add, u64 *orig_val)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	int err;

	err = rxe_odp_map_range_and_lock(mr, iova, sizeof(char),
					 RXE_PAGEFAULT_DEFAULT);
	if (err < 0)
		return RESPST_ERR_RKEY_VIOLATION;

	err = rxe_odp_do_atomic_op(mr, iova, opcode, compare, swap_add,
				   orig_val);
	mutex_unlock(&umem_odp->umem_mutex);

	return err;
}
