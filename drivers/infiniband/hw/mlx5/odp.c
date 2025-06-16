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

#include <rdma/ib_umem_odp.h>
#include <linux/kernel.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/hmm.h>
#include <linux/hmm-dma.h>
#include <linux/pci-p2pdma.h>

#include "mlx5_ib.h"
#include "cmd.h"
#include "umr.h"
#include "qp.h"

#include <linux/mlx5/eq.h>

/* Contains the details of a pagefault. */
struct mlx5_pagefault {
	u32			bytes_committed;
	u64			token;
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
		struct {
			u64	va;
			u32	mkey;
			u32	fault_byte_count;
			u32     prefetch_before_byte_count;
			u32     prefetch_after_byte_count;
			u8	flags;
		} memory;
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

static void populate_klm(struct mlx5_klm *pklm, size_t idx, size_t nentries,
			struct mlx5_ib_mr *imr, int flags)
{
	struct mlx5_core_dev *dev = mr_to_mdev(imr)->mdev;
	struct mlx5_klm *end = pklm + nentries;
	int step = MLX5_CAP_ODP(dev, mem_page_fault) ? MLX5_IMR_MTT_SIZE : 0;
	__be32 key = MLX5_CAP_ODP(dev, mem_page_fault) ?
			     cpu_to_be32(imr->null_mmkey.key) :
			     mr_to_mdev(imr)->mkeys.null_mkey;
	u64 va =
		MLX5_CAP_ODP(dev, mem_page_fault) ? idx * MLX5_IMR_MTT_SIZE : 0;

	if (flags & MLX5_IB_UPD_XLT_ZAP) {
		for (; pklm != end; pklm++, idx++, va += step) {
			pklm->bcount = cpu_to_be32(MLX5_IMR_MTT_SIZE);
			pklm->key = key;
			pklm->va = cpu_to_be64(va);
		}
		return;
	}

	/*
	 * The locking here is pretty subtle. Ideally the implicit_children
	 * xarray would be protected by the umem_mutex, however that is not
	 * possible. Instead this uses a weaker update-then-lock pattern:
	 *
	 *    xa_store()
	 *    mutex_lock(umem_mutex)
	 *     mlx5r_umr_update_xlt()
	 *    mutex_unlock(umem_mutex)
	 *    destroy lkey
	 *
	 * ie any change the xarray must be followed by the locked update_xlt
	 * before destroying.
	 *
	 * The umem_mutex provides the acquire/release semantic needed to make
	 * the xa_store() visible to a racing thread.
	 */
	lockdep_assert_held(&to_ib_umem_odp(imr->umem)->umem_mutex);

	for (; pklm != end; pklm++, idx++, va += step) {
		struct mlx5_ib_mr *mtt = xa_load(&imr->implicit_children, idx);

		pklm->bcount = cpu_to_be32(MLX5_IMR_MTT_SIZE);
		if (mtt) {
			pklm->key = cpu_to_be32(mtt->ibmr.lkey);
			pklm->va = cpu_to_be64(idx * MLX5_IMR_MTT_SIZE);
		} else {
			pklm->key = key;
			pklm->va = cpu_to_be64(va);
		}
	}
}

static int populate_mtt(__be64 *pas, size_t start, size_t nentries,
			struct mlx5_ib_mr *mr, int flags)
{
	struct ib_umem_odp *odp = to_ib_umem_odp(mr->umem);
	bool downgrade = flags & MLX5_IB_UPD_XLT_DOWNGRADE;
	struct pci_p2pdma_map_state p2pdma_state = {};
	struct ib_device *dev = odp->umem.ibdev;
	size_t i;

	if (flags & MLX5_IB_UPD_XLT_ZAP)
		return 0;

