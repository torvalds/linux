/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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

#include <config.h>

#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "mlx4.h"
#include "doorbell.h"
#include "wqe.h"

static const uint32_t mlx4_ib_opcode[] = {
	[IBV_WR_SEND]			= MLX4_OPCODE_SEND,
	[IBV_WR_SEND_WITH_IMM]		= MLX4_OPCODE_SEND_IMM,
	[IBV_WR_RDMA_WRITE]		= MLX4_OPCODE_RDMA_WRITE,
	[IBV_WR_RDMA_WRITE_WITH_IMM]	= MLX4_OPCODE_RDMA_WRITE_IMM,
	[IBV_WR_RDMA_READ]		= MLX4_OPCODE_RDMA_READ,
	[IBV_WR_ATOMIC_CMP_AND_SWP]	= MLX4_OPCODE_ATOMIC_CS,
	[IBV_WR_ATOMIC_FETCH_AND_ADD]	= MLX4_OPCODE_ATOMIC_FA,
	[IBV_WR_LOCAL_INV]		= MLX4_OPCODE_LOCAL_INVAL,
	[IBV_WR_BIND_MW]		= MLX4_OPCODE_BIND_MW,
	[IBV_WR_SEND_WITH_INV]		= MLX4_OPCODE_SEND_INVAL,
};

static void *get_recv_wqe(struct mlx4_qp *qp, int n)
{
	return qp->buf.buf + qp->rq.offset + (n << qp->rq.wqe_shift);
}

static void *get_send_wqe(struct mlx4_qp *qp, int n)
{
	return qp->buf.buf + qp->sq.offset + (n << qp->sq.wqe_shift);
}

/*
 * Stamp a SQ WQE so that it is invalid if prefetched by marking the
 * first four bytes of every 64 byte chunk with 0xffffffff, except for
 * the very first chunk of the WQE.
 */
static void stamp_send_wqe(struct mlx4_qp *qp, int n)
{
	uint32_t *wqe = get_send_wqe(qp, n);
	int i;
	int ds = (((struct mlx4_wqe_ctrl_seg *)wqe)->fence_size & 0x3f) << 2;

	for (i = 16; i < ds; i += 16)
		wqe[i] = 0xffffffff;
}

void mlx4_init_qp_indices(struct mlx4_qp *qp)
{
	qp->sq.head	 = 0;
	qp->sq.tail	 = 0;
	qp->rq.head	 = 0;
	qp->rq.tail	 = 0;
}

void mlx4_qp_init_sq_ownership(struct mlx4_qp *qp)
{
	struct mlx4_wqe_ctrl_seg *ctrl;
	int i;

	for (i = 0; i < qp->sq.wqe_cnt; ++i) {
		ctrl = get_send_wqe(qp, i);
		ctrl->owner_opcode = htobe32(1 << 31);
		ctrl->fence_size = 1 << (qp->sq.wqe_shift - 4);

		stamp_send_wqe(qp, i);
	}
}

static int wq_overflow(struct mlx4_wq *wq, int nreq, struct mlx4_cq *cq)
{
	unsigned cur;

	cur = wq->head - wq->tail;
	if (cur + nreq < wq->max_post)
		return 0;

	pthread_spin_lock(&cq->lock);
	cur = wq->head - wq->tail;
	pthread_spin_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}

static void set_bind_seg(struct mlx4_wqe_bind_seg *bseg, struct ibv_send_wr *wr)
{
	int acc = wr->bind_mw.bind_info.mw_access_flags;
	bseg->flags1 = 0;
	if (acc & IBV_ACCESS_REMOTE_ATOMIC)
		bseg->flags1 |= htobe32(MLX4_WQE_MW_ATOMIC);
	if (acc & IBV_ACCESS_REMOTE_WRITE)
		bseg->flags1 |= htobe32(MLX4_WQE_MW_REMOTE_WRITE);
	if (acc & IBV_ACCESS_REMOTE_READ)
		bseg->flags1 |= htobe32(MLX4_WQE_MW_REMOTE_READ);

	bseg->flags2 = 0;
	if (((struct ibv_mw *)(wr->bind_mw.mw))->type == IBV_MW_TYPE_2)
		bseg->flags2 |= htobe32(MLX4_WQE_BIND_TYPE_2);
	if (acc & IBV_ACCESS_ZERO_BASED)
		bseg->flags2 |= htobe32(MLX4_WQE_BIND_ZERO_BASED);

	bseg->new_rkey = htobe32(wr->bind_mw.rkey);
	bseg->lkey = htobe32(wr->bind_mw.bind_info.mr->lkey);
	bseg->addr = htobe64((uint64_t) wr->bind_mw.bind_info.addr);
	bseg->length = htobe64(wr->bind_mw.bind_info.length);
}

