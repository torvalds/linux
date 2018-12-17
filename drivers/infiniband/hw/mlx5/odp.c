/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>
#include <linux/kernel.h>

#include "mlx5_ib.h"
#include "cmd.h"

#define MAX_PREFETCH_LEN (4*1024*1024U)

/* Timeout in ms to wait for an active mmu notifier to complete when handling
 * a pagefault. */
#define MMU_NOTIFIER_TIMEOUT 1000

#define MLX5_IMR_MTT_BITS (30 - PAGE_SHIFT)
#define MLX5_IMR_MTT_SHIFT (MLX5_IMR_MTT_BITS + PAGE_SHIFT)
#define MLX5_IMR_MTT_ENTRIES BIT_ULL(MLX5_IMR_MTT_BITS)
#define MLX5_IMR_MTT_SIZE BIT_ULL(MLX5_IMR_MTT_SHIFT)
#define MLX5_IMR_MTT_MASK (~(MLX5_IMR_MTT_SIZE - 1))

#define MLX5_KSM_PAGE_SHIFT MLX5_IMR_MTT_SHIFT

static u64 mlx5_imr_ksm_entries;

static int check_parent(struct ib_umem_odp *odp,
			       struct mlx5_ib_mr *parent)
{
	struct mlx5_ib_mr *mr = odp->private;

	return mr && mr->parent == parent && !odp->dying;
}

static struct ib_umem_odp *odp_next(struct ib_umem_odp *odp)
{
	struct mlx5_ib_mr *mr = odp->private, *parent = mr->parent;
	struct ib_ucontext *ctx = odp->umem->context;
	struct rb_node *rb;

	down_read(&ctx->umem_rwsem);
	while (1) {
		rb = rb_next(&odp->interval_tree.rb);
		if (!rb)
			goto not_found;
		odp = rb_entry(rb, struct ib_umem_odp, interval_tree.rb);
		if (check_parent(odp, parent))
			goto end;
	}
not_found:
	odp = NULL;
end:
	up_read(&ctx->umem_rwsem);
	return odp;
}

static struct ib_umem_odp *odp_lookup(struct ib_ucontext *ctx,
				      u64 start, u64 length,
				      struct mlx5_ib_mr *parent)
{
	struct ib_umem_odp *odp;
	struct rb_node *rb;

	down_read(&ctx->umem_rwsem);
	odp = rbt_ib_umem_lookup(&ctx->umem_tree, start, length);
	if (!odp)
		goto end;

	while (1) {
		if (check_parent(odp, parent))
			goto end;
		rb = rb_next(&odp->interval_tree.rb);
		if (!rb)
			goto not_found;
		odp = rb_entry(rb, struct ib_umem_odp, interval_tree.rb);
		if (ib_umem_start(odp->umem) > start + length)
			goto not_found;
	}
not_found:
	odp = NULL;
end:
	up_read(&ctx->umem_rwsem);
	return odp;
}

void mlx5_odp_populate_klm(struct mlx5_klm *pklm, size_t offset,
			   size_t nentries, struct mlx5_ib_mr *mr, int flags)
{
	struct ib_pd *pd = mr->ibmr.pd;
	struct ib_ucontext *ctx = pd->uobject->context;
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct ib_umem_odp *odp;
	unsigned long va;
	int i;

	if (flags & MLX5_IB_UPD_XLT_ZAP) {
		for (i = 0; i < nentries; i++, pklm++) {
			pklm->bcount = cpu_to_be32(MLX5_IMR_MTT_SIZE);
			pklm->key = cpu_to_be32(dev->null_mkey);
			pklm->va = 0;
		}
		return;
	}

	odp = odp_lookup(ctx, offset * MLX5_IMR_MTT_SIZE,
			     nentries * MLX5_IMR_MTT_SIZE, mr);

	for (i = 0; i < nentries; i++, pklm++) {
		pklm->bcount = cpu_to_be32(MLX5_IMR_MTT_SIZE);
		va = (offset + i) * MLX5_IMR_MTT_SIZE;
		if (odp && odp->umem->address == va) {
			struct mlx5_ib_mr *mtt = odp->private;

			pklm->key = cpu_to_be32(mtt->ibmr.lkey);
			odp = odp_next(odp);
		} else {
			pklm->key = cpu_to_be32(dev->null_mkey);
		}
		mlx5_ib_dbg(dev, "[%d] va %lx key %x\n",
			    i, va, be32_to_cpu(pklm->key));
	}
}

static void mr_leaf_free_action(struct work_struct *work)
{
	struct ib_umem_odp *odp = container_of(work, struct ib_umem_odp, work);
	int idx = ib_umem_start(odp->umem) >> MLX5_IMR_MTT_SHIFT;
	struct mlx5_ib_mr *mr = odp->private, *imr = mr->parent;

	mr->parent = NULL;
	synchronize_srcu(&mr->dev->mr_srcu);

	ib_umem_release(odp->umem);
	if (imr->live)
		mlx5_ib_update_xlt(imr, idx, 1, 0,
				   MLX5_IB_UPD_XLT_INDIRECT |
				   MLX5_IB_UPD_XLT_ATOMIC);
	mlx5_mr_cache_free(mr->dev, mr);

	if (atomic_dec_and_test(&imr->num_leaf_free))
		wake_up(&imr->q_leaf_free);
}