	for (i = 0; i < nentries; i++) {
		unsigned long pfn = odp->map.pfn_list[start + i];
		dma_addr_t dma_addr;

		pfn = odp->map.pfn_list[start + i];
		if (!(pfn & HMM_PFN_VALID))
			/* ODP initialization */
			continue;

		dma_addr = hmm_dma_map_pfn(dev->dma_device, &odp->map,
					   start + i, &p2pdma_state);
		if (ib_dma_mapping_error(dev, dma_addr))
			return -EFAULT;

		dma_addr |= MLX5_IB_MTT_READ;
		if ((pfn & HMM_PFN_WRITE) && !downgrade)
			dma_addr |= MLX5_IB_MTT_WRITE;

		pas[i] = cpu_to_be64(dma_addr);
		odp->npages++;
	}
	return 0;
}

int mlx5_odp_populate_xlt(void *xlt, size_t idx, size_t nentries,
			  struct mlx5_ib_mr *mr, int flags)
{
	if (flags & MLX5_IB_UPD_XLT_INDIRECT) {
		populate_klm(xlt, idx, nentries, mr, flags);
		return 0;
	} else {
		return populate_mtt(xlt, idx, nentries, mr, flags);
	}
}

/*
 * This must be called after the mr has been removed from implicit_children.
 * NOTE: The MR does not necessarily have to be
 * empty here, parallel page faults could have raced with the free process and
 * added pages to it.
 */
static void free_implicit_child_mr_work(struct work_struct *work)
{
	struct mlx5_ib_mr *mr =
		container_of(work, struct mlx5_ib_mr, odp_destroy.work);
	struct mlx5_ib_mr *imr = mr->parent;
	struct ib_umem_odp *odp_imr = to_ib_umem_odp(imr->umem);
	struct ib_umem_odp *odp = to_ib_umem_odp(mr->umem);

	mlx5r_deref_wait_odp_mkey(&mr->mmkey);

	mutex_lock(&odp_imr->umem_mutex);
	mlx5r_umr_update_xlt(mr->parent,
			     ib_umem_start(odp) >> MLX5_IMR_MTT_SHIFT, 1, 0,
			     MLX5_IB_UPD_XLT_INDIRECT | MLX5_IB_UPD_XLT_ATOMIC);
	mutex_unlock(&odp_imr->umem_mutex);
	mlx5_ib_dereg_mr(&mr->ibmr, NULL);

	mlx5r_deref_odp_mkey(&imr->mmkey);
}

static void destroy_unused_implicit_child_mr(struct mlx5_ib_mr *mr)
{
	struct ib_umem_odp *odp = to_ib_umem_odp(mr->umem);
	unsigned long idx = ib_umem_start(odp) >> MLX5_IMR_MTT_SHIFT;
	struct mlx5_ib_mr *imr = mr->parent;

	/*
	 * If userspace is racing freeing the parent implicit ODP MR then we can
	 * loose the race with parent destruction. In this case
	 * mlx5_ib_free_odp_mr() will free everything in the implicit_children
	 * xarray so NOP is fine. This child MR cannot be destroyed here because
	 * we are under its umem_mutex.
	 */
	if (!refcount_inc_not_zero(&imr->mmkey.usecount))
		return;

	xa_lock(&imr->implicit_children);
	if (__xa_cmpxchg(&imr->implicit_children, idx, mr, NULL, GFP_KERNEL) !=
	    mr) {
		xa_unlock(&imr->implicit_children);
		mlx5r_deref_odp_mkey(&imr->mmkey);
		return;
	}

	if (MLX5_CAP_ODP(mr_to_mdev(mr)->mdev, mem_page_fault))
		xa_erase(&mr_to_mdev(mr)->odp_mkeys,
			 mlx5_base_mkey(mr->mmkey.key));
	xa_unlock(&imr->implicit_children);

	/* Freeing a MR is a sleeping operation, so bounce to a work queue */
	INIT_WORK(&mr->odp_destroy.work, free_implicit_child_mr_work);
	queue_work(system_unbound_wq, &mr->odp_destroy.work);
}

static bool mlx5_ib_invalidate_range(struct mmu_interval_notifier *mni,
				     const struct mmu_notifier_range *range,
				     unsigned long cur_seq)
{
	struct ib_umem_odp *umem_odp =
		container_of(mni, struct ib_umem_odp, notifier);
	struct mlx5_ib_mr *mr;
	const u64 umr_block_mask = MLX5_UMR_MTT_NUM_ENTRIES_ALIGNMENT - 1;
	u64 idx = 0, blk_start_idx = 0;
	u64 invalidations = 0;
	unsigned long start;
	unsigned long end;
	int in_block = 0;
	u64 addr;

	if (!mmu_notifier_range_blockable(range))
		return false;

	mutex_lock(&umem_odp->umem_mutex);
	mmu_interval_set_seq(mni, cur_seq);
	/*
	 * If npages is zero then umem_odp->private may not be setup yet. This
	 * does not complete until after the first page is mapped for DMA.
	 */
	if (!umem_odp->npages)
		goto out;
	mr = umem_odp->private;
	if (!mr)
		goto out;

	start = max_t(u64, ib_umem_start(umem_odp), range->start);
	end = min_t(u64, ib_umem_end(umem_odp), range->end);

	/*
	 * Iteration one - zap the HW's MTTs. The notifiers_count ensures that
	 * while we are doing the invalidation, no page fault will attempt to
	 * overwrite the same MTTs.  Concurent invalidations might race us,
	 * but they will write 0s as well, so no difference in the end result.
	 */
	for (addr = start; addr < end; addr += BIT(umem_odp->page_shift)) {
		idx = (addr - ib_umem_start(umem_odp)) >> umem_odp->page_shift;
		/*
		 * Strive to write the MTTs in chunks, but avoid overwriting
		 * non-existing MTTs. The huristic here can be improved to
		 * estimate the cost of another UMR vs. the cost of bigger
		 * UMR.
		 */
		if (umem_odp->map.pfn_list[idx] & HMM_PFN_VALID) {
			if (!in_block) {
				blk_start_idx = idx;
				in_block = 1;
			}
		} else {
			u64 umr_offset = idx & umr_block_mask;

			if (in_block && umr_offset == 0) {
				mlx5r_umr_update_xlt(mr, blk_start_idx,
						     idx - blk_start_idx, 0,
						     MLX5_IB_UPD_XLT_ZAP |
						     MLX5_IB_UPD_XLT_ATOMIC);
				in_block = 0;
				/* Count page invalidations */
				invalidations += idx - blk_start_idx + 1;
			}
		}
	}
	if (in_block) {
		mlx5r_umr_update_xlt(mr, blk_start_idx,
				     idx - blk_start_idx + 1, 0,
				     MLX5_IB_UPD_XLT_ZAP |
				     MLX5_IB_UPD_XLT_ATOMIC);
		/* Count page invalidations */
		invalidations += idx - blk_start_idx + 1;
	}

	mlx5_update_odp_stats_with_handled(mr, invalidations, invalidations);

	/*
	 * We are now sure that the device will not access the
	 * memory. We can safely unmap it, and mark it as dirty if
	 * needed.
	 */

	ib_umem_odp_unmap_dma_pages(umem_odp, start, end);

	if (unlikely(!umem_odp->npages && mr->parent))
		destroy_unused_implicit_child_mr(mr);
out:
	mutex_unlock(&umem_odp->umem_mutex);
	return true;
}

const struct mmu_interval_notifier_ops mlx5_mn_ops = {
	.invalidate = mlx5_ib_invalidate_range,
};

static void internal_fill_odp_caps(struct mlx5_ib_dev *dev)
{
	struct ib_odp_caps *caps = &dev->odp_caps;

	memset(caps, 0, sizeof(*caps));

	if (!MLX5_CAP_GEN(dev->mdev, pg) || !mlx5r_umr_can_load_pas(dev, 0))
		return;

	caps->general_caps = IB_ODP_SUPPORT;

	if (MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset))
		dev->odp_max_size = U64_MAX;
	else
		dev->odp_max_size = BIT_ULL(MLX5_MAX_UMR_SHIFT + PAGE_SHIFT);

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, ud_odp_caps.send))
		caps->per_transport_caps.ud_odp_caps |= IB_ODP_SUPPORT_SEND;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, ud_odp_caps.srq_receive))
		caps->per_transport_caps.ud_odp_caps |= IB_ODP_SUPPORT_SRQ_RECV;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, rc_odp_caps.send))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_SEND;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, rc_odp_caps.receive))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_RECV;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, rc_odp_caps.write))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_WRITE;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, rc_odp_caps.read))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_READ;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, rc_odp_caps.atomic))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_ATOMIC;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, rc_odp_caps.srq_receive))
		caps->per_transport_caps.rc_odp_caps |= IB_ODP_SUPPORT_SRQ_RECV;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, xrc_odp_caps.send))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_SEND;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, xrc_odp_caps.receive))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_RECV;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, xrc_odp_caps.write))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_WRITE;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, xrc_odp_caps.read))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_READ;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, xrc_odp_caps.atomic))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_ATOMIC;

	if (MLX5_CAP_ODP_SCHEME(dev->mdev, xrc_odp_caps.srq_receive))
		caps->per_transport_caps.xrc_odp_caps |= IB_ODP_SUPPORT_SRQ_RECV;

	if (MLX5_CAP_GEN(dev->mdev, fixed_buffer_size) &&
	    MLX5_CAP_GEN(dev->mdev, null_mkey) &&
	    MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset) &&
	    !MLX5_CAP_GEN(dev->mdev, umr_indirect_mkey_disabled))
		caps->general_caps |= IB_ODP_SUPPORT_IMPLICIT;
}

static void mlx5_ib_page_fault_resume(struct mlx5_ib_dev *dev,
				      struct mlx5_pagefault *pfault,
				      int error)
{
	int wq_num = pfault->event_subtype == MLX5_PFAULT_SUBTYPE_WQE ?
		     pfault->wqe.wq_num : pfault->token;
	u32 in[MLX5_ST_SZ_DW(page_fault_resume_in)] = {};
	void *info;
	int err;

	MLX5_SET(page_fault_resume_in, in, opcode, MLX5_CMD_OP_PAGE_FAULT_RESUME);

	if (pfault->event_subtype == MLX5_PFAULT_SUBTYPE_MEMORY) {
		info = MLX5_ADDR_OF(page_fault_resume_in, in,
				    page_fault_info.mem_page_fault_info);
		MLX5_SET(mem_page_fault_info, info, fault_token_31_0,
			 pfault->token & 0xffffffff);
		MLX5_SET(mem_page_fault_info, info, fault_token_47_32,
			 (pfault->token >> 32) & 0xffff);
		MLX5_SET(mem_page_fault_info, info, error, !!error);
	} else {
		info = MLX5_ADDR_OF(page_fault_resume_in, in,
				    page_fault_info.trans_page_fault_info);
		MLX5_SET(trans_page_fault_info, info, page_fault_type,
			 pfault->type);
		MLX5_SET(trans_page_fault_info, info, fault_token,
			 pfault->token);
		MLX5_SET(trans_page_fault_info, info, wq_number, wq_num);
		MLX5_SET(trans_page_fault_info, info, error, !!error);
	}

	err = mlx5_cmd_exec_in(dev->mdev, page_fault_resume, in);
	if (err)
		mlx5_ib_err(dev, "Failed to resolve the page fault on WQ 0x%x err %d\n",
			    wq_num, err);
}