static inline void set_local_inv_seg(struct mlx4_wqe_local_inval_seg *iseg,
		uint32_t rkey)
{
	iseg->mem_key	= htobe32(rkey);

	iseg->reserved1    = 0;
	iseg->reserved2    = 0;
	iseg->reserved3[0] = 0;
	iseg->reserved3[1] = 0;
}

static inline void set_raddr_seg(struct mlx4_wqe_raddr_seg *rseg,
				 uint64_t remote_addr, uint32_t rkey)
{
	rseg->raddr    = htobe64(remote_addr);
	rseg->rkey     = htobe32(rkey);
	rseg->reserved = 0;
}

static void set_atomic_seg(struct mlx4_wqe_atomic_seg *aseg, struct ibv_send_wr *wr)
{
	if (wr->opcode == IBV_WR_ATOMIC_CMP_AND_SWP) {
		aseg->swap_add = htobe64(wr->wr.atomic.swap);
		aseg->compare  = htobe64(wr->wr.atomic.compare_add);
	} else {
		aseg->swap_add = htobe64(wr->wr.atomic.compare_add);
		aseg->compare  = 0;
	}

}

static void set_datagram_seg(struct mlx4_wqe_datagram_seg *dseg,
			     struct ibv_send_wr *wr)
{
	memcpy(dseg->av, &to_mah(wr->wr.ud.ah)->av, sizeof (struct mlx4_av));
	dseg->dqpn = htobe32(wr->wr.ud.remote_qpn);
	dseg->qkey = htobe32(wr->wr.ud.remote_qkey);
	dseg->vlan = htobe16(to_mah(wr->wr.ud.ah)->vlan);
	memcpy(dseg->mac, to_mah(wr->wr.ud.ah)->mac, 6);
}

static void __set_data_seg(struct mlx4_wqe_data_seg *dseg, struct ibv_sge *sg)
{
	dseg->byte_count = htobe32(sg->length);
	dseg->lkey       = htobe32(sg->lkey);
	dseg->addr       = htobe64(sg->addr);
}

static void set_data_seg(struct mlx4_wqe_data_seg *dseg, struct ibv_sge *sg)
{
	dseg->lkey       = htobe32(sg->lkey);
	dseg->addr       = htobe64(sg->addr);

	/*
	 * Need a barrier here before writing the byte_count field to
	 * make sure that all the data is visible before the
	 * byte_count field is set.  Otherwise, if the segment begins
	 * a new cacheline, the HCA prefetcher could grab the 64-byte
	 * chunk and get a valid (!= * 0xffffffff) byte count but
	 * stale data, and end up sending the wrong data.
	 */
	udma_to_device_barrier();

	if (likely(sg->length))
		dseg->byte_count = htobe32(sg->length);
	else
		dseg->byte_count = htobe32(0x80000000);
}