void mlx5_ib_invalidate_range(struct ib_umem *umem, unsigned long start,
			      unsigned long end)
{
	struct mlx5_ib_mr *mr;
	const u64 umr_block_mask = (MLX5_UMR_MTT_ALIGNMENT /
				    sizeof(struct mlx5_mtt)) - 1;
	u64 idx = 0, blk_start_idx = 0;
	int in_block = 0;
	u64 addr;

	if (!umem || !umem->odp_data) {
		pr_err("invalidation called on NULL umem or non-ODP umem\n");
		return;
	}

	mr = umem->odp_data->private;

	if (!mr || !mr->ibmr.pd)
		return;

	start = max_t(u64, ib_umem_start(umem), start);
	end = min_t(u64, ib_umem_end(umem), end);

	/*
	 * Iteration one - zap the HW's MTTs. The notifiers_count ensures that
	 * while we are doing the invalidation, no page fault will attempt to
	 * overwrite the same MTTs.  Concurent invalidations might race us,
	 * but they will write 0s as well, so no difference in the end result.
	 */

	for (addr = start; addr < end; addr += BIT(umem->page_shift)) {
		idx = (addr - ib_umem_start(umem)) >> umem->page_shift;
		/*
		 * Strive to write the MTTs in chunks, but avoid overwriting
		 * non-existing MTTs. The huristic here can be improved to
		 * estimate the cost of another UMR vs. the cost of bigger
		 * UMR.
		 */
		if (umem->odp_data->dma_list[idx] &
		    (ODP_READ_ALLOWED_BIT | ODP_WRITE_ALLOWED_BIT)) {
			if (!in_block) {
				blk_start_idx = idx;
				in_block = 1;
			}
		} else {
			u64 umr_offset = idx & umr_block_mask;

			if (in_block && umr_offset == 0) {
				mlx5_ib_update_xlt(mr, blk_start_idx,
						   idx - blk_start_idx, 0,
						   MLX5_IB_UPD_XLT_ZAP |
						   MLX5_IB_UPD_XLT_ATOMIC);
				in_block = 0;
			}
		}
	}
	if (in_block)
		mlx5_ib_update_xlt(mr, blk_start_idx,
				   idx - blk_start_idx + 1, 0,
				   MLX5_IB_UPD_XLT_ZAP |
				   MLX5_IB_UPD_XLT_ATOMIC);
	/*
	 * We are now sure that the device will not access the
	 * memory. We can safely unmap it, and mark it as dirty if
	 * needed.
	 */

	ib_umem_odp_unmap_dma_pages(umem, start, end);

	if (unlikely(!umem->npages && mr->parent &&
		     !umem->odp_data->dying)) {
		WRITE_ONCE(umem->odp_data->dying, 1);
		atomic_inc(&mr->parent->num_leaf_free);
		schedule_work(&umem->odp_data->work);
	}
}

void mlx5_ib_internal_fill_odp_caps(struct mlx5_ib_dev *dev)
{
	struct ib_odp_caps *caps = &dev->odp_caps;

	memset(caps, 0, sizeof(*caps));

	if (!MLX5_CAP_GEN(dev->mdev, pg))
		return;

	caps->general_caps = IB_ODP_SUPPORT;

	if (MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset))
		dev->odp_max_size = U64_MAX;
	else
		dev->odp_max_size = BIT_ULL(MLX5_MAX_UMR_SHIFT + PAGE_SHIFT);

	if (MLX5_CAP_ODP(dev->mdev, ud_odp_caps.send))
		caps->per_transport_caps.ud_odp_caps |= IB_ODP_SUPPORT_SEND;

	if (MLX5_CAP_ODP(dev->mdev, rc_odp_caps.send))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_SEND;

	if (MLX5_CAP_ODP(dev->mdev, rc_odp_caps.receive))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_RECV;

	if (MLX5_CAP_ODP(dev->mdev, rc_odp_caps.write))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_WRITE;

	if (MLX5_CAP_ODP(dev->mdev, rc_odp_caps.read))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_READ;

	if (MLX5_CAP_ODP(dev->mdev, rc_odp_caps.atomic))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_ATOMIC;

	if (MLX5_CAP_GEN(dev->mdev, fixed_buffer_size) &&
	    MLX5_CAP_GEN(dev->mdev, null_mkey) &&
	    MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset))
		caps->general_caps |= IB_ODP_SUPPORT_IMPLICIT;

	return;
}

static void mlx5_ib_page_fault_resume(struct mlx5_ib_dev *dev,
				      struct mlx5_pagefault *pfault,
				      int error)
{
	int wq_num = pfault->event_subtype == MLX5_PFAULT_SUBTYPE_WQE ?
		     pfault->wqe.wq_num : pfault->token;
	int ret = mlx5_core_page_fault_resume(dev->mdev,
					      pfault->token,
					      wq_num,
					      pfault->type,
					      error);
	if (ret)
		mlx5_ib_err(dev, "Failed to resolve the page fault on WQ 0x%x\n",
			    wq_num);
}