static struct mlx5_ib_mr *implicit_get_child_mr(struct mlx5_ib_mr *imr,
						unsigned long idx)
{
	struct mlx5_ib_dev *dev = mr_to_mdev(imr);
	struct ib_umem_odp *odp;
	struct mlx5_ib_mr *mr;
	struct mlx5_ib_mr *ret;
	int err;

	odp = ib_umem_odp_alloc_child(to_ib_umem_odp(imr->umem),
				      idx * MLX5_IMR_MTT_SIZE,
				      MLX5_IMR_MTT_SIZE, &mlx5_mn_ops);
	if (IS_ERR(odp))
		return ERR_CAST(odp);

	mr = mlx5_mr_cache_alloc(dev, imr->access_flags,
				 MLX5_MKC_ACCESS_MODE_MTT,
				 MLX5_IMR_MTT_ENTRIES);
	if (IS_ERR(mr)) {
		ib_umem_odp_release(odp);
		return mr;
	}

	mr->access_flags = imr->access_flags;
	mr->ibmr.pd = imr->ibmr.pd;
	mr->ibmr.device = &mr_to_mdev(imr)->ib_dev;
	mr->umem = &odp->umem;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->ibmr.iova = idx * MLX5_IMR_MTT_SIZE;
	mr->parent = imr;
	odp->private = mr;

	/*
	 * First refcount is owned by the xarray and second refconut
	 * is returned to the caller.
	 */
	refcount_set(&mr->mmkey.usecount, 2);

	err = mlx5r_umr_update_xlt(mr, 0,
				   MLX5_IMR_MTT_ENTRIES,
				   PAGE_SHIFT,
				   MLX5_IB_UPD_XLT_ZAP |
				   MLX5_IB_UPD_XLT_ENABLE);
	if (err) {
		ret = ERR_PTR(err);
		goto out_mr;
	}

	xa_lock(&imr->implicit_children);
	ret = __xa_cmpxchg(&imr->implicit_children, idx, NULL, mr,
			   GFP_KERNEL);
	if (unlikely(ret)) {
		if (xa_is_err(ret)) {
			ret = ERR_PTR(xa_err(ret));
			goto out_lock;
		}
		/*
		 * Another thread beat us to creating the child mr, use
		 * theirs.
		 */
		refcount_inc(&ret->mmkey.usecount);
		goto out_lock;
	}

	if (MLX5_CAP_ODP(dev->mdev, mem_page_fault)) {
		ret = xa_store(&dev->odp_mkeys, mlx5_base_mkey(mr->mmkey.key),
			       &mr->mmkey, GFP_KERNEL);
		if (xa_is_err(ret)) {
			ret = ERR_PTR(xa_err(ret));
			__xa_erase(&imr->implicit_children, idx);
			goto out_lock;
		}
		mr->mmkey.type = MLX5_MKEY_IMPLICIT_CHILD;
	}
	xa_unlock(&imr->implicit_children);
	mlx5_ib_dbg(mr_to_mdev(imr), "key %x mr %p\n", mr->mmkey.key, mr);
	return mr;

out_lock:
	xa_unlock(&imr->implicit_children);
out_mr:
	mlx5_ib_dereg_mr(&mr->ibmr, NULL);
	return ret;
}

/*
 * When using memory scheme ODP, implicit MRs can't use the reserved null mkey
 * and each implicit MR needs to assign a private null mkey to get the page
 * faults on.
 * The null mkey is created with the properties to enable getting the page
 * fault for every time it is accessed and having all relevant access flags.
 */
static int alloc_implicit_mr_null_mkey(struct mlx5_ib_dev *dev,
				       struct mlx5_ib_mr *imr,
				       struct mlx5_ib_pd *pd)
{
	size_t inlen = MLX5_ST_SZ_BYTES(create_mkey_in) + 64;
	void *mkc;
	u32 *in;
	int err;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_mkey_in, in, translations_octword_actual_size, 4);
	MLX5_SET(create_mkey_in, in, pg_access, 1);

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, a, 1);
	MLX5_SET(mkc, mkc, rw, 1);
	MLX5_SET(mkc, mkc, rr, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, free, 0);
	MLX5_SET(mkc, mkc, umr_en, 0);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_MTT);

	MLX5_SET(mkc, mkc, translations_octword_size, 4);
	MLX5_SET(mkc, mkc, log_page_size, 61);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, pd, pd->pdn);
	MLX5_SET64(mkc, mkc, start_addr, 0);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	err = mlx5_core_create_mkey(dev->mdev, &imr->null_mmkey.key, in, inlen);
	if (err)
		goto free_in;

	imr->null_mmkey.type = MLX5_MKEY_NULL;

free_in:
	kfree(in);
	return err;
}

struct mlx5_ib_mr *mlx5_ib_alloc_implicit_mr(struct mlx5_ib_pd *pd,
					     int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->ibpd.device);
	struct ib_umem_odp *umem_odp;
	struct mlx5_ib_mr *imr;
	int err;

	if (!mlx5r_umr_can_load_pas(dev, MLX5_IMR_MTT_ENTRIES * PAGE_SIZE))
		return ERR_PTR(-EOPNOTSUPP);

	umem_odp = ib_umem_odp_alloc_implicit(&dev->ib_dev, access_flags);
	if (IS_ERR(umem_odp))
		return ERR_CAST(umem_odp);

	imr = mlx5_mr_cache_alloc(dev, access_flags, MLX5_MKC_ACCESS_MODE_KSM,
				  mlx5_imr_ksm_entries);
	if (IS_ERR(imr)) {
		ib_umem_odp_release(umem_odp);
		return imr;
	}

	imr->access_flags = access_flags;
	imr->ibmr.pd = &pd->ibpd;
	imr->ibmr.iova = 0;
	imr->umem = &umem_odp->umem;
	imr->ibmr.lkey = imr->mmkey.key;
	imr->ibmr.rkey = imr->mmkey.key;
	imr->ibmr.device = &dev->ib_dev;
	imr->is_odp_implicit = true;
	xa_init(&imr->implicit_children);

	if (MLX5_CAP_ODP(dev->mdev, mem_page_fault)) {
		err = alloc_implicit_mr_null_mkey(dev, imr, pd);
		if (err)
			goto out_mr;

		err = mlx5r_store_odp_mkey(dev, &imr->null_mmkey);
		if (err)
			goto out_mr;
	}

	err = mlx5r_umr_update_xlt(imr, 0,
				   mlx5_imr_ksm_entries,
				   MLX5_KSM_PAGE_SHIFT,
				   MLX5_IB_UPD_XLT_INDIRECT |
				   MLX5_IB_UPD_XLT_ZAP |
				   MLX5_IB_UPD_XLT_ENABLE);
	if (err)
		goto out_mr;

	err = mlx5r_store_odp_mkey(dev, &imr->mmkey);
	if (err)
		goto out_mr;

	mlx5_ib_dbg(dev, "key %x mr %p\n", imr->mmkey.key, imr);
	return imr;