int mlx4_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr)
{
	struct mlx4_context *ctx;
	struct mlx4_qp *qp = to_mqp(ibqp);
	void *wqe;
	struct mlx4_wqe_ctrl_seg *ctrl = NULL;
	int ind;
	int nreq;
	int inl = 0;
	int ret = 0;
	int size = 0;
	int i;

	pthread_spin_lock(&qp->sq.lock);

	/* XXX check that state is OK to post send */

	ind = qp->sq.head;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (wq_overflow(&qp->sq, nreq, to_mcq(ibqp->send_cq))) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (wr->num_sge > qp->sq.max_gs) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (wr->opcode >= sizeof mlx4_ib_opcode / sizeof mlx4_ib_opcode[0]) {
			ret = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		ctrl = wqe = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
		qp->sq.wrid[ind & (qp->sq.wqe_cnt - 1)] = wr->wr_id;

		ctrl->srcrb_flags =
			(wr->send_flags & IBV_SEND_SIGNALED ?
			 htobe32(MLX4_WQE_CTRL_CQ_UPDATE) : 0) |
			(wr->send_flags & IBV_SEND_SOLICITED ?
			 htobe32(MLX4_WQE_CTRL_SOLICIT) : 0)   |
			qp->sq_signal_bits;

		if (wr->opcode == IBV_WR_SEND_WITH_IMM ||
		    wr->opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
			ctrl->imm = wr->imm_data;
		else
			ctrl->imm = 0;

		wqe += sizeof *ctrl;
		size = sizeof *ctrl / 16;

		switch (ibqp->qp_type) {
		case IBV_QPT_XRC_SEND:
			ctrl->srcrb_flags |= MLX4_REMOTE_SRQN_FLAGS(wr);
			/* fall through */
		case IBV_QPT_RC:
		case IBV_QPT_UC:
			switch (wr->opcode) {
			case IBV_WR_ATOMIC_CMP_AND_SWP:
			case IBV_WR_ATOMIC_FETCH_AND_ADD:
				set_raddr_seg(wqe, wr->wr.atomic.remote_addr,
					      wr->wr.atomic.rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);

				set_atomic_seg(wqe, wr);
				wqe  += sizeof (struct mlx4_wqe_atomic_seg);
				size += (sizeof (struct mlx4_wqe_raddr_seg) +
					 sizeof (struct mlx4_wqe_atomic_seg)) / 16;

				break;

			case IBV_WR_RDMA_READ:
				inl = 1;
				/* fall through */
			case IBV_WR_RDMA_WRITE:
			case IBV_WR_RDMA_WRITE_WITH_IMM:
				if (!wr->num_sge)
					inl = 1;
				set_raddr_seg(wqe, wr->wr.rdma.remote_addr,
					      wr->wr.rdma.rkey);
				wqe  += sizeof (struct mlx4_wqe_raddr_seg);
				size += sizeof (struct mlx4_wqe_raddr_seg) / 16;

				break;
			case IBV_WR_LOCAL_INV:
				ctrl->srcrb_flags |=
					htobe32(MLX4_WQE_CTRL_STRONG_ORDER);
				set_local_inv_seg(wqe, wr->imm_data);
				wqe  += sizeof
					(struct mlx4_wqe_local_inval_seg);
				size += sizeof
					(struct mlx4_wqe_local_inval_seg) / 16;
				break;
			case IBV_WR_BIND_MW:
				ctrl->srcrb_flags |=
					htobe32(MLX4_WQE_CTRL_STRONG_ORDER);
				set_bind_seg(wqe, wr);
				wqe  += sizeof
					(struct mlx4_wqe_bind_seg);
				size += sizeof
					(struct mlx4_wqe_bind_seg) / 16;
				break;
			case IBV_WR_SEND_WITH_INV:
				ctrl->imm = htobe32(wr->imm_data);
				break;

			default:
				/* No extra segments required for sends */
				break;
			}
			break;

		case IBV_QPT_UD:
			set_datagram_seg(wqe, wr);
			wqe  += sizeof (struct mlx4_wqe_datagram_seg);
			size += sizeof (struct mlx4_wqe_datagram_seg) / 16;

			if (wr->send_flags & IBV_SEND_IP_CSUM) {
				if (!(qp->qp_cap_cache & MLX4_CSUM_SUPPORT_UD_OVER_IB)) {
					ret = EINVAL;
					*bad_wr = wr;
					goto out;
				}
				ctrl->srcrb_flags |= htobe32(MLX4_WQE_CTRL_IP_HDR_CSUM |
							   MLX4_WQE_CTRL_TCP_UDP_CSUM);
			}
			break;

		case IBV_QPT_RAW_PACKET:
			/* For raw eth, the MLX4_WQE_CTRL_SOLICIT flag is used
			 * to indicate that no icrc should be calculated */
			ctrl->srcrb_flags |= htobe32(MLX4_WQE_CTRL_SOLICIT);
			if (wr->send_flags & IBV_SEND_IP_CSUM) {
				if (!(qp->qp_cap_cache & MLX4_CSUM_SUPPORT_RAW_OVER_ETH)) {
					ret = EINVAL;
					*bad_wr = wr;
					goto out;
				}
				ctrl->srcrb_flags |= htobe32(MLX4_WQE_CTRL_IP_HDR_CSUM |
							   MLX4_WQE_CTRL_TCP_UDP_CSUM);
			}
			break;

		default:
			break;
		}

		if (wr->send_flags & IBV_SEND_INLINE && wr->num_sge) {
			struct mlx4_wqe_inline_seg *seg;
			void *addr;
			int len, seg_len;
			int num_seg;
			int off, to_copy;

			inl = 0;

			seg = wqe;
			wqe += sizeof *seg;
			off = ((uintptr_t) wqe) & (MLX4_INLINE_ALIGN - 1);
			num_seg = 0;
			seg_len = 0;

			for (i = 0; i < wr->num_sge; ++i) {
				addr = (void *) (uintptr_t) wr->sg_list[i].addr;
				len  = wr->sg_list[i].length;
				inl += len;

				if (inl > qp->max_inline_data) {
					inl = 0;
					ret = ENOMEM;
					*bad_wr = wr;
					goto out;
				}

				while (len >= MLX4_INLINE_ALIGN - off) {
					to_copy = MLX4_INLINE_ALIGN - off;
					memcpy(wqe, addr, to_copy);
					len -= to_copy;
					wqe += to_copy;
					addr += to_copy;
					seg_len += to_copy;
					udma_to_device_barrier(); /* see comment below */
					seg->byte_count = htobe32(MLX4_INLINE_SEG | seg_len);
					seg_len = 0;
					seg = wqe;
					wqe += sizeof *seg;
					off = sizeof *seg;
					++num_seg;
				}

				memcpy(wqe, addr, len);
				wqe += len;
				seg_len += len;
				off += len;
			}

			if (seg_len) {
				++num_seg;
				/*
				 * Need a barrier here to make sure
				 * all the data is visible before the
				 * byte_count field is set.  Otherwise
				 * the HCA prefetcher could grab the
				 * 64-byte chunk with this inline
				 * segment and get a valid (!=
				 * 0xffffffff) byte count but stale
				 * data, and end up sending the wrong
				 * data.
				 */
				udma_to_device_barrier();
				seg->byte_count = htobe32(MLX4_INLINE_SEG | seg_len);
			}

			size += (inl + num_seg * sizeof * seg + 15) / 16;
		} else {
			struct mlx4_wqe_data_seg *seg = wqe;

			for (i = wr->num_sge - 1; i >= 0 ; --i)
				set_data_seg(seg + i, wr->sg_list + i);

			size += wr->num_sge * (sizeof *seg / 16);
		}

		ctrl->fence_size = (wr->send_flags & IBV_SEND_FENCE ?
				    MLX4_WQE_CTRL_FENCE : 0) | size;

		/*
		 * Make sure descriptor is fully written before
		 * setting ownership bit (because HW can start
		 * executing as soon as we do).
		 */
		udma_to_device_barrier();

		ctrl->owner_opcode = htobe32(mlx4_ib_opcode[wr->opcode]) |
			(ind & qp->sq.wqe_cnt ? htobe32(1 << 31) : 0);

		/*
		 * We can improve latency by not stamping the last
		 * send queue WQE until after ringing the doorbell, so
		 * only stamp here if there are still more WQEs to post.
		 */
		if (wr->next)
			stamp_send_wqe(qp, (ind + qp->sq_spare_wqes) &
				       (qp->sq.wqe_cnt - 1));

		++ind;
	}

out:
	ctx = to_mctx(ibqp->context);

	if (nreq == 1 && inl && size > 1 && size <= ctx->bf_buf_size / 16) {
		ctrl->owner_opcode |= htobe32((qp->sq.head & 0xffff) << 8);

		ctrl->bf_qpn |= qp->doorbell_qpn;
		++qp->sq.head;
		/*
		 * Make sure that descriptor is written to memory
		 * before writing to BlueFlame page.
		 */
		mmio_wc_spinlock(&ctx->bf_lock);

		mlx4_bf_copy(ctx->bf_page + ctx->bf_offset, (unsigned long *) ctrl,
			     align(size * 16, 64));
		/* Flush before toggling bf_offset to be latency oriented */
		mmio_flush_writes();

		ctx->bf_offset ^= ctx->bf_buf_size;

		pthread_spin_unlock(&ctx->bf_lock);
	} else if (nreq) {
		qp->sq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		udma_to_device_barrier();

		mmio_writel((unsigned long)(ctx->uar + MLX4_SEND_DOORBELL),
			    qp->doorbell_qpn);
	}

	if (nreq)
		stamp_send_wqe(qp, (ind + qp->sq_spare_wqes - 1) &
			       (qp->sq.wqe_cnt - 1));

	pthread_spin_unlock(&qp->sq.lock);

	return ret;
}

int mlx4_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		   struct ibv_recv_wr **bad_wr)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	struct mlx4_wqe_data_seg *scat;
	int ret = 0;
	int nreq;
	int ind;
	int i;

	pthread_spin_lock(&qp->rq.lock);

	/* XXX check that state is OK to post receive */

	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (wq_overflow(&qp->rq, nreq, to_mcq(ibqp->recv_cq))) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (wr->num_sge > qp->rq.max_gs) {
			ret = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		scat = get_recv_wqe(qp, ind);

		for (i = 0; i < wr->num_sge; ++i)
			__set_data_seg(scat + i, wr->sg_list + i);

		if (i < qp->rq.max_gs) {
			scat[i].byte_count = 0;
			scat[i].lkey       = htobe32(MLX4_INVALID_LKEY);
			scat[i].addr       = 0;
		}

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);
	}