static struct mlx5_ib_mr *implicit_mr_alloc(struct ib_pd *pd,
					    struct ib_umem *umem,
					    bool ksm, int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr;
	int err;

	mr = mlx5_mr_cache_alloc(dev, ksm ? MLX5_IMR_KSM_CACHE_ENTRY :
					    MLX5_IMR_MTT_CACHE_ENTRY);

	if (IS_ERR(mr))
		return mr;

	mr->ibmr.pd = pd;

	mr->dev = dev;
	mr->access_flags = access_flags;
	mr->mmkey.iova = 0;
	mr->umem = umem;

	if (ksm) {
		err = mlx5_ib_update_xlt(mr, 0,
					 mlx5_imr_ksm_entries,
					 MLX5_KSM_PAGE_SHIFT,
					 MLX5_IB_UPD_XLT_INDIRECT |
					 MLX5_IB_UPD_XLT_ZAP |
					 MLX5_IB_UPD_XLT_ENABLE);

	} else {
		err = mlx5_ib_update_xlt(mr, 0,
					 MLX5_IMR_MTT_ENTRIES,
					 PAGE_SHIFT,
					 MLX5_IB_UPD_XLT_ZAP |
					 MLX5_IB_UPD_XLT_ENABLE |
					 MLX5_IB_UPD_XLT_ATOMIC);
	}

	if (err)
		goto fail;

	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;

	mr->live = 1;

	mlx5_ib_dbg(dev, "key %x dev %p mr %p\n",
		    mr->mmkey.key, dev->mdev, mr);

	return mr;

fail:
	mlx5_ib_err(dev, "Failed to register MKEY %d\n", err);
	mlx5_mr_cache_free(dev, mr);

	return ERR_PTR(err);
}

static struct ib_umem_odp *implicit_mr_get_data(struct mlx5_ib_mr *mr,
						u64 io_virt, size_t bcnt)
{
	struct ib_ucontext *ctx = mr->ibmr.pd->uobject->context;
	struct mlx5_ib_dev *dev = to_mdev(mr->ibmr.pd->device);
	struct ib_umem_odp *odp, *result = NULL;
	u64 addr = io_virt & MLX5_IMR_MTT_MASK;
	int nentries = 0, start_idx = 0, ret;
	struct mlx5_ib_mr *mtt;
	struct ib_umem *umem;

	mutex_lock(&mr->umem->odp_data->umem_mutex);
	odp = odp_lookup(ctx, addr, 1, mr);

	mlx5_ib_dbg(dev, "io_virt:%llx bcnt:%zx addr:%llx odp:%p\n",
		    io_virt, bcnt, addr, odp);

next_mr:
	if (likely(odp)) {
		if (nentries)
			nentries++;
	} else {
		umem = ib_alloc_odp_umem(ctx, addr, MLX5_IMR_MTT_SIZE);
		if (IS_ERR(umem)) {
			mutex_unlock(&mr->umem->odp_data->umem_mutex);
			return ERR_CAST(umem);
		}

		mtt = implicit_mr_alloc(mr->ibmr.pd, umem, 0, mr->access_flags);
		if (IS_ERR(mtt)) {
			mutex_unlock(&mr->umem->odp_data->umem_mutex);
			ib_umem_release(umem);
			return ERR_CAST(mtt);
		}

		odp = umem->odp_data;
		odp->private = mtt;
		mtt->umem = umem;
		mtt->mmkey.iova = addr;
		mtt->parent = mr;
		INIT_WORK(&odp->work, mr_leaf_free_action);

		if (!nentries)
			start_idx = addr >> MLX5_IMR_MTT_SHIFT;
		nentries++;
	}

	/* Return first odp if region not covered by single one */
	if (likely(!result))
		result = odp;

	addr += MLX5_IMR_MTT_SIZE;
	if (unlikely(addr < io_virt + bcnt)) {
		odp = odp_next(odp);
		if (odp && odp->umem->address != addr)
			odp = NULL;
		goto next_mr;
	}

	if (unlikely(nentries)) {
		ret = mlx5_ib_update_xlt(mr, start_idx, nentries, 0,
					 MLX5_IB_UPD_XLT_INDIRECT |
					 MLX5_IB_UPD_XLT_ATOMIC);
		if (ret) {
			mlx5_ib_err(dev, "Failed to update PAS\n");
			result = ERR_PTR(ret);
		}
	}

	mutex_unlock(&mr->umem->odp_data->umem_mutex);
	return result;
}

struct mlx5_ib_mr *mlx5_ib_alloc_implicit_mr(struct mlx5_ib_pd *pd,
					     int access_flags)
{
	struct ib_ucontext *ctx = pd->ibpd.uobject->context;
	struct mlx5_ib_mr *imr;
	struct ib_umem *umem;

	umem = ib_umem_get(ctx, 0, 0, IB_ACCESS_ON_DEMAND, 0);
	if (IS_ERR(umem))
		return ERR_CAST(umem);

	imr = implicit_mr_alloc(&pd->ibpd, umem, 1, access_flags);
	if (IS_ERR(imr)) {
		ib_umem_release(umem);
		return ERR_CAST(imr);
	}

	imr->umem = umem;
	init_waitqueue_head(&imr->q_leaf_free);
	atomic_set(&imr->num_leaf_free, 0);

	return imr;
}

static int mr_leaf_free(struct ib_umem *umem, u64 start,
			u64 end, void *cookie)
{
	struct mlx5_ib_mr *mr = umem->odp_data->private, *imr = cookie;

	if (mr->parent != imr)
		return 0;

	ib_umem_odp_unmap_dma_pages(umem,
				    ib_umem_start(umem),
				    ib_umem_end(umem));

	if (umem->odp_data->dying)
		return 0;

	WRITE_ONCE(umem->odp_data->dying, 1);
	atomic_inc(&imr->num_leaf_free);
	schedule_work(&umem->odp_data->work);

	return 0;
}