out_mr:
	mlx5_ib_err(dev, "Failed to register MKEY %d\n", err);
	mlx5_ib_dereg_mr(&imr->ibmr, NULL);
	return ERR_PTR(err);
}

void mlx5_ib_free_odp_mr(struct mlx5_ib_mr *mr)
{
	struct mlx5_ib_mr *mtt;
	unsigned long idx;

	/*
	 * If this is an implicit MR it is already invalidated so we can just
	 * delete the children mkeys.
	 */
	xa_for_each(&mr->implicit_children, idx, mtt) {
		xa_erase(&mr->implicit_children, idx);
		mlx5_ib_dereg_mr(&mtt->ibmr, NULL);
	}

	if (mr->null_mmkey.key) {
		xa_erase(&mr_to_mdev(mr)->odp_mkeys,
			 mlx5_base_mkey(mr->null_mmkey.key));

		mlx5_core_destroy_mkey(mr_to_mdev(mr)->mdev,
				       mr->null_mmkey.key);
	}
}

#define MLX5_PF_FLAGS_DOWNGRADE BIT(1)
#define MLX5_PF_FLAGS_SNAPSHOT BIT(2)
#define MLX5_PF_FLAGS_ENABLE BIT(3)
static int pagefault_real_mr(struct mlx5_ib_mr *mr, struct ib_umem_odp *odp,
			     u64 user_va, size_t bcnt, u32 *bytes_mapped,
			     u32 flags)
{
	int page_shift, ret, np;
	bool downgrade = flags & MLX5_PF_FLAGS_DOWNGRADE;
	u64 access_mask = 0;
	u64 start_idx;
	bool fault = !(flags & MLX5_PF_FLAGS_SNAPSHOT);
	u32 xlt_flags = MLX5_IB_UPD_XLT_ATOMIC;

	if (flags & MLX5_PF_FLAGS_ENABLE)
		xlt_flags |= MLX5_IB_UPD_XLT_ENABLE;

	if (flags & MLX5_PF_FLAGS_DOWNGRADE)
		xlt_flags |= MLX5_IB_UPD_XLT_DOWNGRADE;

	page_shift = odp->page_shift;
	start_idx = (user_va - ib_umem_start(odp)) >> page_shift;

	if (odp->umem.writable && !downgrade)
		access_mask |= HMM_PFN_WRITE;

	np = ib_umem_odp_map_dma_and_lock(odp, user_va, bcnt, access_mask, fault);
	if (np < 0)
		return np;

	/*
	 * No need to check whether the MTTs really belong to this MR, since
	 * ib_umem_odp_map_dma_and_lock already checks this.
	 */
	ret = mlx5r_umr_update_xlt(mr, start_idx, np, page_shift, xlt_flags);
	mutex_unlock(&odp->umem_mutex);

	if (ret < 0) {
		if (ret != -EAGAIN)
			mlx5_ib_err(mr_to_mdev(mr),
				    "Failed to update mkey page tables\n");
		goto out;
	}

	if (bytes_mapped) {
		u32 new_mappings = (np << page_shift) -
			(user_va - round_down(user_va, 1 << page_shift));

		*bytes_mapped += min_t(u32, new_mappings, bcnt);
	}

	return np << (page_shift - PAGE_SHIFT);

out:
	return ret;
}

static int pagefault_implicit_mr(struct mlx5_ib_mr *imr,
				 struct ib_umem_odp *odp_imr, u64 user_va,
				 size_t bcnt, u32 *bytes_mapped, u32 flags)
{
	unsigned long end_idx = (user_va + bcnt - 1) >> MLX5_IMR_MTT_SHIFT;
	unsigned long upd_start_idx = end_idx + 1;
	unsigned long upd_len = 0;
	unsigned long npages = 0;
	int err;
	int ret;

	if (unlikely(user_va >= mlx5_imr_ksm_entries * MLX5_IMR_MTT_SIZE ||
		     mlx5_imr_ksm_entries * MLX5_IMR_MTT_SIZE - user_va < bcnt))
		return -EFAULT;

	/* Fault each child mr that intersects with our interval. */
	while (bcnt) {
		unsigned long idx = user_va >> MLX5_IMR_MTT_SHIFT;
		struct ib_umem_odp *umem_odp;
		struct mlx5_ib_mr *mtt;
		u64 len;

		xa_lock(&imr->implicit_children);
		mtt = xa_load(&imr->implicit_children, idx);
		if (unlikely(!mtt)) {
			xa_unlock(&imr->implicit_children);
			mtt = implicit_get_child_mr(imr, idx);
			if (IS_ERR(mtt)) {
				ret = PTR_ERR(mtt);
				goto out;
			}
			upd_start_idx = min(upd_start_idx, idx);
			upd_len = idx - upd_start_idx + 1;
		} else {
			refcount_inc(&mtt->mmkey.usecount);
			xa_unlock(&imr->implicit_children);
		}

		umem_odp = to_ib_umem_odp(mtt->umem);
		len = min_t(u64, user_va + bcnt, ib_umem_end(umem_odp)) -
		      user_va;

		ret = pagefault_real_mr(mtt, umem_odp, user_va, len,
					bytes_mapped, flags);

		mlx5r_deref_odp_mkey(&mtt->mmkey);

		if (ret < 0)
			goto out;
		user_va += len;
		bcnt -= len;
		npages += ret;
	}

	ret = npages;

	/*
	 * Any time the implicit_children are changed we must perform an
	 * update of the xlt before exiting to ensure the HW and the
	 * implicit_children remains synchronized.
	 */
out:
	if (likely(!upd_len))
		return ret;

	/*
	 * Notice this is not strictly ordered right, the KSM is updated after
	 * the implicit_children is updated, so a parallel page fault could
	 * see a MR that is not yet visible in the KSM.  This is similar to a
	 * parallel page fault seeing a MR that is being concurrently removed
	 * from the KSM. Both of these improbable situations are resolved
	 * safely by resuming the HW and then taking another page fault. The
	 * next pagefault handler will see the new information.
	 */
	mutex_lock(&odp_imr->umem_mutex);
	err = mlx5r_umr_update_xlt(imr, upd_start_idx, upd_len, 0,
				   MLX5_IB_UPD_XLT_INDIRECT |
					  MLX5_IB_UPD_XLT_ATOMIC);
	mutex_unlock(&odp_imr->umem_mutex);
	if (err) {
		mlx5_ib_err(mr_to_mdev(imr), "Failed to update PAS\n");
		return err;
	}
	return ret;
}

static int pagefault_dmabuf_mr(struct mlx5_ib_mr *mr, size_t bcnt,
			       u32 *bytes_mapped, u32 flags)
{
	struct ib_umem_dmabuf *umem_dmabuf = to_ib_umem_dmabuf(mr->umem);
	u32 xlt_flags = 0;
	int err;
	unsigned long page_size;

	if (flags & MLX5_PF_FLAGS_ENABLE)
		xlt_flags |= MLX5_IB_UPD_XLT_ENABLE;

	dma_resv_lock(umem_dmabuf->attach->dmabuf->resv, NULL);
	err = ib_umem_dmabuf_map_pages(umem_dmabuf);
	if (err) {
		dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);
		return err;
	}

	page_size = mlx5_umem_dmabuf_find_best_pgsz(umem_dmabuf);
	if (!page_size) {
		ib_umem_dmabuf_unmap_pages(umem_dmabuf);
		err = -EINVAL;
	} else {
		if (mr->data_direct)
			err = mlx5r_umr_update_data_direct_ksm_pas(mr, xlt_flags);
		else
			err = mlx5r_umr_update_mr_pas(mr, xlt_flags);
	}
	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);

	if (err)
		return err;

	if (bytes_mapped)
		*bytes_mapped += bcnt;

	return ib_umem_num_pages(mr->umem);
}