out:
	if (nreq) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		udma_to_device_barrier();

		*qp->db = htobe32(qp->rq.head & 0xffff);
	}

	pthread_spin_unlock(&qp->rq.lock);

	return ret;
}

static int num_inline_segs(int data, enum ibv_qp_type type)
{
	/*
	 * Inline data segments are not allowed to cross 64 byte
	 * boundaries.  For UD QPs, the data segments always start
	 * aligned to 64 bytes (16 byte control segment + 48 byte
	 * datagram segment); for other QPs, there will be a 16 byte
	 * control segment and possibly a 16 byte remote address
	 * segment, so in the worst case there will be only 32 bytes
	 * available for the first data segment.
	 */
	if (type == IBV_QPT_UD)
		data += (sizeof (struct mlx4_wqe_ctrl_seg) +
			 sizeof (struct mlx4_wqe_datagram_seg)) %
			MLX4_INLINE_ALIGN;
	else
		data += (sizeof (struct mlx4_wqe_ctrl_seg) +
			 sizeof (struct mlx4_wqe_raddr_seg)) %
			MLX4_INLINE_ALIGN;

	return (data + MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg) - 1) /
		(MLX4_INLINE_ALIGN - sizeof (struct mlx4_wqe_inline_seg));
}

void mlx4_calc_sq_wqe_size(struct ibv_qp_cap *cap, enum ibv_qp_type type,
			   struct mlx4_qp *qp)
{
	int size;
	int max_sq_sge;

