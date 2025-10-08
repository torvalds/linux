// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2022-2023 Fujitsu Ltd. All rights reserved.
 */

#include <linux/hmm.h>
#include <linux/libnvdimm.h>

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

	/* update umem_odp->map.pfn_list */
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
	u64 access_mask = 0;
	int np;

	if (umem_odp->umem.writable && !(flags & RXE_PAGEFAULT_RDONLY))
		access_mask |= HMM_PFN_WRITE;

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

static inline bool rxe_check_pagefault(struct ib_umem_odp *umem_odp, u64 iova,
				       int length)
{
	bool need_fault = false;
	u64 addr;
	int idx;

	addr = iova & (~(BIT(umem_odp->page_shift) - 1));

	/* Skim through all pages that are to be accessed. */
	while (addr < iova + length) {
		idx = (addr - ib_umem_start(umem_odp)) >> umem_odp->page_shift;

		if (!(umem_odp->map.pfn_list[idx] & HMM_PFN_VALID)) {
			need_fault = true;
			break;
		}

		addr += BIT(umem_odp->page_shift);
	}
	return need_fault;
}

static unsigned long rxe_odp_iova_to_index(struct ib_umem_odp *umem_odp, u64 iova)
{
	return (iova - ib_umem_start(umem_odp)) >> umem_odp->page_shift;
}

static unsigned long rxe_odp_iova_to_page_offset(struct ib_umem_odp *umem_odp, u64 iova)
{
	return iova & (BIT(umem_odp->page_shift) - 1);
}

static int rxe_odp_map_range_and_lock(struct rxe_mr *mr, u64 iova, int length, u32 flags)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	bool need_fault;
	int err;

	if (unlikely(length < 1))
		return -EINVAL;

	mutex_lock(&umem_odp->umem_mutex);

	need_fault = rxe_check_pagefault(umem_odp, iova, length);
	if (need_fault) {
		mutex_unlock(&umem_odp->umem_mutex);

		/* umem_mutex is locked on success. */
		err = rxe_odp_do_pagefault_and_lock(mr, iova, length,
						    flags);
		if (err < 0)
			return err;

		need_fault = rxe_check_pagefault(umem_odp, iova, length);
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

	idx = rxe_odp_iova_to_index(umem_odp, iova);
	offset = rxe_odp_iova_to_page_offset(umem_odp, iova);

	while (length > 0) {
		u8 *src, *dest;

		page = hmm_pfn_to_page(umem_odp->map.pfn_list[idx]);
		user_va = kmap_local_page(page);

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

static enum resp_states rxe_odp_do_atomic_op(struct rxe_mr *mr, u64 iova,
					     int opcode, u64 compare,
					     u64 swap_add, u64 *orig_val)
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

	page_offset = rxe_odp_iova_to_page_offset(umem_odp, iova);
	if (unlikely(page_offset & 0x7)) {
		rxe_dbg_mr(mr, "iova not aligned\n");
		return RESPST_ERR_MISALIGNED_ATOMIC;
	}

	idx = rxe_odp_iova_to_index(umem_odp, iova);
	page = hmm_pfn_to_page(umem_odp->map.pfn_list[idx]);

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

	return RESPST_NONE;
}

enum resp_states rxe_odp_atomic_op(struct rxe_mr *mr, u64 iova, int opcode,
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

int rxe_odp_flush_pmem_iova(struct rxe_mr *mr, u64 iova,
			    unsigned int length)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	unsigned int page_offset;
	unsigned long index;
	struct page *page;
	unsigned int bytes;
	int err;
	u8 *va;

	err = rxe_odp_map_range_and_lock(mr, iova, length,
					 RXE_PAGEFAULT_DEFAULT);
	if (err)
		return err;

	while (length > 0) {
		index = rxe_odp_iova_to_index(umem_odp, iova);
		page_offset = rxe_odp_iova_to_page_offset(umem_odp, iova);

		page = hmm_pfn_to_page(umem_odp->map.pfn_list[index]);

		bytes = min_t(unsigned int, length,
			      mr_page_size(mr) - page_offset);

		va = kmap_local_page(page);
		arch_wb_cache_pmem(va + page_offset, bytes);
		kunmap_local(va);

		length -= bytes;
		iova += bytes;
		page_offset = 0;
	}

	mutex_unlock(&umem_odp->umem_mutex);

	return 0;
}

enum resp_states rxe_odp_do_atomic_write(struct rxe_mr *mr, u64 iova, u64 value)
{
	struct ib_umem_odp *umem_odp = to_ib_umem_odp(mr->umem);
	unsigned int page_offset;
	unsigned long index;
	struct page *page;
	int err;
	u64 *va;

	/* See IBA oA19-28 */
	err = mr_check_range(mr, iova, sizeof(value));
	if (unlikely(err)) {
		rxe_dbg_mr(mr, "iova out of range\n");
		return RESPST_ERR_RKEY_VIOLATION;
	}

	err = rxe_odp_map_range_and_lock(mr, iova, sizeof(value),
					 RXE_PAGEFAULT_DEFAULT);
	if (err)
		return RESPST_ERR_RKEY_VIOLATION;

	page_offset = rxe_odp_iova_to_page_offset(umem_odp, iova);
	/* See IBA A19.4.2 */
	if (unlikely(page_offset & 0x7)) {
		mutex_unlock(&umem_odp->umem_mutex);
		rxe_dbg_mr(mr, "misaligned address\n");
		return RESPST_ERR_MISALIGNED_ATOMIC;
	}

	index = rxe_odp_iova_to_index(umem_odp, iova);
	page = hmm_pfn_to_page(umem_odp->map.pfn_list[index]);

	va = kmap_local_page(page);
	/* Do atomic write after all prior operations have completed */
	smp_store_release(&va[page_offset >> 3], value);
	kunmap_local(va);

	mutex_unlock(&umem_odp->umem_mutex);

	return RESPST_NONE;
}

struct prefetch_mr_work {
	struct work_struct work;
	u32 pf_flags;
	u32 num_sge;
	struct {
		u64 io_virt;
		struct rxe_mr *mr;
		size_t length;
	} frags[];
};

static void rxe_ib_prefetch_mr_work(struct work_struct *w)
{
	struct prefetch_mr_work *work =
		container_of(w, struct prefetch_mr_work, work);
	int ret;
	u32 i;

	/*
	 * We rely on IB/core that work is executed
	 * if we have num_sge != 0 only.
	 */
	WARN_ON(!work->num_sge);
	for (i = 0; i < work->num_sge; ++i) {
		struct ib_umem_odp *umem_odp;

		ret = rxe_odp_do_pagefault_and_lock(work->frags[i].mr,
						    work->frags[i].io_virt,
						    work->frags[i].length,
						    work->pf_flags);
		if (ret < 0) {
			rxe_dbg_mr(work->frags[i].mr,
				   "failed to prefetch the mr\n");
			goto deref;
		}

		umem_odp = to_ib_umem_odp(work->frags[i].mr->umem);
		mutex_unlock(&umem_odp->umem_mutex);

deref:
		rxe_put(work->frags[i].mr);
	}

	kvfree(work);
}

static int rxe_ib_prefetch_sg_list(struct ib_pd *ibpd,
				   enum ib_uverbs_advise_mr_advice advice,
				   u32 pf_flags, struct ib_sge *sg_list,
				   u32 num_sge)
{
	struct rxe_pd *pd = container_of(ibpd, struct rxe_pd, ibpd);
	int ret = 0;
	u32 i;

	for (i = 0; i < num_sge; ++i) {
		struct rxe_mr *mr;
		struct ib_umem_odp *umem_odp;

		mr = lookup_mr(pd, IB_ACCESS_LOCAL_WRITE,
			       sg_list[i].lkey, RXE_LOOKUP_LOCAL);

		if (!mr) {
			rxe_dbg_pd(pd, "mr with lkey %x not found\n",
				   sg_list[i].lkey);
			return -EINVAL;
		}

		if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE &&
		    !mr->umem->writable) {
			rxe_dbg_mr(mr, "missing write permission\n");
			rxe_put(mr);
			return -EPERM;
		}

		ret = rxe_odp_do_pagefault_and_lock(
			mr, sg_list[i].addr, sg_list[i].length, pf_flags);
		if (ret < 0) {
			rxe_dbg_mr(mr, "failed to prefetch the mr\n");
			rxe_put(mr);
			return ret;
		}

		umem_odp = to_ib_umem_odp(mr->umem);
		mutex_unlock(&umem_odp->umem_mutex);

		rxe_put(mr);
	}

	return 0;
}