/*
 * Returns:
 *  -EFAULT: The io_virt->bcnt is not within the MR, it covers pages that are
 *           not accessible, or the MR is no longer valid.
 *  -EAGAIN/-ENOMEM: The operation should be retried
 *
 *  -EINVAL/others: General internal malfunction
 *  >0: Number of pages mapped
 */
static int pagefault_mr(struct mlx5_ib_mr *mr, u64 io_virt, size_t bcnt,
			u32 *bytes_mapped, u32 flags, bool permissive_fault)
{
	struct ib_umem_odp *odp = to_ib_umem_odp(mr->umem);

	if (unlikely(io_virt < mr->ibmr.iova) && !permissive_fault)
		return -EFAULT;

	if (mr->umem->is_dmabuf)
		return pagefault_dmabuf_mr(mr, bcnt, bytes_mapped, flags);

	if (!odp->is_implicit_odp) {
		u64 offset = io_virt < mr->ibmr.iova ? 0 : io_virt - mr->ibmr.iova;
		u64 user_va;

		if (check_add_overflow(offset, (u64)odp->umem.address,
				       &user_va))
			return -EFAULT;

		if (permissive_fault) {
			if (user_va < ib_umem_start(odp))
				user_va = ib_umem_start(odp);
			if ((user_va + bcnt) > ib_umem_end(odp))
				bcnt = ib_umem_end(odp) - user_va;
		} else if (unlikely(user_va >= ib_umem_end(odp) ||
				    ib_umem_end(odp) - user_va < bcnt))
			return -EFAULT;
		return pagefault_real_mr(mr, odp, user_va, bcnt, bytes_mapped,
					 flags);
	}
	return pagefault_implicit_mr(mr, odp, io_virt, bcnt, bytes_mapped,
				     flags);
}

int mlx5_ib_init_odp_mr(struct mlx5_ib_mr *mr)
{
	int ret;

	ret = pagefault_real_mr(mr, to_ib_umem_odp(mr->umem), mr->umem->address,
				mr->umem->length, NULL,
				MLX5_PF_FLAGS_SNAPSHOT | MLX5_PF_FLAGS_ENABLE);
	return ret >= 0 ? 0 : ret;
}

int mlx5_ib_init_dmabuf_mr(struct mlx5_ib_mr *mr)
{
	int ret;

	ret = pagefault_dmabuf_mr(mr, mr->umem->length, NULL,
				  MLX5_PF_FLAGS_ENABLE);

	return ret >= 0 ? 0 : ret;
}

struct pf_frame {
	struct pf_frame *next;
	u32 key;
	u64 io_virt;
	size_t bcnt;
	int depth;
};

static bool mkey_is_eq(struct mlx5_ib_mkey *mmkey, u32 key)
{
	if (!mmkey)
		return false;
	if (mmkey->type == MLX5_MKEY_MW ||
	    mmkey->type == MLX5_MKEY_INDIRECT_DEVX)
		return mlx5_base_mkey(mmkey->key) == mlx5_base_mkey(key);
	return mmkey->key == key;
}

static struct mlx5_ib_mkey *find_odp_mkey(struct mlx5_ib_dev *dev, u32 key)
{
	struct mlx5_ib_mkey *mmkey;

	xa_lock(&dev->odp_mkeys);
	mmkey = xa_load(&dev->odp_mkeys, mlx5_base_mkey(key));
	if (!mmkey) {
		mmkey = ERR_PTR(-ENOENT);
		goto out;
	}
	if (!mkey_is_eq(mmkey, key)) {
		mmkey = ERR_PTR(-EFAULT);
		goto out;
	}
	refcount_inc(&mmkey->usecount);
out:
	xa_unlock(&dev->odp_mkeys);

	return mmkey;
}

/*
 * Handle a single data segment in a page-fault WQE or RDMA region.
 *
 * Returns zero on success. The caller may continue to the next data segment.
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
					 u32 *bytes_mapped)
{
	int ret, i, outlen, cur_outlen = 0, depth = 0, pages_in_range;
	struct pf_frame *head = NULL, *frame;
	struct mlx5_ib_mkey *mmkey;
	struct mlx5_ib_mr *mr;
	struct mlx5_klm *pklm;
	u32 *out = NULL;
	size_t offset;

	io_virt += *bytes_committed;
	bcnt -= *bytes_committed;
next_mr:
	mmkey = find_odp_mkey(dev, key);
	if (IS_ERR(mmkey)) {
		ret = PTR_ERR(mmkey);
		if (ret == -ENOENT) {
			mlx5_ib_dbg(
				dev,
				"skipping non ODP MR (lkey=0x%06x) in page fault handler.\n",
				key);
			if (bytes_mapped)
				*bytes_mapped += bcnt;
			/*
			 * The user could specify a SGL with multiple lkeys and
			 * only some of them are ODP. Treat the non-ODP ones as
			 * fully faulted.
			 */
			ret = 0;
		}
		goto end;
	}

	switch (mmkey->type) {
	case MLX5_MKEY_MR:
		mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);

		pages_in_range = (ALIGN(io_virt + bcnt, PAGE_SIZE) -
				  (io_virt & PAGE_MASK)) >>
				 PAGE_SHIFT;
		ret = pagefault_mr(mr, io_virt, bcnt, bytes_mapped, 0, false);
		if (ret < 0)
			goto end;

		mlx5_update_odp_stats_with_handled(mr, faults, ret);

		if (ret < pages_in_range) {
			ret = -EFAULT;
			goto end;
		}

		ret = 0;
		break;

	case MLX5_MKEY_MW:
	case MLX5_MKEY_INDIRECT_DEVX:
		if (depth >= MLX5_CAP_GEN(dev->mdev, max_indirection)) {
			mlx5_ib_dbg(dev, "indirection level exceeded\n");
			ret = -EFAULT;
			goto end;
		}

		outlen = MLX5_ST_SZ_BYTES(query_mkey_out) +
			sizeof(*pklm) * (mmkey->ndescs - 2);

		if (outlen > cur_outlen) {
			kfree(out);
			out = kzalloc(outlen, GFP_KERNEL);
			if (!out) {
				ret = -ENOMEM;
				goto end;
			}
			cur_outlen = outlen;
		}

		pklm = (struct mlx5_klm *)MLX5_ADDR_OF(query_mkey_out, out,
						       bsf0_klm0_pas_mtt0_1);

		ret = mlx5_core_query_mkey(dev->mdev, mmkey->key, out, outlen);
		if (ret)
			goto end;

		offset = io_virt - MLX5_GET64(query_mkey_out, out,
					      memory_key_mkey_entry.start_addr);

		for (i = 0; bcnt && i < mmkey->ndescs; i++, pklm++) {
			if (offset >= be32_to_cpu(pklm->bcount)) {
				offset -= be32_to_cpu(pklm->bcount);
				continue;
			}

			frame = kzalloc(sizeof(*frame), GFP_KERNEL);
			if (!frame) {
				ret = -ENOMEM;
				goto end;
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
		goto end;
	}

	if (head) {
		frame = head;
		head = frame->next;

		key = frame->key;
		io_virt = frame->io_virt;
		bcnt = frame->bcnt;
		depth = frame->depth;
		kfree(frame);

		mlx5r_deref_odp_mkey(mmkey);
		goto next_mr;
	}

end:
	if (!IS_ERR(mmkey))
		mlx5r_deref_odp_mkey(mmkey);
	while (head) {
		frame = head;
		head = frame->next;
		kfree(frame);
	}
	kfree(out);

	*bytes_committed = 0;
	return ret;
}

