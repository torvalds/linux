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

#include <linux/mlx5/eq.h>

/* Contains the details of a pagefault. */
struct mlx5_pagefault {
	u32			bytes_committed;
	u32			token;
	u8			event_subtype;
	u8			type;
	union {
		/* Initiator or send message responder pagefault details. */
		struct {
			/* Received packet size, only valid for responders. */
			u32	packet_size;
			/*
			 * Number of resource holding WQE, depends on type.
			 */
			u32	wq_num;
			/*
			 * WQE index. Refers to either the send queue or
			 * receive queue, according to event_subtype.
			 */
			u16	wqe_index;
		} wqe;
		/* RDMA responder pagefault details */
		struct {
			u32	r_key;
			/*
			 * Received packet size, minimal size page fault
			 * resolution required for forward progress.
			 */
			u32	packet_size;
			u32	rdma_op_len;
			u64	rdma_va;
		} rdma;
	};

	struct mlx5_ib_pf_eq	*eq;
	struct work_struct	work;
};

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

static struct ib_ucontext_per_mm *mr_to_per_mm(struct mlx5_ib_mr *mr)
{
	if (WARN_ON(!mr || !is_odp_mr(mr)))
		return NULL;

	return to_ib_umem_odp(mr->umem)->per_mm;
}

static struct ib_umem_odp *odp_next(struct ib_umem_odp *odp)
{
	struct mlx5_ib_mr *mr = odp->private, *parent = mr->parent;
	struct ib_ucontext_per_mm *per_mm = odp->per_mm;
	struct rb_node *rb;

	down_read(&per_mm->umem_rwsem);
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
	up_read(&per_mm->umem_rwsem);
	return odp;
}

static struct ib_umem_odp *odp_lookup(u64 start, u64 length,
				      struct mlx5_ib_mr *parent)
{
	struct ib_ucontext_per_mm *per_mm = mr_to_per_mm(parent);
	struct ib_umem_odp *odp;
	struct rb_node *rb;

	down_read(&per_mm->umem_rwsem);
	odp = rbt_ib_umem_lookup(&per_mm->umem_tree, start, length);
	if (!odp)
		goto end;

	while (1) {
		if (check_parent(odp, parent))
			goto end;
		rb = rb_next(&odp->interval_tree.rb);
		if (!rb)
			goto not_found;
		odp = rb_entry(rb, struct ib_umem_odp, interval_tree.rb);
		if (ib_umem_start(&odp->umem) > start + length)
			goto not_found;
	}
not_found:
	odp = NULL;
end:
	up_read(&per_mm->umem_rwsem);
	return odp;
}