void mlx5_ib_free_implicit_mr(struct mlx5_ib_mr *imr)
{
	struct ib_ucontext *ctx = imr->ibmr.pd->uobject->context;

	down_read(&ctx->umem_rwsem);
	rbt_ib_umem_for_each_in_range(&ctx->umem_tree, 0, ULLONG_MAX,
				      mr_leaf_free, true, imr);
	up_read(&ctx->umem_rwsem);

	wait_event(imr->q_leaf_free, !atomic_read(&imr->num_leaf_free));
}

static int pagefault_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr,
			u64 io_virt, size_t bcnt, u32 *bytes_mapped)
{
	u64 access_mask = ODP_READ_ALLOWED_BIT;
	int npages = 0, page_shift, np;
	u64 start_idx, page_mask;
	struct ib_umem_odp *odp;
	int current_seq;
	size_t size;
	int ret;

	if (!mr->umem->odp_data->page_list) {
		odp = implicit_mr_get_data(mr, io_virt, bcnt);

		if (IS_ERR(odp))
			return PTR_ERR(odp);
		mr = odp->private;

	} else {
		odp = mr->umem->odp_data;
	}

next_mr:
	size = min_t(size_t, bcnt, ib_umem_end(odp->umem) - io_virt);

	page_shift = mr->umem->page_shift;
	page_mask = ~(BIT(page_shift) - 1);
	start_idx = (io_virt - (mr->mmkey.iova & page_mask)) >> page_shift;

	if (mr->umem->writable)
		access_mask |= ODP_WRITE_ALLOWED_BIT;

	current_seq = READ_ONCE(odp->notifiers_seq);
	/*
	 * Ensure the sequence number is valid for some time before we call
	 * gup.
	 */
	smp_rmb();

	ret = ib_umem_odp_map_dma_pages(mr->umem, io_virt, size,
					access_mask, current_seq);

	if (ret < 0)
		goto out;

	np = ret;

	mutex_lock(&odp->umem_mutex);
	if (!ib_umem_mmu_notifier_retry(mr->umem, current_seq)) {
		/*
		 * No need to check whether the MTTs really belong to
		 * this MR, since ib_umem_odp_map_dma_pages already
		 * checks this.
		 */
		ret = mlx5_ib_update_xlt(mr, start_idx, np,
					 page_shift, MLX5_IB_UPD_XLT_ATOMIC);
	} else {
		ret = -EAGAIN;
	}
	mutex_unlock(&odp->umem_mutex);

	if (ret < 0) {
		if (ret != -EAGAIN)
			mlx5_ib_err(dev, "Failed to update mkey page tables\n");
		goto out;
	}

	if (bytes_mapped) {
		u32 new_mappings = (np << page_shift) -
			(io_virt - round_down(io_virt, 1 << page_shift));
		*bytes_mapped += min_t(u32, new_mappings, size);
	}

	npages += np << (page_shift - PAGE_SHIFT);
	bcnt -= size;

	if (unlikely(bcnt)) {
		struct ib_umem_odp *next;

		io_virt += size;
		next = odp_next(odp);
		if (unlikely(!next || next->umem->address != io_virt)) {
			mlx5_ib_dbg(dev, "next implicit leaf removed at 0x%llx. got %p\n",
				    io_virt, next);
			return -EAGAIN;
		}
		odp = next;
		mr = odp->private;
		goto next_mr;
	}

	return npages;

out:
	if (ret == -EAGAIN) {
		if (mr->parent || !odp->dying) {
			unsigned long timeout =
				msecs_to_jiffies(MMU_NOTIFIER_TIMEOUT);

			if (!wait_for_completion_timeout(
					&odp->notifier_completion,
					timeout)) {
				mlx5_ib_warn(dev, "timeout waiting for mmu notifier. seq %d against %d\n",
					     current_seq, odp->notifiers_seq);
			}
		} else {
			/* The MR is being killed, kill the QP as well. */
			ret = -EFAULT;
		}
	}

	return ret;
}

struct pf_frame {
	struct pf_frame *next;
	u32 key;
	u64 io_virt;
	size_t bcnt;
	int depth;
};

/*
 * Handle a single data segment in a page-fault WQE or RDMA region.
 *
 * Returns number of OS pages retrieved on success. The caller may continue to
 * the next data segment.
 * Can return the following error codes:
 * -EAGAIN to designate a temporary error. The caller will abort handling the
 *  page fault and resolve it.
 * -EFAULT when there's an error mapping the requested pages. The caller will
 *  abort the page fault handling.
 */