/*
 * Parse a series of data segments for page fault handling.
 *
 * @dev:  Pointer to mlx5 IB device
 * @pfault: contains page fault information.
 * @wqe: points at the first data segment in the WQE.
 * @wqe_end: points after the end of the WQE.
 * @bytes_mapped: receives the number of bytes that the function was able to
 *                map. This allows the caller to decide intelligently whether
 *                enough memory was mapped to resolve the page fault
 *                successfully (e.g. enough for the next MTU, or the entire
 *                WQE).
 * @total_wqe_bytes: receives the total data size of this WQE in bytes (minus
 *                   the committed bytes).
 * @receive_queue: receive WQE end of sg list
 *
 * Returns zero for success or a negative error code.
 */
static int pagefault_data_segments(struct mlx5_ib_dev *dev,
				   struct mlx5_pagefault *pfault,
				   void *wqe,
				   void *wqe_end, u32 *bytes_mapped,
				   u32 *total_wqe_bytes, bool receive_queue)
{
	int ret = 0;
	u64 io_virt;
	__be32 key;
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
		key = dseg->lkey;
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
		if (receive_queue && bcnt == 0 &&
		    key == dev->mkeys.terminate_scatter_list_mkey &&
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

		ret = pagefault_single_data_segment(dev, NULL, be32_to_cpu(key),
						    io_virt, bcnt,
						    &pfault->bytes_committed,
						    bytes_mapped);
		if (ret < 0)
			break;
	}

	return ret;
}

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
	struct mlx5_base_av *av;
	unsigned ds, opcode;
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

	*wqe_end = *wqe + ds * MLX5_WQE_DS_UNITS;
	*wqe += sizeof(*ctrl);

	opcode = be32_to_cpu(ctrl->opmod_idx_opcode) &
		 MLX5_WQE_CTRL_OPCODE_MASK;

	if (qp->type == IB_QPT_XRC_INI)
		*wqe += sizeof(struct mlx5_wqe_xrc_seg);

	if (qp->type == IB_QPT_UD || qp->type == MLX5_IB_QPT_DCI) {
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

	if (qp->flags_en & MLX5_QP_FLAG_SIGNATURE) {
		mlx5_ib_err(dev, "ODP fault with WQE signatures is not supported\n");
		return -EFAULT;
	}

	if (wqe_size > wqe_length) {
		mlx5_ib_err(dev, "Couldn't read all of the receive WQE's content\n");
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
		common = mlx5_core_res_hold(dev, wq_num, MLX5_RES_QP);
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
	void *wqe, *wqe_start = NULL, *wqe_end = NULL;
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

	wqe_start = (void *)__get_free_page(GFP_KERNEL);
	if (!wqe_start) {
		mlx5_ib_err(dev, "Error allocating memory for IO page fault handling.\n");
		goto resolve_page_fault;
	}

	wqe = wqe_start;
	qp = (res->res == MLX5_RES_QP) ? res_to_qp(res) : NULL;
	if (qp && sq) {
		ret = mlx5_ib_read_wqe_sq(qp, wqe_index, wqe, PAGE_SIZE,
					  &bytes_copied);
		if (ret)
			goto read_user;
		ret = mlx5_ib_mr_initiator_pfault_handler(
			dev, pfault, qp, &wqe, &wqe_end, bytes_copied);
	} else if (qp && !sq) {
		ret = mlx5_ib_read_wqe_rq(qp, wqe_index, wqe, PAGE_SIZE,
					  &bytes_copied);
		if (ret)
			goto read_user;
		ret = mlx5_ib_mr_responder_pfault_handler_rq(
			dev, qp, wqe, &wqe_end, bytes_copied);
	} else if (!qp) {
		struct mlx5_ib_srq *srq = res_to_srq(res);

		ret = mlx5_ib_read_wqe_srq(srq, wqe_index, wqe, PAGE_SIZE,
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
			"Failed reading a WQE following page fault, error %d, wqe_index %x, qpn %llx\n",
			ret, wqe_index, pfault->token);

resolve_page_fault:
	mlx5_ib_page_fault_resume(dev, pfault, resume_with_error);
	mlx5_ib_dbg(dev, "PAGE FAULT completed. QP 0x%x resume_with_error=%d, type: 0x%x\n",
		    pfault->wqe.wq_num, resume_with_error,
		    pfault->type);
	mlx5_core_res_put(res);
	free_page((unsigned long)wqe_start);
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
					    &pfault->bytes_committed, NULL);
	if (ret == -EAGAIN) {
		/* We're racing with an invalidation, don't prefetch */
		prefetch_activated = 0;
	} else if (ret < 0) {
		mlx5_ib_page_fault_resume(dev, pfault, 1);
		if (ret != -ENOENT)
			mlx5_ib_dbg(dev, "PAGE FAULT error %d. QP 0x%llx, type: 0x%x\n",
				    ret, pfault->token, pfault->type);
		return;
	}

	mlx5_ib_page_fault_resume(dev, pfault, 0);
	mlx5_ib_dbg(dev, "PAGE FAULT completed. QP 0x%llx, type: 0x%x, prefetch_activated: %d\n",
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
						    &bytes_committed, NULL);
		if (ret < 0 && ret != -EAGAIN) {
			mlx5_ib_dbg(dev, "Prefetch failed. ret: %d, QP 0x%llx, address: 0x%.16llx, length = 0x%.16x\n",
				    ret, pfault->token, address, prefetch_len);
		}
	}
}

#define MLX5_MEMORY_PAGE_FAULT_FLAGS_LAST BIT(7)
static void mlx5_ib_mr_memory_pfault_handler(struct mlx5_ib_dev *dev,
					     struct mlx5_pagefault *pfault)
{
	u64 prefetch_va =
		pfault->memory.va - pfault->memory.prefetch_before_byte_count;
	size_t prefetch_size = pfault->memory.prefetch_before_byte_count +
			       pfault->memory.fault_byte_count +
			       pfault->memory.prefetch_after_byte_count;
	struct mlx5_ib_mkey *mmkey;
	struct mlx5_ib_mr *mr, *child_mr;
	int ret = 0;

	mmkey = find_odp_mkey(dev, pfault->memory.mkey);
	if (IS_ERR(mmkey))
		goto err;