void mlx5_odp_populate_klm(struct mlx5_klm *pklm, size_t offset,
			   size_t nentries, struct mlx5_ib_mr *mr, int flags)
{
	struct ib_pd *pd = mr->ibmr.pd;
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

	odp = odp_lookup(offset * MLX5_IMR_MTT_SIZE,
			 nentries * MLX5_IMR_MTT_SIZE, mr);

	for (i = 0; i < nentries; i++, pklm++) {
		pklm->bcount = cpu_to_be32(MLX5_IMR_MTT_SIZE);
		va = (offset + i) * MLX5_IMR_MTT_SIZE;
		if (odp && odp->umem.address == va) {
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
	int idx = ib_umem_start(&odp->umem) >> MLX5_IMR_MTT_SHIFT;
	struct mlx5_ib_mr *mr = odp->private, *imr = mr->parent;

	mr->parent = NULL;
	synchronize_srcu(&mr->dev->mr_srcu);

	ib_umem_release(&odp->umem);
	if (imr->live)
		mlx5_ib_update_xlt(imr, idx, 1, 0,
				   MLX5_IB_UPD_XLT_INDIRECT |
				   MLX5_IB_UPD_XLT_ATOMIC);
	mlx5_mr_cache_free(mr->dev, mr);

	if (atomic_dec_and_test(&imr->num_leaf_free))
		wake_up(&imr->q_leaf_free);
}

void mlx5_ib_invalidate_range(struct ib_umem_odp *umem_odp, unsigned long start,
			      unsigned long end)
{
	struct mlx5_ib_mr *mr;
	const u64 umr_block_mask = (MLX5_UMR_MTT_ALIGNMENT /
				    sizeof(struct mlx5_mtt)) - 1;
	u64 idx = 0, blk_start_idx = 0;
	struct ib_umem *umem;
	int in_block = 0;
	u64 addr;

	if (!umem_odp) {
		pr_err("invalidation called on NULL umem or non-ODP umem\n");
		return;
	}
	umem = &umem_odp->umem;

	mr = umem_odp->private;

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
		if (umem_odp->dma_list[idx] &
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

	ib_umem_odp_unmap_dma_pages(umem_odp, start, end);

	if (unlikely(!umem_odp->npages && mr->parent &&
		     !umem_odp->dying)) {
		WRITE_ONCE(umem_odp->dying, 1);
		atomic_inc(&mr->parent->num_leaf_free);
		schedule_work(&umem_odp->work);
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

	if (MLX5_CAP_ODP(dev->mdev, ud_odp_caps.srq_receive))
		caps->per_transport_caps.ud_odp_caps |= IB_ODP_SUPPORT_SRQ_RECV;

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

	if (MLX5_CAP_ODP(dev->mdev, rc_odp_caps.srq_receive))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_SRQ_RECV;

	if (MLX5_CAP_ODP(dev->mdev, xrc_odp_caps.send))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_SEND;

	if (MLX5_CAP_ODP(dev->mdev, xrc_odp_caps.receive))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_RECV;

	if (MLX5_CAP_ODP(dev->mdev, xrc_odp_caps.write))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_WRITE;

	if (MLX5_CAP_ODP(dev->mdev, xrc_odp_caps.read))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_READ;

	if (MLX5_CAP_ODP(dev->mdev, xrc_odp_caps.atomic))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_ATOMIC;

	if (MLX5_CAP_ODP(dev->mdev, xrc_odp_caps.srq_receive))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_SRQ_RECV;

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
	u32 out[MLX5_ST_SZ_DW(page_fault_resume_out)] = { };
	u32 in[MLX5_ST_SZ_DW(page_fault_resume_in)]   = { };
	int err;

	MLX5_SET(page_fault_resume_in, in, opcode, MLX5_CMD_OP_PAGE_FAULT_RESUME);
	MLX5_SET(page_fault_resume_in, in, page_fault_type, pfault->type);
	MLX5_SET(page_fault_resume_in, in, token, pfault->token);
	MLX5_SET(page_fault_resume_in, in, wq_number, wq_num);
	MLX5_SET(page_fault_resume_in, in, error, !!error);

	err = mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		mlx5_ib_err(dev, "Failed to resolve the page fault on WQ 0x%x err %d\n",
			    wq_num, err);
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
	struct mlx5_ib_dev *dev = to_mdev(mr->ibmr.pd->device);
	struct ib_umem_odp *odp, *result = NULL;
	struct ib_umem_odp *odp_mr = to_ib_umem_odp(mr->umem);
	u64 addr = io_virt & MLX5_IMR_MTT_MASK;
	int nentries = 0, start_idx = 0, ret;
	struct mlx5_ib_mr *mtt;

	mutex_lock(&odp_mr->umem_mutex);
	odp = odp_lookup(addr, 1, mr);

	mlx5_ib_dbg(dev, "io_virt:%llx bcnt:%zx addr:%llx odp:%p\n",
		    io_virt, bcnt, addr, odp);

next_mr:
	if (likely(odp)) {
		if (nentries)
			nentries++;
	} else {
		odp = ib_alloc_odp_umem(odp_mr, addr,
					MLX5_IMR_MTT_SIZE);
		if (IS_ERR(odp)) {
			mutex_unlock(&odp_mr->umem_mutex);
			return ERR_CAST(odp);
		}

		mtt = implicit_mr_alloc(mr->ibmr.pd, &odp->umem, 0,
					mr->access_flags);
		if (IS_ERR(mtt)) {
			mutex_unlock(&odp_mr->umem_mutex);
			ib_umem_release(&odp->umem);
			return ERR_CAST(mtt);
		}

		odp->private = mtt;
		mtt->umem = &odp->umem;
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
		if (odp && odp->umem.address != addr)
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

	mutex_unlock(&odp_mr->umem_mutex);
	return result;
}

struct mlx5_ib_mr *mlx5_ib_alloc_implicit_mr(struct mlx5_ib_pd *pd,
					     struct ib_udata *udata,
					     int access_flags)
{
	struct mlx5_ib_mr *imr;
	struct ib_umem *umem;

	umem = ib_umem_get(udata, 0, 0, access_flags, 0);
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
	atomic_set(&imr->num_pending_prefetch, 0);

	return imr;
}

static int mr_leaf_free(struct ib_umem_odp *umem_odp, u64 start, u64 end,
			void *cookie)
{
	struct mlx5_ib_mr *mr = umem_odp->private, *imr = cookie;
	struct ib_umem *umem = &umem_odp->umem;

	if (mr->parent != imr)
		return 0;

	ib_umem_odp_unmap_dma_pages(umem_odp, ib_umem_start(umem),
				    ib_umem_end(umem));

	if (umem_odp->dying)
		return 0;

	WRITE_ONCE(umem_odp->dying, 1);
	atomic_inc(&imr->num_leaf_free);
	schedule_work(&umem_odp->work);

	return 0;
}

void mlx5_ib_free_implicit_mr(struct mlx5_ib_mr *imr)
{
	struct ib_ucontext_per_mm *per_mm = mr_to_per_mm(imr);

	down_read(&per_mm->umem_rwsem);
	rbt_ib_umem_for_each_in_range(&per_mm->umem_tree, 0, ULLONG_MAX,
				      mr_leaf_free, true, imr);
	up_read(&per_mm->umem_rwsem);

	wait_event(imr->q_leaf_free, !atomic_read(&imr->num_leaf_free));
}

#define MLX5_PF_FLAGS_PREFETCH  BIT(0)
#define MLX5_PF_FLAGS_DOWNGRADE BIT(1)
static int pagefault_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr,
			u64 io_virt, size_t bcnt, u32 *bytes_mapped,
			u32 flags)
{
	int npages = 0, current_seq, page_shift, ret, np;
	bool implicit = false;
	struct ib_umem_odp *odp_mr = to_ib_umem_odp(mr->umem);
	bool downgrade = flags & MLX5_PF_FLAGS_DOWNGRADE;
	bool prefetch = flags & MLX5_PF_FLAGS_PREFETCH;
	u64 access_mask;
	u64 start_idx, page_mask;
	struct ib_umem_odp *odp;
	size_t size;

	if (!odp_mr->page_list) {
		odp = implicit_mr_get_data(mr, io_virt, bcnt);

		if (IS_ERR(odp))
			return PTR_ERR(odp);
		mr = odp->private;
		implicit = true;
	} else {
		odp = odp_mr;
	}

next_mr:
	size = min_t(size_t, bcnt, ib_umem_end(&odp->umem) - io_virt);

	page_shift = mr->umem->page_shift;
	page_mask = ~(BIT(page_shift) - 1);
	start_idx = (io_virt - (mr->mmkey.iova & page_mask)) >> page_shift;
	access_mask = ODP_READ_ALLOWED_BIT;

	if (prefetch && !downgrade && !mr->umem->writable) {
		/* prefetch with write-access must
		 * be supported by the MR
		 */
		ret = -EINVAL;
		goto out;
	}

	if (mr->umem->writable && !downgrade)
		access_mask |= ODP_WRITE_ALLOWED_BIT;

	current_seq = READ_ONCE(odp->notifiers_seq);
	/*
	 * Ensure the sequence number is valid for some time before we call
	 * gup.
	 */
	smp_rmb();

	ret = ib_umem_odp_map_dma_pages(to_ib_umem_odp(mr->umem), io_virt, size,
					access_mask, current_seq);

	if (ret < 0)
		goto out;

	np = ret;

	mutex_lock(&odp->umem_mutex);
	if (!ib_umem_mmu_notifier_retry(to_ib_umem_odp(mr->umem),
					current_seq)) {
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
		if (unlikely(!next || next->umem.address != io_virt)) {
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
		if (implicit || !odp->dying) {
			unsigned long timeout =
				msecs_to_jiffies(MMU_NOTIFIER_TIMEOUT);

			if (!wait_for_completion_timeout(
					&odp->notifier_completion,
					timeout)) {
				mlx5_ib_warn(dev, "timeout waiting for mmu notifier. seq %d against %d. notifiers_count=%d\n",
					     current_seq, odp->notifiers_seq, odp->notifiers_count);
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

static bool mkey_is_eq(struct mlx5_core_mkey *mmkey, u32 key)
{
	if (!mmkey)
		return false;
	if (mmkey->type == MLX5_MKEY_MW)
		return mlx5_base_mkey(mmkey->key) == mlx5_base_mkey(key);
	return mmkey->key == key;
}

static int get_indirect_num_descs(struct mlx5_core_mkey *mmkey)
{
	struct mlx5_ib_mw *mw;
	struct mlx5_ib_devx_mr *devx_mr;

	if (mmkey->type == MLX5_MKEY_MW) {
		mw = container_of(mmkey, struct mlx5_ib_mw, mmkey);
		return mw->ndescs;
	}

	devx_mr = container_of(mmkey, struct mlx5_ib_devx_mr,
			       mmkey);
	return devx_mr->ndescs;
}

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
					 struct ib_pd *pd, u32 key,
					 u64 io_virt, size_t bcnt,
					 u32 *bytes_committed,
					 u32 *bytes_mapped, u32 flags)
{
	int npages = 0, srcu_key, ret, i, outlen, cur_outlen = 0, depth = 0;
	bool prefetch = flags & MLX5_PF_FLAGS_PREFETCH;
	struct pf_frame *head = NULL, *frame;
	struct mlx5_core_mkey *mmkey;
	struct mlx5_ib_mr *mr;
	struct mlx5_klm *pklm;
	u32 *out = NULL;
	size_t offset;
	int ndescs;

	srcu_key = srcu_read_lock(&dev->mr_srcu);

	io_virt += *bytes_committed;
	bcnt -= *bytes_committed;

next_mr:
	mmkey = __mlx5_mr_lookup(dev->mdev, mlx5_base_mkey(key));
	if (!mkey_is_eq(mmkey, key)) {
		mlx5_ib_dbg(dev, "failed to find mkey %x\n", key);
		ret = -EFAULT;
		goto srcu_unlock;
	}

	if (prefetch && mmkey->type != MLX5_MKEY_MR) {
		mlx5_ib_dbg(dev, "prefetch is allowed only for MR\n");
		ret = -EINVAL;
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

		if (prefetch) {
			if (!is_odp_mr(mr) ||
			    mr->ibmr.pd != pd) {
				mlx5_ib_dbg(dev, "Invalid prefetch request: %s\n",
					    is_odp_mr(mr) ?  "MR is not ODP" :
					    "PD is not of the MR");
				ret = -EINVAL;
				goto srcu_unlock;
			}
		}

		if (!is_odp_mr(mr)) {
			mlx5_ib_dbg(dev, "skipping non ODP MR (lkey=0x%06x) in page fault handler.\n",
				    key);
			if (bytes_mapped)
				*bytes_mapped += bcnt;
			ret = 0;
			goto srcu_unlock;
		}

		ret = pagefault_mr(dev, mr, io_virt, bcnt, bytes_mapped, flags);
		if (ret < 0)
			goto srcu_unlock;

		npages += ret;
		ret = 0;
		break;

	case MLX5_MKEY_MW:
	case MLX5_MKEY_INDIRECT_DEVX:
		ndescs = get_indirect_num_descs(mmkey);

		if (depth >= MLX5_CAP_GEN(dev->mdev, max_indirection)) {
			mlx5_ib_dbg(dev, "indirection level exceeded\n");
			ret = -EFAULT;
			goto srcu_unlock;
		}

		outlen = MLX5_ST_SZ_BYTES(query_mkey_out) +
			sizeof(*pklm) * (ndescs - 2);

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

		ret = mlx5_core_query_mkey(dev->mdev, mmkey, out, outlen);
		if (ret)
			goto srcu_unlock;

		offset = io_virt - MLX5_GET64(query_mkey_out, out,
					      memory_key_mkey_entry.start_addr);

		for (i = 0; bcnt && i < ndescs; i++, pklm++) {
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
				   void *wqe,
				   void *wqe_end, u32 *bytes_mapped,
				   u32 *total_wqe_bytes, bool receive_queue)
{
	int ret = 0, npages = 0;
	u64 io_virt;
	u32 key;
	u32 byte_count;
	size_t bcnt;
	int inline_segment;

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

		ret = pagefault_single_data_segment(dev, NULL, key,
						    io_virt, bcnt,
						    &pfault->bytes_committed,
						    bytes_mapped, 0);
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
	case IB_QPT_XRC_INI:
		*wqe += sizeof(struct mlx5_wqe_xrc_seg);
		transport_caps = dev->odp_caps.per_transport_caps.xrc_odp_caps;
		break;
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

	if (qp->ibqp.qp_type == IB_QPT_UD) {
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
 * Parse responder WQE and set wqe_end to the end of the WQE.
 */
static int mlx5_ib_mr_responder_pfault_handler_srq(struct mlx5_ib_dev *dev,
						   struct mlx5_ib_srq *srq,
						   void **wqe, void **wqe_end,
						   int wqe_length)
{
	int wqe_size = 1 << srq->msrq.wqe_shift;

	if (wqe_size > wqe_length) {
		mlx5_ib_err(dev, "Couldn't read all of the receive WQE's content\n");
		return -EFAULT;
	}

	*wqe_end = *wqe + wqe_size;
	*wqe += sizeof(struct mlx5_wqe_srq_next_seg);

	return 0;
}

static int mlx5_ib_mr_responder_pfault_handler_rq(struct mlx5_ib_dev *dev,
						  struct mlx5_ib_qp *qp,
						  void *wqe, void **wqe_end,
						  int wqe_length)
{
	struct mlx5_ib_wq *wq = &qp->rq;
	int wqe_size = 1 << wq->wqe_shift;

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

	*wqe_end = wqe + wqe_size;

	return 0;
}

static inline struct mlx5_core_rsc_common *odp_get_rsc(struct mlx5_ib_dev *dev,
						       u32 wq_num, int pf_type)
{
	struct mlx5_core_rsc_common *common = NULL;
	struct mlx5_core_srq *srq;

	switch (pf_type) {
	case MLX5_WQE_PF_TYPE_RMP:
		srq = mlx5_cmd_get_srq(dev, wq_num);
		if (srq)
			common = &srq->common;
		break;
	case MLX5_WQE_PF_TYPE_REQ_SEND_OR_WRITE:
	case MLX5_WQE_PF_TYPE_RESP:
	case MLX5_WQE_PF_TYPE_REQ_READ_OR_ATOMIC:
		common = mlx5_core_res_hold(dev->mdev, wq_num, MLX5_RES_QP);
		break;
	default:
		break;
	}

	return common;
}

static inline struct mlx5_ib_qp *res_to_qp(struct mlx5_core_rsc_common *res)
{
	struct mlx5_core_qp *mqp = (struct mlx5_core_qp *)res;

	return to_mibqp(mqp);
}

static inline struct mlx5_ib_srq *res_to_srq(struct mlx5_core_rsc_common *res)
{
	struct mlx5_core_srq *msrq =
		container_of(res, struct mlx5_core_srq, common);

	return to_mibsrq(msrq);
}

static void mlx5_ib_mr_wqe_pfault_handler(struct mlx5_ib_dev *dev,
					  struct mlx5_pagefault *pfault)
{
	bool sq = pfault->type & MLX5_PFAULT_REQUESTOR;
	u16 wqe_index = pfault->wqe.wqe_index;
	void *wqe = NULL, *wqe_end = NULL;
	u32 bytes_mapped, total_wqe_bytes;
	struct mlx5_core_rsc_common *res;
	int resume_with_error = 1;
	struct mlx5_ib_qp *qp;
	size_t bytes_copied;
	int ret = 0;

	res = odp_get_rsc(dev, pfault->wqe.wq_num, pfault->type);
	if (!res) {
		mlx5_ib_dbg(dev, "wqe page fault for missing resource %d\n", pfault->wqe.wq_num);
		return;
	}

	if (res->res != MLX5_RES_QP && res->res != MLX5_RES_SRQ &&
	    res->res != MLX5_RES_XSRQ) {
		mlx5_ib_err(dev, "wqe page fault for unsupported type %d\n",
			    pfault->type);
		goto resolve_page_fault;
	}

	wqe = (void *)__get_free_page(GFP_KERNEL);
	if (!wqe) {
		mlx5_ib_err(dev, "Error allocating memory for IO page fault handling.\n");
		goto resolve_page_fault;
	}

	qp = (res->res == MLX5_RES_QP) ? res_to_qp(res) : NULL;
	if (qp && sq) {
		ret = mlx5_ib_read_user_wqe_sq(qp, wqe_index, wqe, PAGE_SIZE,
					       &bytes_copied);
		if (ret)
			goto read_user;
		ret = mlx5_ib_mr_initiator_pfault_handler(
			dev, pfault, qp, &wqe, &wqe_end, bytes_copied);
	} else if (qp && !sq) {
		ret = mlx5_ib_read_user_wqe_rq(qp, wqe_index, wqe, PAGE_SIZE,
					       &bytes_copied);
		if (ret)
			goto read_user;
		ret = mlx5_ib_mr_responder_pfault_handler_rq(
			dev, qp, wqe, &wqe_end, bytes_copied);
	} else if (!qp) {
		struct mlx5_ib_srq *srq = res_to_srq(res);

		ret = mlx5_ib_read_user_wqe_srq(srq, wqe_index, wqe, PAGE_SIZE,
						&bytes_copied);
		if (ret)
			goto read_user;
		ret = mlx5_ib_mr_responder_pfault_handler_srq(
			dev, srq, &wqe, &wqe_end, bytes_copied);
	}

	if (ret < 0 || wqe >= wqe_end)
		goto resolve_page_fault;

	ret = pagefault_data_segments(dev, pfault, wqe, wqe_end, &bytes_mapped,
				      &total_wqe_bytes, !sq);
	if (ret == -EAGAIN)
		goto out;

	if (ret < 0 || total_wqe_bytes > bytes_mapped)
		goto resolve_page_fault;

out:
	ret = 0;
	resume_with_error = 0;

read_user:
	if (ret)
		mlx5_ib_err(
			dev,
			"Failed reading a WQE following page fault, error %d, wqe_index %x, qpn %x\n",
			ret, wqe_index, pfault->token);

resolve_page_fault:
	mlx5_ib_page_fault_resume(dev, pfault, resume_with_error);
	mlx5_ib_dbg(dev, "PAGE FAULT completed. QP 0x%x resume_with_error=%d, type: 0x%x\n",
		    pfault->wqe.wq_num, resume_with_error,
		    pfault->type);
	mlx5_core_res_put(res);
	free_page((unsigned long)wqe);
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

	ret = pagefault_single_data_segment(dev, NULL, rkey, address, length,
					    &pfault->bytes_committed, NULL,
					    0);
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

		ret = pagefault_single_data_segment(dev, NULL, rkey, address,
						    prefetch_len,
						    &bytes_committed, NULL,
						    0);
		if (ret < 0 && ret != -EAGAIN) {
			mlx5_ib_dbg(dev, "Prefetch failed. ret: %d, QP 0x%x, address: 0x%.16llx, length = 0x%.16x\n",
				    ret, pfault->token, address, prefetch_len);
		}
	}
}

static void mlx5_ib_pfault(struct mlx5_ib_dev *dev, struct mlx5_pagefault *pfault)
{
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

static void mlx5_ib_eqe_pf_action(struct work_struct *work)
{
	struct mlx5_pagefault *pfault = container_of(work,
						     struct mlx5_pagefault,
						     work);
	struct mlx5_ib_pf_eq *eq = pfault->eq;

	mlx5_ib_pfault(eq->dev, pfault);
	mempool_free(pfault, eq->pool);
}

static void mlx5_ib_eq_pf_process(struct mlx5_ib_pf_eq *eq)
{
	struct mlx5_eqe_page_fault *pf_eqe;
	struct mlx5_pagefault *pfault;
	struct mlx5_eqe *eqe;
	int cc = 0;

	while ((eqe = mlx5_eq_get_eqe(eq->core, cc))) {
		pfault = mempool_alloc(eq->pool, GFP_ATOMIC);
		if (!pfault) {
			schedule_work(&eq->work);
			break;
		}

		pf_eqe = &eqe->data.page_fault;
		pfault->event_subtype = eqe->sub_type;
		pfault->bytes_committed = be32_to_cpu(pf_eqe->bytes_committed);

		mlx5_ib_dbg(eq->dev,
			    "PAGE_FAULT: subtype: 0x%02x, bytes_committed: 0x%06x\n",
			    eqe->sub_type, pfault->bytes_committed);

		switch (eqe->sub_type) {
		case MLX5_PFAULT_SUBTYPE_RDMA:
			/* RDMA based event */
			pfault->type =
				be32_to_cpu(pf_eqe->rdma.pftype_token) >> 24;
			pfault->token =
				be32_to_cpu(pf_eqe->rdma.pftype_token) &
				MLX5_24BIT_MASK;
			pfault->rdma.r_key =
				be32_to_cpu(pf_eqe->rdma.r_key);
			pfault->rdma.packet_size =
				be16_to_cpu(pf_eqe->rdma.packet_length);
			pfault->rdma.rdma_op_len =
				be32_to_cpu(pf_eqe->rdma.rdma_op_len);
			pfault->rdma.rdma_va =
				be64_to_cpu(pf_eqe->rdma.rdma_va);
			mlx5_ib_dbg(eq->dev,
				    "PAGE_FAULT: type:0x%x, token: 0x%06x, r_key: 0x%08x\n",
				    pfault->type, pfault->token,
				    pfault->rdma.r_key);
			mlx5_ib_dbg(eq->dev,
				    "PAGE_FAULT: rdma_op_len: 0x%08x, rdma_va: 0x%016llx\n",
				    pfault->rdma.rdma_op_len,
				    pfault->rdma.rdma_va);
			break;

		case MLX5_PFAULT_SUBTYPE_WQE:
			/* WQE based event */
			pfault->type =
				(be32_to_cpu(pf_eqe->wqe.pftype_wq) >> 24) & 0x7;
			pfault->token =
				be32_to_cpu(pf_eqe->wqe.token);
			pfault->wqe.wq_num =
				be32_to_cpu(pf_eqe->wqe.pftype_wq) &
				MLX5_24BIT_MASK;
			pfault->wqe.wqe_index =
				be16_to_cpu(pf_eqe->wqe.wqe_index);
			pfault->wqe.packet_size =
				be16_to_cpu(pf_eqe->wqe.packet_length);
			mlx5_ib_dbg(eq->dev,
				    "PAGE_FAULT: type:0x%x, token: 0x%06x, wq_num: 0x%06x, wqe_index: 0x%04x\n",
				    pfault->type, pfault->token,
				    pfault->wqe.wq_num,
				    pfault->wqe.wqe_index);
			break;

		default:
			mlx5_ib_warn(eq->dev,
				     "Unsupported page fault event sub-type: 0x%02hhx\n",
				     eqe->sub_type);
			/* Unsupported page faults should still be
			 * resolved by the page fault handler
			 */
		}

		pfault->eq = eq;
		INIT_WORK(&pfault->work, mlx5_ib_eqe_pf_action);
		queue_work(eq->wq, &pfault->work);

		cc = mlx5_eq_update_cc(eq->core, ++cc);
	}

	mlx5_eq_update_ci(eq->core, cc, 1);
}

static irqreturn_t mlx5_ib_eq_pf_int(int irq, void *eq_ptr)
{
	struct mlx5_ib_pf_eq *eq = eq_ptr;
	unsigned long flags;

	if (spin_trylock_irqsave(&eq->lock, flags)) {
		mlx5_ib_eq_pf_process(eq);
		spin_unlock_irqrestore(&eq->lock, flags);
	} else {
		schedule_work(&eq->work);
	}

	return IRQ_HANDLED;
}

/* mempool_refill() was proposed but unfortunately wasn't accepted
 * http://lkml.iu.edu/hypermail/linux/kernel/1512.1/05073.html
 * Cheap workaround.
 */
static void mempool_refill(mempool_t *pool)
{
	while (pool->curr_nr < pool->min_nr)
		mempool_free(mempool_alloc(pool, GFP_KERNEL), pool);
}

static void mlx5_ib_eq_pf_action(struct work_struct *work)
{
	struct mlx5_ib_pf_eq *eq =
		container_of(work, struct mlx5_ib_pf_eq, work);

	mempool_refill(eq->pool);

	spin_lock_irq(&eq->lock);
	mlx5_ib_eq_pf_process(eq);
	spin_unlock_irq(&eq->lock);
}

enum {
	MLX5_IB_NUM_PF_EQE	= 0x1000,
	MLX5_IB_NUM_PF_DRAIN	= 64,
};

static int
mlx5_ib_create_pf_eq(struct mlx5_ib_dev *dev, struct mlx5_ib_pf_eq *eq)
{
	struct mlx5_eq_param param = {};
	int err;

	INIT_WORK(&eq->work, mlx5_ib_eq_pf_action);
	spin_lock_init(&eq->lock);
	eq->dev = dev;

	eq->pool = mempool_create_kmalloc_pool(MLX5_IB_NUM_PF_DRAIN,
					       sizeof(struct mlx5_pagefault));
	if (!eq->pool)
		return -ENOMEM;

	eq->wq = alloc_workqueue("mlx5_ib_page_fault",
				 WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM,
				 MLX5_NUM_CMD_EQE);
	if (!eq->wq) {
		err = -ENOMEM;
		goto err_mempool;
	}

	param = (struct mlx5_eq_param) {
		.index = MLX5_EQ_PFAULT_IDX,
		.mask = 1 << MLX5_EVENT_TYPE_PAGE_FAULT,
		.nent = MLX5_IB_NUM_PF_EQE,
		.context = eq,
		.handler = mlx5_ib_eq_pf_int
	};
	eq->core = mlx5_eq_create_generic(dev->mdev, "mlx5_ib_page_fault_eq", &param);
	if (IS_ERR(eq->core)) {
		err = PTR_ERR(eq->core);
		goto err_wq;
	}

	return 0;
err_wq:
	destroy_workqueue(eq->wq);
err_mempool:
	mempool_destroy(eq->pool);
	return err;
}

static int
mlx5_ib_destroy_pf_eq(struct mlx5_ib_dev *dev, struct mlx5_ib_pf_eq *eq)
{
	int err;

	err = mlx5_eq_destroy_generic(dev->mdev, eq->core);
	cancel_work_sync(&eq->work);
	destroy_workqueue(eq->wq);
	mempool_destroy(eq->pool);

	return err;
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

static const struct ib_device_ops mlx5_ib_dev_odp_ops = {
	.advise_mr = mlx5_ib_advise_mr,
};

int mlx5_ib_odp_init_one(struct mlx5_ib_dev *dev)
{
	int ret = 0;

	if (dev->odp_caps.general_caps & IB_ODP_SUPPORT)
		ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_odp_ops);

	if (dev->odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT) {
		ret = mlx5_cmd_null_mkey(dev->mdev, &dev->null_mkey);
		if (ret) {
			mlx5_ib_err(dev, "Error getting null_mkey %d\n", ret);
			return ret;
		}
	}

	if (!MLX5_CAP_GEN(dev->mdev, pg))
		return ret;

	ret = mlx5_ib_create_pf_eq(dev, &dev->odp_pf_eq);

	return ret;
}

void mlx5_ib_odp_cleanup_one(struct mlx5_ib_dev *dev)
{
	if (!MLX5_CAP_GEN(dev->mdev, pg))
		return;

	mlx5_ib_destroy_pf_eq(dev, &dev->odp_pf_eq);
}

int mlx5_ib_odp_init(void)
{
	mlx5_imr_ksm_entries = BIT_ULL(get_order(TASK_SIZE) -
				       MLX5_IMR_MTT_BITS);

	return 0;
}

struct prefetch_mr_work {
	struct work_struct work;
	struct ib_pd *pd;
	u32 pf_flags;
	u32 num_sge;
	struct ib_sge sg_list[0];
};

static void num_pending_prefetch_dec(struct mlx5_ib_dev *dev,
				     struct ib_sge *sg_list, u32 num_sge,
				     u32 from)
{
	u32 i;
	int srcu_key;

	srcu_key = srcu_read_lock(&dev->mr_srcu);

	for (i = from; i < num_sge; ++i) {
		struct mlx5_core_mkey *mmkey;
		struct mlx5_ib_mr *mr;

		mmkey = __mlx5_mr_lookup(dev->mdev,
					 mlx5_base_mkey(sg_list[i].lkey));
		mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);
		atomic_dec(&mr->num_pending_prefetch);
	}

	srcu_read_unlock(&dev->mr_srcu, srcu_key);
}

static bool num_pending_prefetch_inc(struct ib_pd *pd,
				     struct ib_sge *sg_list, u32 num_sge)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	bool ret = true;
	u32 i;

	for (i = 0; i < num_sge; ++i) {
		struct mlx5_core_mkey *mmkey;
		struct mlx5_ib_mr *mr;

		mmkey = __mlx5_mr_lookup(dev->mdev,
					 mlx5_base_mkey(sg_list[i].lkey));
		if (!mmkey || mmkey->key != sg_list[i].lkey) {
			ret = false;
			break;
		}

		if (mmkey->type != MLX5_MKEY_MR) {
			ret = false;
			break;
		}

		mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);

		if (mr->ibmr.pd != pd) {
			ret = false;
			break;
		}

		if (!mr->live) {
			ret = false;
			break;
		}

		atomic_inc(&mr->num_pending_prefetch);
	}

	if (!ret)
		num_pending_prefetch_dec(dev, sg_list, i, 0);

	return ret;
}

static int mlx5_ib_prefetch_sg_list(struct ib_pd *pd, u32 pf_flags,
				    struct ib_sge *sg_list, u32 num_sge)
{
	u32 i;
	int ret = 0;
	struct mlx5_ib_dev *dev = to_mdev(pd->device);

	for (i = 0; i < num_sge; ++i) {
		struct ib_sge *sg = &sg_list[i];
		int bytes_committed = 0;

		ret = pagefault_single_data_segment(dev, pd, sg->lkey, sg->addr,
						    sg->length,
						    &bytes_committed, NULL,
						    pf_flags);
		if (ret < 0)
			break;
	}

	return ret < 0 ? ret : 0;
}

static void mlx5_ib_prefetch_mr_work(struct work_struct *work)
{
	struct prefetch_mr_work *w =
		container_of(work, struct prefetch_mr_work, work);

	if (ib_device_try_get(w->pd->device)) {
		mlx5_ib_prefetch_sg_list(w->pd, w->pf_flags, w->sg_list,
					 w->num_sge);
		ib_device_put(w->pd->device);
	}

	num_pending_prefetch_dec(to_mdev(w->pd->device), w->sg_list,
				 w->num_sge, 0);
	kfree(w);
}

int mlx5_ib_advise_mr_prefetch(struct ib_pd *pd,
			       enum ib_uverbs_advise_mr_advice advice,
			       u32 flags, struct ib_sge *sg_list, u32 num_sge)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	u32 pf_flags = MLX5_PF_FLAGS_PREFETCH;
	struct prefetch_mr_work *work;
	bool valid_req;
	int srcu_key;

	if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH)
		pf_flags |= MLX5_PF_FLAGS_DOWNGRADE;

	if (flags & IB_UVERBS_ADVISE_MR_FLAG_FLUSH)
		return mlx5_ib_prefetch_sg_list(pd, pf_flags, sg_list,
						num_sge);

	work = kvzalloc(struct_size(work, sg_list, num_sge), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	memcpy(work->sg_list, sg_list, num_sge * sizeof(struct ib_sge));

	/* It is guaranteed that the pd when work is executed is the pd when
	 * work was queued since pd can't be destroyed while it holds MRs and
	 * destroying a MR leads to flushing the workquque
	 */
	work->pd = pd;
	work->pf_flags = pf_flags;
	work->num_sge = num_sge;

	INIT_WORK(&work->work, mlx5_ib_prefetch_mr_work);

	srcu_key = srcu_read_lock(&dev->mr_srcu);

	valid_req = num_pending_prefetch_inc(pd, sg_list, num_sge);
	if (valid_req)
		queue_work(system_unbound_wq, &work->work);
	else
		kfree(work);

	srcu_read_unlock(&dev->mr_srcu, srcu_key);

	return valid_req ? 0 : -EINVAL;
}