static int pagefault_single_data_segment(struct mlx5_ib_dev *dev,
					 u32 key, u64 io_virt, size_t bcnt,
					 u32 *bytes_committed,
					 u32 *bytes_mapped)
{
	int npages = 0, srcu_key, ret, i, outlen, cur_outlen = 0, depth = 0;
	struct pf_frame *head = NULL, *frame;
	struct mlx5_core_mkey *mmkey;
	struct mlx5_ib_mw *mw;
	struct mlx5_ib_mr *mr;
	struct mlx5_klm *pklm;
	u32 *out = NULL;
	size_t offset;

	srcu_key = srcu_read_lock(&dev->mr_srcu);

	io_virt += *bytes_committed;
	bcnt -= *bytes_committed;

next_mr:
	mmkey = __mlx5_mr_lookup(dev->mdev, mlx5_base_mkey(key));
	if (!mmkey || mmkey->key != key) {
		mlx5_ib_dbg(dev, "failed to find mkey %x\n", key);
		ret = -EFAULT;
		goto srcu_unlock;
	}

	switch (mmkey->type) {
	case MLX5_MKEY_MR:
		mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);
		if (!mr->live || !mr->ibmr.pd) {
			mlx5_ib_dbg(dev, "got dead MR\n");
			ret = -EFAULT;
			goto srcu_unlock;
		}

		ret = pagefault_mr(dev, mr, io_virt, bcnt, bytes_mapped);
		if (ret < 0)
			goto srcu_unlock;

		npages += ret;
		ret = 0;
		break;

	case MLX5_MKEY_MW:
		mw = container_of(mmkey, struct mlx5_ib_mw, mmkey);

		if (depth >= MLX5_CAP_GEN(dev->mdev, max_indirection)) {
			mlx5_ib_dbg(dev, "indirection level exceeded\n");
			ret = -EFAULT;
			goto srcu_unlock;
		}

		outlen = MLX5_ST_SZ_BYTES(query_mkey_out) +
			sizeof(*pklm) * (mw->ndescs - 2);

		if (outlen > cur_outlen) {
			kfree(out);
			out = kzalloc(outlen, GFP_KERNEL);
			if (!out) {
				ret = -ENOMEM;
				goto srcu_unlock;
			}
			cur_outlen = outlen;
		}

		pklm = (struct mlx5_klm *)MLX5_ADDR_OF(query_mkey_out, out,
						       bsf0_klm0_pas_mtt0_1);

		ret = mlx5_core_query_mkey(dev->mdev, &mw->mmkey, out, outlen);
		if (ret)
			goto srcu_unlock;

		offset = io_virt - MLX5_GET64(query_mkey_out, out,
					      memory_key_mkey_entry.start_addr);

		for (i = 0; bcnt && i < mw->ndescs; i++, pklm++) {
			if (offset >= be32_to_cpu(pklm->bcount)) {
				offset -= be32_to_cpu(pklm->bcount);
				continue;
			}

			frame = kzalloc(sizeof(*frame), GFP_KERNEL);
			if (!frame) {
				ret = -ENOMEM;
				goto srcu_unlock;
			}

			frame->key = be32_to_cpu(pklm->key);
			frame->io_virt = be64_to_cpu(pklm->va) + offset;
			frame->bcnt = min_t(size_t, bcnt,
					    be32_to_cpu(pklm->bcount) - offset);
			frame->depth = depth + 1;
			frame->next = head;
			head = frame;

			bcnt -= frame->bcnt;
			offset = 0;
		}
		break;

	default:
		mlx5_ib_dbg(dev, "wrong mkey type %d\n", mmkey->type);
		ret = -EFAULT;
		goto srcu_unlock;
	}

	if (head) {
		frame = head;
		head = frame->next;

		key = frame->key;
		io_virt = frame->io_virt;
		bcnt = frame->bcnt;
		depth = frame->depth;
		kfree(frame);

		goto next_mr;
	}

srcu_unlock:
	while (head) {
		frame = head;
		head = frame->next;
		kfree(frame);
	}
	kfree(out);

	srcu_read_unlock(&dev->mr_srcu, srcu_key);
	*bytes_committed = 0;
	return ret ? ret : npages;
}

/**
 * Parse a series of data segments for page fault handling.
 *
 * @qp the QP on which the fault occurred.
 * @pfault contains page fault information.
 * @wqe points at the first data segment in the WQE.
 * @wqe_end points after the end of the WQE.
 * @bytes_mapped receives the number of bytes that the function was able to
 *               map. This allows the caller to decide intelligently whether
 *               enough memory was mapped to resolve the page fault
 *               successfully (e.g. enough for the next MTU, or the entire
 *               WQE).
 * @total_wqe_bytes receives the total data size of this WQE in bytes (minus
 *                  the committed bytes).
 *
 * Returns the number of pages loaded if positive, zero for an empty WQE, or a
 * negative error code.
 */
static int pagefault_data_segments(struct mlx5_ib_dev *dev,
				   struct mlx5_pagefault *pfault,
				   struct mlx5_ib_qp *qp, void *wqe,
				   void *wqe_end, u32 *bytes_mapped,
				   u32 *total_wqe_bytes, int receive_queue)
{
	int ret = 0, npages = 0;
	u64 io_virt;
	u32 key;
	u32 byte_count;
	size_t bcnt;
	int inline_segment;

	/* Skip SRQ next-WQE segment. */
	if (receive_queue && qp->ibqp.srq)
		wqe += sizeof(struct mlx5_wqe_srq_next_seg);

	if (bytes_mapped)
		*bytes_mapped = 0;
	if (total_wqe_bytes)
		*total_wqe_bytes = 0;

	while (wqe < wqe_end) {
		struct mlx5_wqe_data_seg *dseg = wqe;

		io_virt = be64_to_cpu(dseg->addr);
		key = be32_to_cpu(dseg->lkey);
		byte_count = be32_to_cpu(dseg->byte_count);
		inline_segment = !!(byte_count &  MLX5_INLINE_SEG);
		bcnt	       = byte_count & ~MLX5_INLINE_SEG;

		if (inline_segment) {
			bcnt = bcnt & MLX5_WQE_INLINE_SEG_BYTE_COUNT_MASK;
			wqe += ALIGN(sizeof(struct mlx5_wqe_inline_seg) + bcnt,
				     16);
		} else {
			wqe += sizeof(*dseg);
		}

		/* receive WQE end of sg list. */
		if (receive_queue && bcnt == 0 && key == MLX5_INVALID_LKEY &&
		    io_virt == 0)
			break;

		if (!inline_segment && total_wqe_bytes) {
			*total_wqe_bytes += bcnt - min_t(size_t, bcnt,
					pfault->bytes_committed);
		}

		/* A zero length data segment designates a length of 2GB. */
		if (bcnt == 0)
			bcnt = 1U << 31;

		if (inline_segment || bcnt <= pfault->bytes_committed) {
			pfault->bytes_committed -=
				min_t(size_t, bcnt,
				      pfault->bytes_committed);
			continue;
		}

		ret = pagefault_single_data_segment(dev, key, io_virt, bcnt,
						    &pfault->bytes_committed,
						    bytes_mapped);
		if (ret < 0)
			break;
		npages += ret;
	}

	return ret < 0 ? ret : npages;
}