	max_sq_sge	 = align(cap->max_inline_data +
				 num_inline_segs(cap->max_inline_data, type) *
				 sizeof (struct mlx4_wqe_inline_seg),
				 sizeof (struct mlx4_wqe_data_seg)) /
		sizeof (struct mlx4_wqe_data_seg);
	if (max_sq_sge < cap->max_send_sge)
		max_sq_sge = cap->max_send_sge;

	size = max_sq_sge * sizeof (struct mlx4_wqe_data_seg);
	switch (type) {
	case IBV_QPT_UD:
		size += sizeof (struct mlx4_wqe_datagram_seg);
		break;

	case IBV_QPT_UC:
		size += sizeof (struct mlx4_wqe_raddr_seg);
		break;

	case IBV_QPT_XRC_SEND:
	case IBV_QPT_RC:
		size += sizeof (struct mlx4_wqe_raddr_seg);
		/*
		 * An atomic op will require an atomic segment, a
		 * remote address segment and one scatter entry.
		 */
		if (size < (sizeof (struct mlx4_wqe_atomic_seg) +
			    sizeof (struct mlx4_wqe_raddr_seg) +
			    sizeof (struct mlx4_wqe_data_seg)))
			size = (sizeof (struct mlx4_wqe_atomic_seg) +
				sizeof (struct mlx4_wqe_raddr_seg) +
				sizeof (struct mlx4_wqe_data_seg));
		break;

	default:
		break;
	}

	/* Make sure that we have enough space for a bind request */
	if (size < sizeof (struct mlx4_wqe_bind_seg))
		size = sizeof (struct mlx4_wqe_bind_seg);

	size += sizeof (struct mlx4_wqe_ctrl_seg);

	for (qp->sq.wqe_shift = 6; 1 << qp->sq.wqe_shift < size;
	     qp->sq.wqe_shift++)
		; /* nothing */
}