	switch (mmkey->type) {
	case MLX5_MKEY_IMPLICIT_CHILD:
		child_mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);
		mr = child_mr->parent;
		break;
	case MLX5_MKEY_NULL:
		mr = container_of(mmkey, struct mlx5_ib_mr, null_mmkey);
		break;
	default:
		mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);
		break;
	}

	/* If prefetch fails, handle only demanded page fault */
	ret = pagefault_mr(mr, prefetch_va, prefetch_size, NULL, 0, true);
	if (ret < 0) {
		ret = pagefault_mr(mr, pfault->memory.va,
				   pfault->memory.fault_byte_count, NULL, 0,
				   true);
		if (ret < 0)
			goto err;
	}

	mlx5_update_odp_stats_with_handled(mr, faults, ret);
	mlx5r_deref_odp_mkey(mmkey);

	if (pfault->memory.flags & MLX5_MEMORY_PAGE_FAULT_FLAGS_LAST)
		mlx5_ib_page_fault_resume(dev, pfault, 0);

	mlx5_ib_dbg(
		dev,
		"PAGE FAULT completed %s. token 0x%llx, mkey: 0x%x, va: 0x%llx, byte_count: 0x%x\n",
		pfault->memory.flags & MLX5_MEMORY_PAGE_FAULT_FLAGS_LAST ?
			"" :
			"without resume cmd",
		pfault->token, pfault->memory.mkey, pfault->memory.va,
		pfault->memory.fault_byte_count);

	return;

err:
	if (!IS_ERR(mmkey))
		mlx5r_deref_odp_mkey(mmkey);
	mlx5_ib_page_fault_resume(dev, pfault, 1);
	mlx5_ib_dbg(
		dev,
		"PAGE FAULT error. token 0x%llx, mkey: 0x%x, va: 0x%llx, byte_count: 0x%x, err: %d\n",
		pfault->token, pfault->memory.mkey, pfault->memory.va,
		pfault->memory.fault_byte_count, ret);
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
	case MLX5_PFAULT_SUBTYPE_MEMORY:
		mlx5_ib_mr_memory_pfault_handler(dev, pfault);
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

#define MEMORY_SCHEME_PAGE_FAULT_GRANULARITY 4096
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

		switch (eqe->sub_type) {
		case MLX5_PFAULT_SUBTYPE_RDMA:
			/* RDMA based event */
			pfault->bytes_committed =
				be32_to_cpu(pf_eqe->rdma.bytes_committed);
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
			mlx5_ib_dbg(
				eq->dev,
				"PAGE_FAULT: subtype: 0x%02x, bytes_committed: 0x%06x, type:0x%x, token: 0x%06llx, r_key: 0x%08x\n",
				eqe->sub_type, pfault->bytes_committed,
				pfault->type, pfault->token,
				pfault->rdma.r_key);
			mlx5_ib_dbg(eq->dev,
				    "PAGE_FAULT: rdma_op_len: 0x%08x, rdma_va: 0x%016llx\n",
				    pfault->rdma.rdma_op_len,
				    pfault->rdma.rdma_va);
			break;

		case MLX5_PFAULT_SUBTYPE_WQE:
			/* WQE based event */
			pfault->bytes_committed =
				be32_to_cpu(pf_eqe->wqe.bytes_committed);
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
			mlx5_ib_dbg(
				eq->dev,
				"PAGE_FAULT: subtype: 0x%02x, bytes_committed: 0x%06x, type:0x%x, token: 0x%06llx, wq_num: 0x%06x, wqe_index: 0x%04x\n",
				eqe->sub_type, pfault->bytes_committed,
				pfault->type, pfault->token, pfault->wqe.wq_num,
				pfault->wqe.wqe_index);
			break;

		case MLX5_PFAULT_SUBTYPE_MEMORY:
			/* Memory based event */
			pfault->bytes_committed = 0;
			pfault->token =
				be32_to_cpu(pf_eqe->memory.token31_0) |
				((u64)be16_to_cpu(pf_eqe->memory.token47_32)
				 << 32);
			pfault->memory.va = be64_to_cpu(pf_eqe->memory.va);
			pfault->memory.mkey = be32_to_cpu(pf_eqe->memory.mkey);
			pfault->memory.fault_byte_count = (be32_to_cpu(
				pf_eqe->memory.demand_fault_pages) >> 12) *
				MEMORY_SCHEME_PAGE_FAULT_GRANULARITY;
			pfault->memory.prefetch_before_byte_count =
				be16_to_cpu(
					pf_eqe->memory.pre_demand_fault_pages) *
				MEMORY_SCHEME_PAGE_FAULT_GRANULARITY;
			pfault->memory.prefetch_after_byte_count =
				be16_to_cpu(
					pf_eqe->memory.post_demand_fault_pages) *
				MEMORY_SCHEME_PAGE_FAULT_GRANULARITY;
			pfault->memory.flags = pf_eqe->memory.flags;
			mlx5_ib_dbg(
				eq->dev,
				"PAGE_FAULT: subtype: 0x%02x, token: 0x%06llx, mkey: 0x%06x, fault_byte_count: 0x%06x, va: 0x%016llx, flags: 0x%02x\n",
				eqe->sub_type, pfault->token,
				pfault->memory.mkey,
				pfault->memory.fault_byte_count,
				pfault->memory.va, pfault->memory.flags);
			mlx5_ib_dbg(
				eq->dev,
				"PAGE_FAULT: prefetch size: before: 0x%06x, after 0x%06x\n",
				pfault->memory.prefetch_before_byte_count,
				pfault->memory.prefetch_after_byte_count);
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

static int mlx5_ib_eq_pf_int(struct notifier_block *nb, unsigned long type,
			     void *data)
{
	struct mlx5_ib_pf_eq *eq =
		container_of(nb, struct mlx5_ib_pf_eq, irq_nb);
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

int mlx5r_odp_create_eq(struct mlx5_ib_dev *dev, struct mlx5_ib_pf_eq *eq)
{
	struct mlx5_eq_param param = {};
	int err = 0;

	mutex_lock(&dev->odp_eq_mutex);
	if (eq->core)
		goto unlock;
	INIT_WORK(&eq->work, mlx5_ib_eq_pf_action);
	spin_lock_init(&eq->lock);
	eq->dev = dev;

	eq->pool = mempool_create_kmalloc_pool(MLX5_IB_NUM_PF_DRAIN,
					       sizeof(struct mlx5_pagefault));
	if (!eq->pool) {
		err = -ENOMEM;
		goto unlock;
	}

	eq->wq = alloc_workqueue("mlx5_ib_page_fault",
				 WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM,
				 MLX5_NUM_CMD_EQE);
	if (!eq->wq) {
		err = -ENOMEM;
		goto err_mempool;
	}

	eq->irq_nb.notifier_call = mlx5_ib_eq_pf_int;
	param = (struct mlx5_eq_param) {
		.nent = MLX5_IB_NUM_PF_EQE,
	};
	param.mask[0] = 1ull << MLX5_EVENT_TYPE_PAGE_FAULT;
	eq->core = mlx5_eq_create_generic(dev->mdev, &param);
	if (IS_ERR(eq->core)) {
		err = PTR_ERR(eq->core);
		goto err_wq;
	}
	err = mlx5_eq_enable(dev->mdev, eq->core, &eq->irq_nb);
	if (err) {
		mlx5_ib_err(dev, "failed to enable odp EQ %d\n", err);
		goto err_eq;
	}

	mutex_unlock(&dev->odp_eq_mutex);
	return 0;
err_eq:
	mlx5_eq_destroy_generic(dev->mdev, eq->core);
err_wq:
	eq->core = NULL;
	destroy_workqueue(eq->wq);
err_mempool:
	mempool_destroy(eq->pool);
unlock:
	mutex_unlock(&dev->odp_eq_mutex);
	return err;
}

static int
mlx5_ib_odp_destroy_eq(struct mlx5_ib_dev *dev, struct mlx5_ib_pf_eq *eq)
{
	int err;

	if (!eq->core)
		return 0;
	mlx5_eq_disable(dev->mdev, eq->core, &eq->irq_nb);
	err = mlx5_eq_destroy_generic(dev->mdev, eq->core);
	cancel_work_sync(&eq->work);
	destroy_workqueue(eq->wq);
	mempool_destroy(eq->pool);

	return err;
}

int mlx5_odp_init_mkey_cache(struct mlx5_ib_dev *dev)
{
	struct mlx5r_cache_rb_key rb_key = {
		.access_mode = MLX5_MKC_ACCESS_MODE_KSM,
		.ndescs = mlx5_imr_ksm_entries,
	};
	struct mlx5_cache_ent *ent;

	if (!(dev->odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT))
		return 0;

	ent = mlx5r_cache_create_ent_locked(dev, rb_key, true);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return 0;
}

static const struct ib_device_ops mlx5_ib_dev_odp_ops = {
	.advise_mr = mlx5_ib_advise_mr,
};

int mlx5_ib_odp_init_one(struct mlx5_ib_dev *dev)
{
	internal_fill_odp_caps(dev);

	if (!(dev->odp_caps.general_caps & IB_ODP_SUPPORT))
		return 0;

	ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_odp_ops);

	mutex_init(&dev->odp_eq_mutex);
	return 0;
}

void mlx5_ib_odp_cleanup_one(struct mlx5_ib_dev *dev)
{
	if (!(dev->odp_caps.general_caps & IB_ODP_SUPPORT))
		return;

	mlx5_ib_odp_destroy_eq(dev, &dev->odp_pf_eq);
}

int mlx5_ib_odp_init(void)
{
	mlx5_imr_ksm_entries = BIT_ULL(get_order(TASK_SIZE) -
				       MLX5_IMR_MTT_BITS);

	return 0;
}

struct prefetch_mr_work {
	struct work_struct work;
	u32 pf_flags;
	u32 num_sge;
	struct {
		u64 io_virt;
		struct mlx5_ib_mr *mr;
		size_t length;
	} frags[];
};

static void destroy_prefetch_work(struct prefetch_mr_work *work)
{
	u32 i;

	for (i = 0; i < work->num_sge; ++i)
		mlx5r_deref_odp_mkey(&work->frags[i].mr->mmkey);

	kvfree(work);
}

static struct mlx5_ib_mr *
get_prefetchable_mr(struct ib_pd *pd, enum ib_uverbs_advise_mr_advice advice,
		    u32 lkey)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr = NULL;
	struct mlx5_ib_mkey *mmkey;

	xa_lock(&dev->odp_mkeys);
	mmkey = xa_load(&dev->odp_mkeys, mlx5_base_mkey(lkey));
	if (!mmkey || mmkey->key != lkey) {
		mr = ERR_PTR(-ENOENT);
		goto end;
	}
	if (mmkey->type != MLX5_MKEY_MR) {
		mr = ERR_PTR(-EINVAL);
		goto end;
	}

	mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);

	if (mr->ibmr.pd != pd) {
		mr = ERR_PTR(-EPERM);
		goto end;
	}

	/* prefetch with write-access must be supported by the MR */
	if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE &&
	    !mr->umem->writable) {
		mr = ERR_PTR(-EPERM);
		goto end;
	}

	refcount_inc(&mmkey->usecount);