static int rxe_ib_advise_mr_prefetch(struct ib_pd *ibpd,
				     enum ib_uverbs_advise_mr_advice advice,
				     u32 flags, struct ib_sge *sg_list,
				     u32 num_sge)
{
	struct rxe_pd *pd = container_of(ibpd, struct rxe_pd, ibpd);
	u32 pf_flags = RXE_PAGEFAULT_DEFAULT;
	struct prefetch_mr_work *work;
	struct rxe_mr *mr;
	u32 i;

	if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH)
		pf_flags |= RXE_PAGEFAULT_RDONLY;

	if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT)
		pf_flags |= RXE_PAGEFAULT_SNAPSHOT;

	/* Synchronous call */
	if (flags & IB_UVERBS_ADVISE_MR_FLAG_FLUSH)
		return rxe_ib_prefetch_sg_list(ibpd, advice, pf_flags, sg_list,
					       num_sge);

	/* Asynchronous call is "best-effort" and allowed to fail */
	work = kvzalloc(struct_size(work, frags, num_sge), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	INIT_WORK(&work->work, rxe_ib_prefetch_mr_work);
	work->pf_flags = pf_flags;
	work->num_sge = num_sge;

	for (i = 0; i < num_sge; ++i) {
		/* Takes a reference, which will be released in the queued work */
		mr = lookup_mr(pd, IB_ACCESS_LOCAL_WRITE,
			       sg_list[i].lkey, RXE_LOOKUP_LOCAL);
		if (!mr) {
			mr = ERR_PTR(-EINVAL);
			goto err;
		}

		work->frags[i].io_virt = sg_list[i].addr;
		work->frags[i].length = sg_list[i].length;
		work->frags[i].mr = mr;
	}

	queue_work(system_unbound_wq, &work->work);

	return 0;

 err:
	/* rollback reference counts for the invalid request */
	while (i > 0) {
		i--;
		rxe_put(work->frags[i].mr);
	}

	kvfree(work);

	return PTR_ERR(mr);
}

int rxe_ib_advise_mr(struct ib_pd *ibpd,
		     enum ib_uverbs_advise_mr_advice advice,
		     u32 flags,
		     struct ib_sge *sg_list,
		     u32 num_sge,
		     struct uverbs_attr_bundle *attrs)
{
	if (advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH &&
	    advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE &&
	    advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT)
		return -EOPNOTSUPP;

	return rxe_ib_advise_mr_prefetch(ibpd, advice, flags,
					 sg_list, num_sge);
}