static const u32 mlx5_ib_odp_opcode_cap[] = {
	[MLX5_OPCODE_SEND]	       = IB_ODP_SUPPORT_SEND,
	[MLX5_OPCODE_SEND_IMM]	       = IB_ODP_SUPPORT_SEND,
	[MLX5_OPCODE_SEND_INVAL]       = IB_ODP_SUPPORT_SEND,
	[MLX5_OPCODE_RDMA_WRITE]       = IB_ODP_SUPPORT_WRITE,
	[MLX5_OPCODE_RDMA_WRITE_IMM]   = IB_ODP_SUPPORT_WRITE,
	[MLX5_OPCODE_RDMA_READ]	       = IB_ODP_SUPPORT_READ,
	[MLX5_OPCODE_ATOMIC_CS]	       = IB_ODP_SUPPORT_ATOMIC,
	[MLX5_OPCODE_ATOMIC_FA]	       = IB_ODP_SUPPORT_ATOMIC,
};

/*
 * Parse initiator WQE. Advances the wqe pointer to point at the
 * scatter-gather list, and set wqe_end to the end of the WQE.
 */
static int mlx5_ib_mr_initiator_pfault_handler(
	struct mlx5_ib_dev *dev, struct mlx5_pagefault *pfault,
	struct mlx5_ib_qp *qp, void **wqe, void **wqe_end, int wqe_length)
{
	struct mlx5_wqe_ctrl_seg *ctrl = *wqe;
	u16 wqe_index = pfault->wqe.wqe_index;
	u32 transport_caps;
	struct mlx5_base_av *av;
	unsigned ds, opcode;
#if defined(DEBUG)
	u32 ctrl_wqe_index, ctrl_qpn;
#endif
	u32 qpn = qp->trans_qp.base.mqp.qpn;

	ds = be32_to_cpu(ctrl->qpn_ds) & MLX5_WQE_CTRL_DS_MASK;
	if (ds * MLX5_WQE_DS_UNITS > wqe_length) {
		mlx5_ib_err(dev, "Unable to read the complete WQE. ds = 0x%x, ret = 0x%x\n",
			    ds, wqe_length);
		return -EFAULT;
	}

	if (ds == 0) {
		mlx5_ib_err(dev, "Got WQE with zero DS. wqe_index=%x, qpn=%x\n",
			    wqe_index, qpn);
		return -EFAULT;
	}

#if defined(DEBUG)
	ctrl_wqe_index = (be32_to_cpu(ctrl->opmod_idx_opcode) &
			MLX5_WQE_CTRL_WQE_INDEX_MASK) >>
			MLX5_WQE_CTRL_WQE_INDEX_SHIFT;
	if (wqe_index != ctrl_wqe_index) {
		mlx5_ib_err(dev, "Got WQE with invalid wqe_index. wqe_index=0x%x, qpn=0x%x ctrl->wqe_index=0x%x\n",
			    wqe_index, qpn,
			    ctrl_wqe_index);
		return -EFAULT;
	}

	ctrl_qpn = (be32_to_cpu(ctrl->qpn_ds) & MLX5_WQE_CTRL_QPN_MASK) >>
		MLX5_WQE_CTRL_QPN_SHIFT;
	if (qpn != ctrl_qpn) {
		mlx5_ib_err(dev, "Got WQE with incorrect QP number. wqe_index=0x%x, qpn=0x%x ctrl->qpn=0x%x\n",
			    wqe_index, qpn,
			    ctrl_qpn);
		return -EFAULT;
	}
#endif /* DEBUG */

	*wqe_end = *wqe + ds * MLX5_WQE_DS_UNITS;
	*wqe += sizeof(*ctrl);

	opcode = be32_to_cpu(ctrl->opmod_idx_opcode) &
		 MLX5_WQE_CTRL_OPCODE_MASK;

	switch (qp->ibqp.qp_type) {
	case IB_QPT_RC:
		transport_caps = dev->odp_caps.per_transport_caps.rc_odp_caps;
		break;
	case IB_QPT_UD:
		transport_caps = dev->odp_caps.per_transport_caps.ud_odp_caps;
		break;
	default:
		mlx5_ib_err(dev, "ODP fault on QP of an unsupported transport 0x%x\n",
			    qp->ibqp.qp_type);
		return -EFAULT;
	}

	if (unlikely(opcode >= ARRAY_SIZE(mlx5_ib_odp_opcode_cap) ||
		     !(transport_caps & mlx5_ib_odp_opcode_cap[opcode]))) {
		mlx5_ib_err(dev, "ODP fault on QP of an unsupported opcode 0x%x\n",
			    opcode);
		return -EFAULT;
	}