int mlx4_alloc_qp_buf(struct ibv_context *context, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type, struct mlx4_qp *qp)
{
	qp->rq.max_gs	 = cap->max_recv_sge;

	if (qp->sq.wqe_cnt) {
		qp->sq.wrid = malloc(qp->sq.wqe_cnt * sizeof (uint64_t));
		if (!qp->sq.wrid)
			return -1;
	}

	if (qp->rq.wqe_cnt) {
		qp->rq.wrid = malloc(qp->rq.wqe_cnt * sizeof (uint64_t));
		if (!qp->rq.wrid) {
			free(qp->sq.wrid);
			return -1;
		}
	}

	for (qp->rq.wqe_shift = 4;
	     1 << qp->rq.wqe_shift < qp->rq.max_gs * sizeof (struct mlx4_wqe_data_seg);
	     qp->rq.wqe_shift++)
		; /* nothing */

	qp->buf_size = (qp->rq.wqe_cnt << qp->rq.wqe_shift) +
		(qp->sq.wqe_cnt << qp->sq.wqe_shift);
	if (qp->rq.wqe_shift > qp->sq.wqe_shift) {
		qp->rq.offset = 0;
		qp->sq.offset = qp->rq.wqe_cnt << qp->rq.wqe_shift;
	} else {
		qp->rq.offset = qp->sq.wqe_cnt << qp->sq.wqe_shift;
		qp->sq.offset = 0;
	}

	if (qp->buf_size) {
		if (mlx4_alloc_buf(&qp->buf,
				   align(qp->buf_size, to_mdev(context->device)->page_size),
				   to_mdev(context->device)->page_size)) {
			free(qp->sq.wrid);
			free(qp->rq.wrid);
			return -1;
		}

		memset(qp->buf.buf, 0, qp->buf_size);
	} else {
		qp->buf.buf = NULL;
	}

	return 0;
}

void mlx4_set_sq_sizes(struct mlx4_qp *qp, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type)
{
	int wqe_size;

	wqe_size = (1 << qp->sq.wqe_shift) - sizeof (struct mlx4_wqe_ctrl_seg);
	switch (type) {
	case IBV_QPT_UD:
		wqe_size -= sizeof (struct mlx4_wqe_datagram_seg);
		break;

	case IBV_QPT_XRC_SEND:
	case IBV_QPT_UC:
	case IBV_QPT_RC:
		wqe_size -= sizeof (struct mlx4_wqe_raddr_seg);
		break;

	default:
		break;
	}

	qp->sq.max_gs	     = wqe_size / sizeof (struct mlx4_wqe_data_seg);
	cap->max_send_sge    = qp->sq.max_gs;
	qp->sq.max_post	     = qp->sq.wqe_cnt - qp->sq_spare_wqes;
	cap->max_send_wr     = qp->sq.max_post;

	/*
	 * Inline data segments can't cross a 64 byte boundary.  So
	 * subtract off one segment header for each 64-byte chunk,
	 * taking into account the fact that wqe_size will be 32 mod
	 * 64 for non-UD QPs.
	 */
	qp->max_inline_data  = wqe_size -
		sizeof (struct mlx4_wqe_inline_seg) *
		(align(wqe_size, MLX4_INLINE_ALIGN) / MLX4_INLINE_ALIGN);
	cap->max_inline_data = qp->max_inline_data;
}

struct mlx4_qp *mlx4_find_qp(struct mlx4_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (ctx->qp_table[tind].refcnt)
		return ctx->qp_table[tind].table[qpn & ctx->qp_table_mask];
	else
		return NULL;
}

int mlx4_store_qp(struct mlx4_context *ctx, uint32_t qpn, struct mlx4_qp *qp)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (!ctx->qp_table[tind].refcnt) {
		ctx->qp_table[tind].table = calloc(ctx->qp_table_mask + 1,
						   sizeof (struct mlx4_qp *));
		if (!ctx->qp_table[tind].table)
			return -1;
	}

	++ctx->qp_table[tind].refcnt;
	ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = qp;
	return 0;
}

void mlx4_clear_qp(struct mlx4_context *ctx, uint32_t qpn)
{
	int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

	if (!--ctx->qp_table[tind].refcnt)
		free(ctx->qp_table[tind].table);
	else
		ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = NULL;
}