end:
	xa_unlock(&dev->odp_mkeys);
	return mr;
}

static void mlx5_ib_prefetch_mr_work(struct work_struct *w)
{
	struct prefetch_mr_work *work =
		container_of(w, struct prefetch_mr_work, work);
	u32 bytes_mapped = 0;
	int ret;
	u32 i;

	/* We rely on IB/core that work is executed if we have num_sge != 0 only. */
	WARN_ON(!work->num_sge);
	for (i = 0; i < work->num_sge; ++i) {
		ret = pagefault_mr(work->frags[i].mr, work->frags[i].io_virt,
				   work->frags[i].length, &bytes_mapped,
				   work->pf_flags, false);
		if (ret <= 0)
			continue;
		mlx5_update_odp_stats(work->frags[i].mr, prefetch, ret);
	}

	destroy_prefetch_work(work);
}

static int init_prefetch_work(struct ib_pd *pd,
			       enum ib_uverbs_advise_mr_advice advice,
			       u32 pf_flags, struct prefetch_mr_work *work,
			       struct ib_sge *sg_list, u32 num_sge)
{
	u32 i;

	INIT_WORK(&work->work, mlx5_ib_prefetch_mr_work);
	work->pf_flags = pf_flags;

	for (i = 0; i < num_sge; ++i) {
		struct mlx5_ib_mr *mr;

		mr = get_prefetchable_mr(pd, advice, sg_list[i].lkey);
		if (IS_ERR(mr)) {
			work->num_sge = i;
			return PTR_ERR(mr);
		}
		work->frags[i].io_virt = sg_list[i].addr;
		work->frags[i].length = sg_list[i].length;
		work->frags[i].mr = mr;
	}
	work->num_sge = num_sge;
	return 0;
}

static int mlx5_ib_prefetch_sg_list(struct ib_pd *pd,
				    enum ib_uverbs_advise_mr_advice advice,
				    u32 pf_flags, struct ib_sge *sg_list,
				    u32 num_sge)
{
	u32 bytes_mapped = 0;
	int ret = 0;
	u32 i;

	for (i = 0; i < num_sge; ++i) {
		struct mlx5_ib_mr *mr;

		mr = get_prefetchable_mr(pd, advice, sg_list[i].lkey);
		if (IS_ERR(mr))
			return PTR_ERR(mr);
		ret = pagefault_mr(mr, sg_list[i].addr, sg_list[i].length,
				   &bytes_mapped, pf_flags, false);
		if (ret < 0) {
			mlx5r_deref_odp_mkey(&mr->mmkey);
			return ret;
		}
		mlx5_update_odp_stats(mr, prefetch, ret);
		mlx5r_deref_odp_mkey(&mr->mmkey);
	}

	return 0;
}

int mlx5_ib_advise_mr_prefetch(struct ib_pd *pd,
			       enum ib_uverbs_advise_mr_advice advice,
			       u32 flags, struct ib_sge *sg_list, u32 num_sge)
{
	u32 pf_flags = 0;
	struct prefetch_mr_work *work;
	int rc;

	if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH)
		pf_flags |= MLX5_PF_FLAGS_DOWNGRADE;

	if (advice == IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT)
		pf_flags |= MLX5_PF_FLAGS_SNAPSHOT;

	if (flags & IB_UVERBS_ADVISE_MR_FLAG_FLUSH)
		return mlx5_ib_prefetch_sg_list(pd, advice, pf_flags, sg_list,
						num_sge);

	work = kvzalloc(struct_size(work, frags, num_sge), GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	rc = init_prefetch_work(pd, advice, pf_flags, work, sg_list, num_sge);
	if (rc) {
		destroy_prefetch_work(work);
		return rc;
	}
	queue_work(system_unbound_wq, &work->work);
	return 0;
}