	if (qp->ibqp.qp_type != IB_QPT_RC) {
		av = *wqe;
		if (av->dqp_dct & cpu_to_be32(MLX5_EXTENDED_UD_AV))
			*wqe += sizeof(struct mlx5_av);
		else
			*wqe += sizeof(struct mlx5_base_av);
	}

	switch (opcode) {
	case MLX5_OPCODE_RDMA_WRITE:
	case MLX5_OPCODE_RDMA_WRITE_IMM:
	case MLX5_OPCODE_RDMA_READ:
		*wqe += sizeof(struct mlx5_wqe_raddr_seg);
		break;
	case MLX5_OPCODE_ATOMIC_CS:
	case MLX5_OPCODE_ATOMIC_FA:
		*wqe += sizeof(struct mlx5_wqe_raddr_seg);
		*wqe += sizeof(struct mlx5_wqe_atomic_seg);
		break;
	}

	return 0;
}

/*
 * Parse responder WQE. Advances the wqe pointer to point at the
 * scatter-gather list, and set wqe_end to the end of the WQE.
 */
static int mlx5_ib_mr_responder_pfault_handler(
	struct mlx5_ib_dev *dev, struct mlx5_pagefault *pfault,
	struct mlx5_ib_qp *qp, void **wqe, void **wqe_end, int wqe_length)
{
	struct mlx5_ib_wq *wq = &qp->rq;
	int wqe_size = 1 << wq->wqe_shift;

	if (qp->ibqp.srq) {
		mlx5_ib_err(dev, "ODP fault on SRQ is not supported\n");
		return -EFAULT;
	}

	if (qp->wq_sig) {
		mlx5_ib_err(dev, "ODP fault with WQE signatures is not supported\n");
		return -EFAULT;
	}

	if (wqe_size > wqe_length) {
		mlx5_ib_err(dev, "Couldn't read all of the receive WQE's content\n");
		return -EFAULT;
	}

	switch (qp->ibqp.qp_type) {
	case IB_QPT_RC:
		if (!(dev->odp_caps.per_transport_caps.rc_odp_caps &
		      IB_ODP_SUPPORT_RECV))
			goto invalid_transport_or_opcode;
		break;
	default:
invalid_transport_or_opcode:
		mlx5_ib_err(dev, "ODP fault on QP of an unsupported transport. transport: 0x%x\n",
			    qp->ibqp.qp_type);
		return -EFAULT;
	}

	*wqe_end = *wqe + wqe_size;

	return 0;
}

static struct mlx5_ib_qp *mlx5_ib_odp_find_qp(struct mlx5_ib_dev *dev,
					      u32 wq_num)
{
	struct mlx5_core_qp *mqp = __mlx5_qp_lookup(dev->mdev, wq_num);

	if (!mqp) {
		mlx5_ib_err(dev, "QPN 0x%6x not found\n", wq_num);
		return NULL;
	}

	return to_mibqp(mqp);
}

static void mlx5_ib_mr_wqe_pfault_handler(struct mlx5_ib_dev *dev,
					  struct mlx5_pagefault *pfault)
{
	int ret;
	void *wqe, *wqe_end;
	u32 bytes_mapped, total_wqe_bytes;
	char *buffer = NULL;
	int resume_with_error = 1;
	u16 wqe_index = pfault->wqe.wqe_index;
	int requestor = pfault->type & MLX5_PFAULT_REQUESTOR;
	struct mlx5_ib_qp *qp;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer) {
		mlx5_ib_err(dev, "Error allocating memory for IO page fault handling.\n");
		goto resolve_page_fault;
	}

	qp = mlx5_ib_odp_find_qp(dev, pfault->wqe.wq_num);
	if (!qp)
		goto resolve_page_fault;

	ret = mlx5_ib_read_user_wqe(qp, requestor, wqe_index, buffer,
				    PAGE_SIZE, &qp->trans_qp.base);
	if (ret < 0) {
		mlx5_ib_err(dev, "Failed reading a WQE following page fault, error=%d, wqe_index=%x, qpn=%x\n",
			    ret, wqe_index, pfault->token);
		goto resolve_page_fault;
	}

	wqe = buffer;
	if (requestor)
		ret = mlx5_ib_mr_initiator_pfault_handler(dev, pfault, qp, &wqe,
							  &wqe_end, ret);
	else
		ret = mlx5_ib_mr_responder_pfault_handler(dev, pfault, qp, &wqe,
							  &wqe_end, ret);
	if (ret < 0)
		goto resolve_page_fault;

	if (wqe >= wqe_end) {
		mlx5_ib_err(dev, "ODP fault on invalid WQE.\n");
		goto resolve_page_fault;
	}

	ret = pagefault_data_segments(dev, pfault, qp, wqe, wqe_end,
				      &bytes_mapped, &total_wqe_bytes,
				      !requestor);
	if (ret == -EAGAIN) {
		resume_with_error = 0;
		goto resolve_page_fault;
	} else if (ret < 0 || total_wqe_bytes > bytes_mapped) {
		goto resolve_page_fault;
	}

	resume_with_error = 0;
resolve_page_fault:
	mlx5_ib_page_fault_resume(dev, pfault, resume_with_error);
	mlx5_ib_dbg(dev, "PAGE FAULT completed. QP 0x%x resume_with_error=%d, type: 0x%x\n",
		    pfault->wqe.wq_num, resume_with_error,
		    pfault->type);
	free_page((unsigned long)buffer);
}

