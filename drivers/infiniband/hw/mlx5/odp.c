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

#include "mlx5_ib.h"

#define MAX_PREFETCH_LEN (4*1024*1024U)

/* Timeout in ms to wait for an active mmu notifier to complete when handling
 * a pagefault. */
#define MMU_NOTIFIER_TIMEOUT 1000

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

	for (addr = start; addr < end; addr += (u64)umem->page_size) {
		idx = (addr - ib_umem_start(umem)) / PAGE_SIZE;
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
						   idx - blk_start_idx,
						   PAGE_SHIFT,
						   MLX5_IB_UPD_XLT_ZAP |
						   MLX5_IB_UPD_XLT_ATOMIC);
				in_block = 0;
			}
		}
	}
	if (in_block)
		mlx5_ib_update_xlt(mr, blk_start_idx,
				   idx - blk_start_idx + 1,
				   PAGE_SHIFT,
				   MLX5_IB_UPD_XLT_ZAP |
				   MLX5_IB_UPD_XLT_ATOMIC);
	/*
	 * We are now sure that the device will not access the
	 * memory. We can safely unmap it, and mark it as dirty if
	 * needed.
	 */

	ib_umem_odp_unmap_dma_pages(umem, start, end);
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

	return;
}

static struct mlx5_ib_mr *mlx5_ib_odp_find_mr_lkey(struct mlx5_ib_dev *dev,
						   u32 key)
{
	u32 base_key = mlx5_base_mkey(key);
	struct mlx5_core_mkey *mmkey = __mlx5_mr_lookup(dev->mdev, base_key);
	struct mlx5_ib_mr *mr;

	if (!mmkey || mmkey->key != key || mmkey->type != MLX5_MKEY_MR)
		return NULL;

	mr = container_of(mmkey, struct mlx5_ib_mr, mmkey);

	if (!mr->live)
		return NULL;

	return container_of(mmkey, struct mlx5_ib_mr, mmkey);
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

/*
 * Handle a single data segment in a page-fault WQE or RDMA region.
 *
 * Returns number of pages retrieved on success. The caller may continue to
 * the next data segment.
 * Can return the following error codes:
 * -EAGAIN to designate a temporary error. The caller will abort handling the
 *  page fault and resolve it.
 * -EFAULT when there's an error mapping the requested pages. The caller will
 *  abort the page fault handling.
 */
static int pagefault_single_data_segment(struct mlx5_ib_dev *mib_dev,
					 u32 key, u64 io_virt, size_t bcnt,
					 u32 *bytes_committed,
					 u32 *bytes_mapped)
{
	int srcu_key;
	unsigned int current_seq;
	u64 start_idx;
	int npages = 0, ret = 0;
	struct mlx5_ib_mr *mr;
	u64 access_mask = ODP_READ_ALLOWED_BIT;

	srcu_key = srcu_read_lock(&mib_dev->mr_srcu);
	mr = mlx5_ib_odp_find_mr_lkey(mib_dev, key);
	/*
	 * If we didn't find the MR, it means the MR was closed while we were
	 * handling the ODP event. In this case we return -EFAULT so that the
	 * QP will be closed.
	 */
	if (!mr || !mr->ibmr.pd) {
		pr_err("Failed to find relevant mr for lkey=0x%06x, probably the MR was destroyed\n",
		       key);
		ret = -EFAULT;
		goto srcu_unlock;
	}
	if (!mr->umem->odp_data) {
		pr_debug("skipping non ODP MR (lkey=0x%06x) in page fault handler.\n",
			 key);
		if (bytes_mapped)
			*bytes_mapped +=
				(bcnt - *bytes_committed);
		goto srcu_unlock;
	}

	current_seq = ACCESS_ONCE(mr->umem->odp_data->notifiers_seq);
	/*
	 * Ensure the sequence number is valid for some time before we call
	 * gup.
	 */
	smp_rmb();

	/*
	 * Avoid branches - this code will perform correctly
	 * in all iterations (in iteration 2 and above,
	 * bytes_committed == 0).
	 */
	io_virt += *bytes_committed;
	bcnt -= *bytes_committed;

	start_idx = (io_virt - (mr->mmkey.iova & PAGE_MASK)) >> PAGE_SHIFT;

	if (mr->umem->writable)
		access_mask |= ODP_WRITE_ALLOWED_BIT;
	npages = ib_umem_odp_map_dma_pages(mr->umem, io_virt, bcnt,
					   access_mask, current_seq);
	if (npages < 0) {
		ret = npages;
		goto srcu_unlock;
	}

	if (npages > 0) {
		mutex_lock(&mr->umem->odp_data->umem_mutex);
		if (!ib_umem_mmu_notifier_retry(mr->umem, current_seq)) {
			/*
			 * No need to check whether the MTTs really belong to
			 * this MR, since ib_umem_odp_map_dma_pages already
			 * checks this.
			 */
			ret = mlx5_ib_update_xlt(mr, start_idx, npages,
						 PAGE_SHIFT,
						 MLX5_IB_UPD_XLT_ATOMIC);
		} else {
			ret = -EAGAIN;
		}
		mutex_unlock(&mr->umem->odp_data->umem_mutex);
		if (ret < 0) {
			if (ret != -EAGAIN)
				pr_err("Failed to update mkey page tables\n");
			goto srcu_unlock;
		}

		if (bytes_mapped) {
			u32 new_mappings = npages * PAGE_SIZE -
				(io_virt - round_down(io_virt, PAGE_SIZE));
			*bytes_mapped += min_t(u32, new_mappings, bcnt);
		}
	}

srcu_unlock:
	if (ret == -EAGAIN) {
		if (!mr->umem->odp_data->dying) {
			struct ib_umem_odp *odp_data = mr->umem->odp_data;
			unsigned long timeout =
				msecs_to_jiffies(MMU_NOTIFIER_TIMEOUT);

			if (!wait_for_completion_timeout(
					&odp_data->notifier_completion,
					timeout)) {
				pr_warn("timeout waiting for mmu notifier completion\n");
			}
		} else {
			/* The MR is being killed, kill the QP as well. */
			ret = -EFAULT;
		}
	}
	srcu_read_unlock(&mib_dev->mr_srcu, srcu_key);
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

	if (unlikely(opcode >= sizeof(mlx5_ib_odp_opcode_cap) /
	    sizeof(mlx5_ib_odp_opcode_cap[0]) ||
	    !(transport_caps & mlx5_ib_odp_opcode_cap[opcode]))) {
		mlx5_ib_err(dev, "ODP fault on QP of an unsupported opcode 0x%x\n",
			    opcode);
		return -EFAULT;
	}

	if (qp->ibqp.qp_type != IB_QPT_RC) {
		av = *wqe;
		if (av->dqp_dct & be32_to_cpu(MLX5_WQE_AV_EXT))
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
		if (ret != -ENOENT)
			mlx5_ib_err(dev, "Error getting user pages for page fault. Error: %d\n",
				    ret);
		goto resolve_page_fault;
	}

	resume_with_error = 0;
resolve_page_fault:
	mlx5_ib_page_fault_resume(dev, pfault, resume_with_error);
	mlx5_ib_dbg(dev, "PAGE FAULT completed. QP 0x%x resume_with_error=%d, type: 0x%x\n",
		    pfault->token, resume_with_error,
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
			mlx5_ib_warn(dev, "PAGE FAULT error %d. QP 0x%x, type: 0x%x\n",
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
		if (ret < 0) {
			mlx5_ib_warn(dev, "Prefetch failed. ret: %d, QP 0x%x, address: 0x%.16llx, length = 0x%.16x\n",
				     ret, pfault->token, address,
				     prefetch_len);
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

int mlx5_ib_odp_init_one(struct mlx5_ib_dev *ibdev)
{
	int ret;

	ret = init_srcu_struct(&ibdev->mr_srcu);
	if (ret)
		return ret;

	return 0;
}

void mlx5_ib_odp_remove_one(struct mlx5_ib_dev *ibdev)
{
	cleanup_srcu_struct(&ibdev->mr_srcu);
}