static int pages_in_range(u64 address, u32 length)
{
	return (ALIGN(address + length, PAGE_SIZE) -
		(address & PAGE_MASK)) >> PAGE_SHIFT;
}

static void mlx5_ib_mr_rdma_pfault_handler(struct mlx5_ib_dev *dev,
					   struct mlx5_pagefault *pfault)
{
	u64 address;
	u32 length;
	u32 prefetch_len = pfault->bytes_committed;
	int prefetch_activated = 0;
	u32 rkey = pfault->rdma.r_key;
	int ret;

	/* The RDMA responder handler handles the page fault in two parts.
	 * First it brings the necessary pages for the current packet
	 * (and uses the pfault context), and then (after resuming the QP)
	 * prefetches more pages. The second operation cannot use the pfault
	 * context and therefore uses the dummy_pfault context allocated on
	 * the stack */
	pfault->rdma.rdma_va += pfault->bytes_committed;
	pfault->rdma.rdma_op_len -= min(pfault->bytes_committed,
					 pfault->rdma.rdma_op_len);
	pfault->bytes_committed = 0;

	address = pfault->rdma.rdma_va;
	length  = pfault->rdma.rdma_op_len;

	/* For some operations, the hardware cannot tell the exact message
	 * length, and in those cases it reports zero. Use prefetch
	 * logic. */
	if (length == 0) {
		prefetch_activated = 1;
		length = pfault->rdma.packet_size;
		prefetch_len = min(MAX_PREFETCH_LEN, prefetch_len);
	}

	ret = pagefault_single_data_segment(dev, rkey, address, length,
					    &pfault->bytes_committed, NULL);
	if (ret == -EAGAIN) {
		/* We're racing with an invalidation, don't prefetch */
		prefetch_activated = 0;
	} else if (ret < 0 || pages_in_range(address, length) > ret) {
		mlx5_ib_page_fault_resume(dev, pfault, 1);
		if (ret != -ENOENT)
			mlx5_ib_dbg(dev, "PAGE FAULT error %d. QP 0x%x, type: 0x%x\n",
				    ret, pfault->token, pfault->type);
		return;
	}

	mlx5_ib_page_fault_resume(dev, pfault, 0);
	mlx5_ib_dbg(dev, "PAGE FAULT completed. QP 0x%x, type: 0x%x, prefetch_activated: %d\n",
		    pfault->token, pfault->type,
		    prefetch_activated);

	/* At this point, there might be a new pagefault already arriving in
	 * the eq, switch to the dummy pagefault for the rest of the
	 * processing. We're still OK with the objects being alive as the
	 * work-queue is being fenced. */

	if (prefetch_activated) {
		u32 bytes_committed = 0;

		ret = pagefault_single_data_segment(dev, rkey, address,
						    prefetch_len,
						    &bytes_committed, NULL);
		if (ret < 0 && ret != -EAGAIN) {
			mlx5_ib_dbg(dev, "Prefetch failed. ret: %d, QP 0x%x, address: 0x%.16llx, length = 0x%.16x\n",
				    ret, pfault->token, address, prefetch_len);
		}
	}
}

void mlx5_ib_pfault(struct mlx5_core_dev *mdev, void *context,
		    struct mlx5_pagefault *pfault)
{
	struct mlx5_ib_dev *dev = context;
	u8 event_subtype = pfault->event_subtype;

	switch (event_subtype) {
	case MLX5_PFAULT_SUBTYPE_WQE:
		mlx5_ib_mr_wqe_pfault_handler(dev, pfault);
		break;
	case MLX5_PFAULT_SUBTYPE_RDMA:
		mlx5_ib_mr_rdma_pfault_handler(dev, pfault);
		break;
	default:
		mlx5_ib_err(dev, "Invalid page fault event subtype: 0x%x\n",
			    event_subtype);
		mlx5_ib_page_fault_resume(dev, pfault, 1);
	}
}

void mlx5_odp_init_mr_cache_entry(struct mlx5_cache_ent *ent)
{
	if (!(ent->dev->odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT))
		return;

	switch (ent->order - 2) {
	case MLX5_IMR_MTT_CACHE_ENTRY:
		ent->page = PAGE_SHIFT;
		ent->xlt = MLX5_IMR_MTT_ENTRIES *
			   sizeof(struct mlx5_mtt) /
			   MLX5_IB_UMR_OCTOWORD;
		ent->access_mode = MLX5_MKC_ACCESS_MODE_MTT;
		ent->limit = 0;
		break;

	case MLX5_IMR_KSM_CACHE_ENTRY:
		ent->page = MLX5_KSM_PAGE_SHIFT;
		ent->xlt = mlx5_imr_ksm_entries *
			   sizeof(struct mlx5_klm) /
			   MLX5_IB_UMR_OCTOWORD;
		ent->access_mode = MLX5_MKC_ACCESS_MODE_KSM;
		ent->limit = 0;
		break;
	}
}

int mlx5_ib_odp_init_one(struct mlx5_ib_dev *dev)
{
	int ret;

	if (dev->odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT) {
		ret = mlx5_cmd_null_mkey(dev->mdev, &dev->null_mkey);
		if (ret) {
			mlx5_ib_err(dev, "Error getting null_mkey %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int mlx5_ib_odp_init(void)
{
	mlx5_imr_ksm_entries = BIT_ULL(get_order(TASK_SIZE) -
				       MLX5_IMR_MTT_BITS);

	return 0;
}

