/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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
#include <stdio.h>

#include "mlx5.h"
#include "doorbell.h"
#include "wqe.h"

#define MLX5_ATOMIC_SIZE 8

static const uint32_t mlx5_ib_opcode[] = {
	[IBV_WR_SEND]			= MLX5_OPCODE_SEND,
	[IBV_WR_SEND_WITH_INV]		= MLX5_OPCODE_SEND_INVAL,
	[IBV_WR_SEND_WITH_IMM]		= MLX5_OPCODE_SEND_IMM,
	[IBV_WR_RDMA_WRITE]		= MLX5_OPCODE_RDMA_WRITE,
	[IBV_WR_RDMA_WRITE_WITH_IMM]	= MLX5_OPCODE_RDMA_WRITE_IMM,
	[IBV_WR_RDMA_READ]		= MLX5_OPCODE_RDMA_READ,
	[IBV_WR_ATOMIC_CMP_AND_SWP]	= MLX5_OPCODE_ATOMIC_CS,
	[IBV_WR_ATOMIC_FETCH_AND_ADD]	= MLX5_OPCODE_ATOMIC_FA,
	[IBV_WR_BIND_MW]		= MLX5_OPCODE_UMR,
	[IBV_WR_LOCAL_INV]		= MLX5_OPCODE_UMR,
	[IBV_WR_TSO]			= MLX5_OPCODE_TSO,
};

static void *get_recv_wqe(struct mlx5_qp *qp, int n)
{
	return qp->buf.buf + qp->rq.offset + (n << qp->rq.wqe_shift);
}

static void *get_wq_recv_wqe(struct mlx5_rwq *rwq, int n)
{
	return rwq->pbuff  + (n << rwq->rq.wqe_shift);
}

static int copy_to_scat(struct mlx5_wqe_data_seg *scat, void *buf, int *size,
			 int max)
{
	int copy;
	int i;

	if (unlikely(!(*size)))
		return IBV_WC_SUCCESS;

	for (i = 0; i < max; ++i) {
		copy = min_t(long, *size, be32toh(scat->byte_count));
		memcpy((void *)(unsigned long)be64toh(scat->addr), buf, copy);
		*size -= copy;
		if (*size == 0)
			return IBV_WC_SUCCESS;

		buf += copy;
		++scat;
	}
	return IBV_WC_LOC_LEN_ERR;
}

int mlx5_copy_to_recv_wqe(struct mlx5_qp *qp, int idx, void *buf, int size)
{
	struct mlx5_wqe_data_seg *scat;
	int max = 1 << (qp->rq.wqe_shift - 4);

	scat = get_recv_wqe(qp, idx);
	if (unlikely(qp->wq_sig))
		++scat;

	return copy_to_scat(scat, buf, &size, max);
}

int mlx5_copy_to_send_wqe(struct mlx5_qp *qp, int idx, void *buf, int size)
{
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_wqe_data_seg *scat;
	void *p;
	int max;

	idx &= (qp->sq.wqe_cnt - 1);
	ctrl = mlx5_get_send_wqe(qp, idx);
	if (qp->ibv_qp->qp_type != IBV_QPT_RC) {
		fprintf(stderr, "scatter to CQE is supported only for RC QPs\n");
		return IBV_WC_GENERAL_ERR;
	}
	p = ctrl + 1;

	switch (be32toh(ctrl->opmod_idx_opcode) & 0xff) {
	case MLX5_OPCODE_RDMA_READ:
		p = p + sizeof(struct mlx5_wqe_raddr_seg);
		break;

	case MLX5_OPCODE_ATOMIC_CS:
	case MLX5_OPCODE_ATOMIC_FA:
		p = p + sizeof(struct mlx5_wqe_raddr_seg) +
			sizeof(struct mlx5_wqe_atomic_seg);
		break;

	default:
		fprintf(stderr, "scatter to CQE for opcode %d\n",
			be32toh(ctrl->opmod_idx_opcode) & 0xff);
		return IBV_WC_REM_INV_REQ_ERR;
	}

	scat = p;
	max = (be32toh(ctrl->qpn_ds) & 0x3F) - (((void *)scat - (void *)ctrl) >> 4);
	if (unlikely((void *)(scat + max) > qp->sq.qend)) {
		int tmp = ((void *)qp->sq.qend - (void *)scat) >> 4;
		int orig_size = size;

		if (copy_to_scat(scat, buf, &size, tmp) == IBV_WC_SUCCESS)
			return IBV_WC_SUCCESS;
		max = max - tmp;
		buf += orig_size - size;
		scat = mlx5_get_send_wqe(qp, 0);
	}

	return copy_to_scat(scat, buf, &size, max);
}

void *mlx5_get_send_wqe(struct mlx5_qp *qp, int n)
{
	return qp->sq_start + (n << MLX5_SEND_WQE_SHIFT);
}

void mlx5_init_rwq_indices(struct mlx5_rwq *rwq)
{
	rwq->rq.head	 = 0;
	rwq->rq.tail	 = 0;
}

void mlx5_init_qp_indices(struct mlx5_qp *qp)
{
	qp->sq.head	 = 0;
	qp->sq.tail	 = 0;
	qp->rq.head	 = 0;
	qp->rq.tail	 = 0;
	qp->sq.cur_post  = 0;
}

static int mlx5_wq_overflow(struct mlx5_wq *wq, int nreq, struct mlx5_cq *cq)
{
	unsigned cur;

	cur = wq->head - wq->tail;
	if (cur + nreq < wq->max_post)
		return 0;

	mlx5_spin_lock(&cq->lock);
	cur = wq->head - wq->tail;
	mlx5_spin_unlock(&cq->lock);

	return cur + nreq >= wq->max_post;
}

static inline void set_raddr_seg(struct mlx5_wqe_raddr_seg *rseg,
				 uint64_t remote_addr, uint32_t rkey)
{
	rseg->raddr    = htobe64(remote_addr);
	rseg->rkey     = htobe32(rkey);
	rseg->reserved = 0;
}

static void set_atomic_seg(struct mlx5_wqe_atomic_seg *aseg,
			   enum ibv_wr_opcode   opcode,
			   uint64_t swap,
			   uint64_t compare_add)
{
	if (opcode == IBV_WR_ATOMIC_CMP_AND_SWP) {
		aseg->swap_add = htobe64(swap);
		aseg->compare  = htobe64(compare_add);
	} else {
		aseg->swap_add = htobe64(compare_add);
	}
}

static void set_datagram_seg(struct mlx5_wqe_datagram_seg *dseg,
			     struct ibv_send_wr *wr)
{
	memcpy(&dseg->av, &to_mah(wr->wr.ud.ah)->av, sizeof dseg->av);
	dseg->av.dqp_dct = htobe32(wr->wr.ud.remote_qpn | MLX5_EXTENDED_UD_AV);
	dseg->av.key.qkey.qkey = htobe32(wr->wr.ud.remote_qkey);
}

static void set_data_ptr_seg(struct mlx5_wqe_data_seg *dseg, struct ibv_sge *sg,
			     int offset)
{
	dseg->byte_count = htobe32(sg->length - offset);
	dseg->lkey       = htobe32(sg->lkey);
	dseg->addr       = htobe64(sg->addr + offset);
}

static void set_data_ptr_seg_atomic(struct mlx5_wqe_data_seg *dseg,
				    struct ibv_sge *sg)
{
	dseg->byte_count = htobe32(MLX5_ATOMIC_SIZE);
	dseg->lkey       = htobe32(sg->lkey);
	dseg->addr       = htobe64(sg->addr);
}

/*
 * Avoid using memcpy() to copy to BlueFlame page, since memcpy()
 * implementations may use move-string-buffer assembler instructions,
 * which do not guarantee order of copying.
 */
static void mlx5_bf_copy(unsigned long long *dst, unsigned long long *src,
			 unsigned bytecnt, struct mlx5_qp *qp)
{
	while (bytecnt > 0) {
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		bytecnt -= 8 * sizeof(unsigned long long);
		if (unlikely(src == qp->sq.qend))
			src = qp->sq_start;
	}
}

static uint32_t send_ieth(struct ibv_send_wr *wr)
{
	switch (wr->opcode) {
	case IBV_WR_SEND_WITH_IMM:
	case IBV_WR_RDMA_WRITE_WITH_IMM:
		return wr->imm_data;
	case IBV_WR_SEND_WITH_INV:
		return htobe32(wr->imm_data);
	default:
		return 0;
	}
}

static int set_data_inl_seg(struct mlx5_qp *qp, struct ibv_send_wr *wr,
			    void *wqe, int *sz,
			    struct mlx5_sg_copy_ptr *sg_copy_ptr)
{
	struct mlx5_wqe_inline_seg *seg;
	void *addr;
	int len;
	int i;
	int inl = 0;
	void *qend = qp->sq.qend;
	int copy;
	int offset = sg_copy_ptr->offset;

	seg = wqe;
	wqe += sizeof *seg;
	for (i = sg_copy_ptr->index; i < wr->num_sge; ++i) {
		addr = (void *) (unsigned long)(wr->sg_list[i].addr + offset);
		len  = wr->sg_list[i].length - offset;
		inl += len;
		offset = 0;

		if (unlikely(inl > qp->max_inline_data))
			return ENOMEM;

		if (unlikely(wqe + len > qend)) {
			copy = qend - wqe;
			memcpy(wqe, addr, copy);
			addr += copy;
			len -= copy;
			wqe = mlx5_get_send_wqe(qp, 0);
		}
		memcpy(wqe, addr, len);
		wqe += len;
	}

	if (likely(inl)) {
		seg->byte_count = htobe32(inl | MLX5_INLINE_SEG);
		*sz = align(inl + sizeof seg->byte_count, 16) / 16;
	} else
		*sz = 0;

	return 0;
}

static uint8_t wq_sig(struct mlx5_wqe_ctrl_seg *ctrl)
{
	return calc_sig(ctrl, be32toh(ctrl->qpn_ds));
}

#ifdef MLX5_DEBUG
static void dump_wqe(FILE *fp, int idx, int size_16, struct mlx5_qp *qp)
{
	uint32_t *p = NULL;
	int i, j;
	int tidx = idx;

	fprintf(fp, "dump wqe at %p\n", mlx5_get_send_wqe(qp, tidx));
	for (i = 0, j = 0; i < size_16 * 4; i += 4, j += 4) {
		if ((i & 0xf) == 0) {
			void *buf = mlx5_get_send_wqe(qp, tidx);
			tidx = (tidx + 1) & (qp->sq.wqe_cnt - 1);
			p = buf;
			j = 0;
		}
		fprintf(fp, "%08x %08x %08x %08x\n", be32toh(p[j]), be32toh(p[j + 1]),
			be32toh(p[j + 2]), be32toh(p[j + 3]));
	}
}
#endif /* MLX5_DEBUG */


void *mlx5_get_atomic_laddr(struct mlx5_qp *qp, uint16_t idx, int *byte_count)
{
	struct mlx5_wqe_data_seg *dpseg;
	void *addr;

	dpseg = mlx5_get_send_wqe(qp, idx) + sizeof(struct mlx5_wqe_ctrl_seg) +
		sizeof(struct mlx5_wqe_raddr_seg) +
		sizeof(struct mlx5_wqe_atomic_seg);
	addr = (void *)(unsigned long)be64toh(dpseg->addr);

	/*
	 * Currently byte count is always 8 bytes. Fix this when
	 * we support variable size of atomics
	 */
	*byte_count = 8;
	return addr;
}

static inline int copy_eth_inline_headers(struct ibv_qp *ibqp,
					  struct ibv_send_wr *wr,
					  struct mlx5_wqe_eth_seg *eseg,
					  struct mlx5_sg_copy_ptr *sg_copy_ptr)
{
	uint32_t inl_hdr_size = MLX5_ETH_L2_INLINE_HEADER_SIZE;
	int inl_hdr_copy_size = 0;
	int j = 0;
	FILE *fp = to_mctx(ibqp->context)->dbg_fp;

	if (unlikely(wr->num_sge < 1)) {
		mlx5_dbg(fp, MLX5_DBG_QP_SEND, "illegal num_sge: %d, minimum is 1\n",
			 wr->num_sge);
		return EINVAL;
	}

	if (likely(wr->sg_list[0].length >= MLX5_ETH_L2_INLINE_HEADER_SIZE)) {
		inl_hdr_copy_size = MLX5_ETH_L2_INLINE_HEADER_SIZE;
		memcpy(eseg->inline_hdr_start,
		       (void *)(uintptr_t)wr->sg_list[0].addr,
		       inl_hdr_copy_size);
	} else {
		for (j = 0; j < wr->num_sge && inl_hdr_size > 0; ++j) {
			inl_hdr_copy_size = min(wr->sg_list[j].length,
						inl_hdr_size);
			memcpy(eseg->inline_hdr_start +
			       (MLX5_ETH_L2_INLINE_HEADER_SIZE - inl_hdr_size),
			       (void *)(uintptr_t)wr->sg_list[j].addr,
			       inl_hdr_copy_size);
			inl_hdr_size -= inl_hdr_copy_size;
		}
		if (unlikely(inl_hdr_size)) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "Ethernet headers < 16 bytes\n");
			return EINVAL;
		}
		--j;
	}


	eseg->inline_hdr_sz = htobe16(MLX5_ETH_L2_INLINE_HEADER_SIZE);

	/* If we copied all the sge into the inline-headers, then we need to
	 * start copying from the next sge into the data-segment.
	 */
	if (unlikely(wr->sg_list[j].length == inl_hdr_copy_size)) {
		++j;
		inl_hdr_copy_size = 0;
	}

	sg_copy_ptr->index = j;
	sg_copy_ptr->offset = inl_hdr_copy_size;

	return 0;
}

#undef	ALIGN
#define ALIGN(x, log_a) ((((x) + (1 << (log_a)) - 1)) & ~((1 << (log_a)) - 1))

static inline uint16_t get_klm_octo(int nentries)
{
	return htobe16(ALIGN(nentries, 3) / 2);
}

static void set_umr_data_seg(struct mlx5_qp *qp, enum ibv_mw_type type,
			     int32_t rkey, struct ibv_mw_bind_info *bind_info,
			     uint32_t qpn, void **seg, int *size)
{
	union {
		struct mlx5_wqe_umr_klm_seg	klm;
		uint8_t				reserved[64];
	} *data = *seg;

	data->klm.byte_count = htobe32(bind_info->length);
	data->klm.mkey = htobe32(bind_info->mr->lkey);
	data->klm.address = htobe64(bind_info->addr);

	memset(&data->klm + 1, 0, sizeof(data->reserved) -
	       sizeof(data->klm));

	*seg += sizeof(*data);
	*size += (sizeof(*data) / 16);
}

static void set_umr_mkey_seg(struct mlx5_qp *qp, enum ibv_mw_type type,
			     int32_t rkey, struct ibv_mw_bind_info *bind_info,
			     uint32_t qpn, void **seg, int *size)
{
	struct mlx5_wqe_mkey_context_seg	*mkey = *seg;

	mkey->qpn_mkey = htobe32((rkey & 0xFF) |
				   ((type == IBV_MW_TYPE_1 || !bind_info->length) ?
				    0xFFFFFF00 : qpn << 8));
	if (bind_info->length) {
		/* Local read is set in kernel */
		mkey->access_flags = 0;
		mkey->free = 0;
		if (bind_info->mw_access_flags & IBV_ACCESS_LOCAL_WRITE)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_LOCAL_WRITE;
		if (bind_info->mw_access_flags & IBV_ACCESS_REMOTE_WRITE)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_WRITE;
		if (bind_info->mw_access_flags & IBV_ACCESS_REMOTE_READ)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_REMOTE_READ;
		if (bind_info->mw_access_flags & IBV_ACCESS_REMOTE_ATOMIC)
			mkey->access_flags |=
				MLX5_WQE_MKEY_CONTEXT_ACCESS_FLAGS_ATOMIC;
		if (bind_info->mw_access_flags & IBV_ACCESS_ZERO_BASED)
			mkey->start_addr = 0;
		else
			mkey->start_addr = htobe64(bind_info->addr);
		mkey->len = htobe64(bind_info->length);
	} else {
		mkey->free = MLX5_WQE_MKEY_CONTEXT_FREE;
	}

	*seg += sizeof(struct mlx5_wqe_mkey_context_seg);
	*size += (sizeof(struct mlx5_wqe_mkey_context_seg) / 16);
}

static inline void set_umr_control_seg(struct mlx5_qp *qp, enum ibv_mw_type type,
				       int32_t rkey, struct ibv_mw_bind_info *bind_info,
				       uint32_t qpn, void **seg, int *size)
{
	struct mlx5_wqe_umr_ctrl_seg		*ctrl = *seg;

	ctrl->flags = MLX5_WQE_UMR_CTRL_FLAG_TRNSLATION_OFFSET |
		MLX5_WQE_UMR_CTRL_FLAG_INLINE;
	ctrl->mkey_mask = htobe64(MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE |
				     MLX5_WQE_UMR_CTRL_MKEY_MASK_MKEY);
	ctrl->translation_offset = 0;
	memset(ctrl->rsvd0, 0, sizeof(ctrl->rsvd0));
	memset(ctrl->rsvd1, 0, sizeof(ctrl->rsvd1));

	if (type == IBV_MW_TYPE_2)
		ctrl->mkey_mask |= htobe64(MLX5_WQE_UMR_CTRL_MKEY_MASK_QPN);

	if (bind_info->length) {
		ctrl->klm_octowords = get_klm_octo(1);
		if (type == IBV_MW_TYPE_2)
			ctrl->flags |=  MLX5_WQE_UMR_CTRL_FLAG_CHECK_FREE;
		ctrl->mkey_mask |= htobe64(MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN	|
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_LOCAL_WRITE |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_READ |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_REMOTE_WRITE |
					      MLX5_WQE_UMR_CTRL_MKEY_MASK_ACCESS_ATOMIC);
	} else {
		ctrl->klm_octowords = get_klm_octo(0);
		if (type == IBV_MW_TYPE_2)
			ctrl->flags |= MLX5_WQE_UMR_CTRL_FLAG_CHECK_QPN;
	}

	*seg += sizeof(struct mlx5_wqe_umr_ctrl_seg);
	*size += sizeof(struct mlx5_wqe_umr_ctrl_seg) / 16;
}

static inline int set_bind_wr(struct mlx5_qp *qp, enum ibv_mw_type type,
			      int32_t rkey, struct ibv_mw_bind_info *bind_info,
			      uint32_t qpn, void **seg, int *size)
{
	void *qend = qp->sq.qend;

#ifdef MW_DEBUG
	if (bind_info->mw_access_flags &
	    ~(IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_READ |
	     IBV_ACCESS_REMOTE_WRITE))
		return EINVAL;

	if (bind_info->mr &&
	    (bind_info->mr->addr > (void *)bind_info->addr ||
	     bind_info->mr->addr + bind_info->mr->length <
	     (void *)bind_info->addr + bind_info->length ||
	     !(to_mmr(bind_info->mr)->alloc_flags &  IBV_ACCESS_MW_BIND) ||
	     (bind_info->mw_access_flags &
	      (IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_WRITE) &&
	      !(to_mmr(bind_info->mr)->alloc_flags & IBV_ACCESS_LOCAL_WRITE))))
		return EINVAL;

#endif

	/* check that len > 2GB because KLM support only 2GB */
	if (bind_info->length > 1UL << 31)
		return EOPNOTSUPP;

	set_umr_control_seg(qp, type, rkey, bind_info, qpn, seg, size);
	if (unlikely((*seg == qend)))
		*seg = mlx5_get_send_wqe(qp, 0);

	set_umr_mkey_seg(qp, type, rkey, bind_info, qpn, seg, size);
	if (!bind_info->length)
		return 0;

	if (unlikely((seg == qend)))
		*seg = mlx5_get_send_wqe(qp, 0);

	set_umr_data_seg(qp, type, rkey, bind_info, qpn, seg, size);
	return 0;
}

/* Copy tso header to eth segment with considering padding and WQE
 * wrap around in WQ buffer.
 */
static inline int set_tso_eth_seg(void **seg, struct ibv_send_wr *wr,
				   void *qend, struct mlx5_qp *qp, int *size)
{
	struct mlx5_wqe_eth_seg *eseg = *seg;
	int size_of_inl_hdr_start = sizeof(eseg->inline_hdr_start);
	uint64_t left, left_len, copy_sz;
	void *pdata = wr->tso.hdr;
	FILE *fp = to_mctx(qp->ibv_qp->context)->dbg_fp;

	if (unlikely(wr->tso.hdr_sz < MLX5_ETH_L2_MIN_HEADER_SIZE ||
		     wr->tso.hdr_sz > qp->max_tso_header)) {
		mlx5_dbg(fp, MLX5_DBG_QP_SEND,
			 "TSO header size should be at least %d and at most %d\n",
			 MLX5_ETH_L2_MIN_HEADER_SIZE,
			 qp->max_tso_header);
		return EINVAL;
	}

	left = wr->tso.hdr_sz;
	eseg->mss = htobe16(wr->tso.mss);
	eseg->inline_hdr_sz = htobe16(wr->tso.hdr_sz);

	/* Check if there is space till the end of queue, if yes,
	 * copy all in one shot, otherwise copy till the end of queue,
	 * rollback and then copy the left
	 */
	left_len = qend - (void *)eseg->inline_hdr_start;
	copy_sz = min(left_len, left);

	memcpy(eseg->inline_hdr_start, pdata, copy_sz);

	/* The -1 is because there are already 16 bytes included in
	 * eseg->inline_hdr[16]
	 */
	*seg += align(copy_sz - size_of_inl_hdr_start, 16) - 16;
	*size += align(copy_sz - size_of_inl_hdr_start, 16) / 16 - 1;

	/* The last wqe in the queue */
	if (unlikely(copy_sz < left)) {
		*seg = mlx5_get_send_wqe(qp, 0);
		left -= copy_sz;
		pdata += copy_sz;
		memcpy(*seg, pdata, left);
		*seg += align(left, 16);
		*size += align(left, 16) / 16;
	}

	return 0;
}

static inline int _mlx5_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
				  struct ibv_send_wr **bad_wr)
{
	struct mlx5_context *ctx;
	struct mlx5_qp *qp = to_mqp(ibqp);
	void *seg;
	struct mlx5_wqe_eth_seg *eseg;
	struct mlx5_wqe_ctrl_seg *ctrl = NULL;
	struct mlx5_wqe_data_seg *dpseg;
	struct mlx5_sg_copy_ptr sg_copy_ptr = {.index = 0, .offset = 0};
	int nreq;
	int inl = 0;
	int err = 0;
	int size = 0;
	int i;
	unsigned idx;
	uint8_t opmod = 0;
	struct mlx5_bf *bf = qp->bf;
	void *qend = qp->sq.qend;
	uint32_t mlx5_opcode;
	struct mlx5_wqe_xrc_seg *xrc;
	uint8_t fence;
	uint8_t next_fence;
	uint32_t max_tso = 0;
	FILE *fp = to_mctx(ibqp->context)->dbg_fp; /* The compiler ignores in non-debug mode */

	mlx5_spin_lock(&qp->sq.lock);

	next_fence = qp->fm_cache;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(wr->opcode < 0 ||
		    wr->opcode >= sizeof mlx5_ib_opcode / sizeof mlx5_ib_opcode[0])) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "bad opcode %d\n", wr->opcode);
			err = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(mlx5_wq_overflow(&qp->sq, nreq,
					      to_mcq(qp->ibv_qp->send_cq)))) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "work queue overflow\n");
			err = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->sq.max_gs)) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "max gs exceeded %d (max = %d)\n",
				 wr->num_sge, qp->sq.max_gs);
			err = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (wr->send_flags & IBV_SEND_FENCE)
			fence = MLX5_WQE_CTRL_FENCE;
		else
			fence = next_fence;
		next_fence = 0;
		idx = qp->sq.cur_post & (qp->sq.wqe_cnt - 1);
		ctrl = seg = mlx5_get_send_wqe(qp, idx);
		*(uint32_t *)(seg + 8) = 0;
		ctrl->imm = send_ieth(wr);
		ctrl->fm_ce_se = qp->sq_signal_bits | fence |
			(wr->send_flags & IBV_SEND_SIGNALED ?
			 MLX5_WQE_CTRL_CQ_UPDATE : 0) |
			(wr->send_flags & IBV_SEND_SOLICITED ?
			 MLX5_WQE_CTRL_SOLICITED : 0);

		seg += sizeof *ctrl;
		size = sizeof *ctrl / 16;

		switch (ibqp->qp_type) {
		case IBV_QPT_XRC_SEND:
			if (unlikely(wr->opcode != IBV_WR_BIND_MW &&
				     wr->opcode != IBV_WR_LOCAL_INV)) {
				xrc = seg;
				xrc->xrc_srqn = htobe32(wr->qp_type.xrc.remote_srqn);
				seg += sizeof(*xrc);
				size += sizeof(*xrc) / 16;
			}
			/* fall through */
		case IBV_QPT_RC:
			switch (wr->opcode) {
			case IBV_WR_RDMA_READ:
			case IBV_WR_RDMA_WRITE:
			case IBV_WR_RDMA_WRITE_WITH_IMM:
				set_raddr_seg(seg, wr->wr.rdma.remote_addr,
					      wr->wr.rdma.rkey);
				seg  += sizeof(struct mlx5_wqe_raddr_seg);
				size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
				break;

			case IBV_WR_ATOMIC_CMP_AND_SWP:
			case IBV_WR_ATOMIC_FETCH_AND_ADD:
				if (unlikely(!qp->atomics_enabled)) {
					mlx5_dbg(fp, MLX5_DBG_QP_SEND, "atomic operations are not supported\n");
					err = ENOSYS;
					*bad_wr = wr;
					goto out;
				}
				set_raddr_seg(seg, wr->wr.atomic.remote_addr,
					      wr->wr.atomic.rkey);
				seg  += sizeof(struct mlx5_wqe_raddr_seg);

				set_atomic_seg(seg, wr->opcode,
					       wr->wr.atomic.swap,
					       wr->wr.atomic.compare_add);
				seg  += sizeof(struct mlx5_wqe_atomic_seg);

				size += (sizeof(struct mlx5_wqe_raddr_seg) +
				sizeof(struct mlx5_wqe_atomic_seg)) / 16;
				break;

			case IBV_WR_BIND_MW:
				next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
				ctrl->imm = htobe32(wr->bind_mw.mw->rkey);
				err = set_bind_wr(qp, wr->bind_mw.mw->type,
						  wr->bind_mw.rkey,
						  &wr->bind_mw.bind_info,
						  ibqp->qp_num, &seg, &size);
				if (err) {
					*bad_wr = wr;
					goto out;
				}

				qp->sq.wr_data[idx] = IBV_WC_BIND_MW;
				break;
			case IBV_WR_LOCAL_INV: {
				struct ibv_mw_bind_info	bind_info = {};

				next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
				ctrl->imm = htobe32(wr->imm_data);
				err = set_bind_wr(qp, IBV_MW_TYPE_2, 0,
						  &bind_info, ibqp->qp_num,
						  &seg, &size);
				if (err) {
					*bad_wr = wr;
					goto out;
				}

				qp->sq.wr_data[idx] = IBV_WC_LOCAL_INV;
				break;
			}

			default:
				break;
			}
			break;

		case IBV_QPT_UC:
			switch (wr->opcode) {
			case IBV_WR_RDMA_WRITE:
			case IBV_WR_RDMA_WRITE_WITH_IMM:
				set_raddr_seg(seg, wr->wr.rdma.remote_addr,
					      wr->wr.rdma.rkey);
				seg  += sizeof(struct mlx5_wqe_raddr_seg);
				size += sizeof(struct mlx5_wqe_raddr_seg) / 16;
				break;
			case IBV_WR_BIND_MW:
				next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
				ctrl->imm = htobe32(wr->bind_mw.mw->rkey);
				err = set_bind_wr(qp, wr->bind_mw.mw->type,
						  wr->bind_mw.rkey,
						  &wr->bind_mw.bind_info,
						  ibqp->qp_num, &seg, &size);
				if (err) {
					*bad_wr = wr;
					goto out;
				}

				qp->sq.wr_data[idx] = IBV_WC_BIND_MW;
				break;
			case IBV_WR_LOCAL_INV: {
				struct ibv_mw_bind_info	bind_info = {};

				next_fence = MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;
				ctrl->imm = htobe32(wr->imm_data);
				err = set_bind_wr(qp, IBV_MW_TYPE_2, 0,
						  &bind_info, ibqp->qp_num,
						  &seg, &size);
				if (err) {
					*bad_wr = wr;
					goto out;
				}

				qp->sq.wr_data[idx] = IBV_WC_LOCAL_INV;
				break;
			}

			default:
				break;
			}
			break;

		case IBV_QPT_UD:
			set_datagram_seg(seg, wr);
			seg  += sizeof(struct mlx5_wqe_datagram_seg);
			size += sizeof(struct mlx5_wqe_datagram_seg) / 16;
			if (unlikely((seg == qend)))
				seg = mlx5_get_send_wqe(qp, 0);
			break;

		case IBV_QPT_RAW_PACKET:
			memset(seg, 0, sizeof(struct mlx5_wqe_eth_seg));
			eseg = seg;

			if (wr->send_flags & IBV_SEND_IP_CSUM) {
				if (!(qp->qp_cap_cache & MLX5_CSUM_SUPPORT_RAW_OVER_ETH)) {
					err = EINVAL;
					*bad_wr = wr;
					goto out;
				}

				eseg->cs_flags |= MLX5_ETH_WQE_L3_CSUM | MLX5_ETH_WQE_L4_CSUM;
			}

			if (wr->opcode == IBV_WR_TSO) {
				max_tso = qp->max_tso;
				err = set_tso_eth_seg(&seg, wr, qend, qp, &size);
				if (unlikely(err)) {
					*bad_wr = wr;
					goto out;
				}
			} else {
				err = copy_eth_inline_headers(ibqp, wr, seg, &sg_copy_ptr);
				if (unlikely(err)) {
					*bad_wr = wr;
					mlx5_dbg(fp, MLX5_DBG_QP_SEND,
						 "copy_eth_inline_headers failed, err: %d\n",
						 err);
					goto out;
				}
			}

			seg += sizeof(struct mlx5_wqe_eth_seg);
			size += sizeof(struct mlx5_wqe_eth_seg) / 16;
			break;

		default:
			break;
		}

		if (wr->send_flags & IBV_SEND_INLINE && wr->num_sge) {
			int sz = 0;

			err = set_data_inl_seg(qp, wr, seg, &sz, &sg_copy_ptr);
			if (unlikely(err)) {
				*bad_wr = wr;
				mlx5_dbg(fp, MLX5_DBG_QP_SEND,
					 "inline layout failed, err %d\n", err);
				goto out;
			}
			inl = 1;
			size += sz;
		} else {
			dpseg = seg;
			for (i = sg_copy_ptr.index; i < wr->num_sge; ++i) {
				if (unlikely(dpseg == qend)) {
					seg = mlx5_get_send_wqe(qp, 0);
					dpseg = seg;
				}
				if (likely(wr->sg_list[i].length)) {
					if (unlikely(wr->opcode ==
						   IBV_WR_ATOMIC_CMP_AND_SWP ||
						   wr->opcode ==
						   IBV_WR_ATOMIC_FETCH_AND_ADD))
						set_data_ptr_seg_atomic(dpseg, wr->sg_list + i);
					else {
						if (unlikely(wr->opcode == IBV_WR_TSO)) {
							if (max_tso < wr->sg_list[i].length) {
								err = EINVAL;
								*bad_wr = wr;
								goto out;
							}
							max_tso -= wr->sg_list[i].length;
						}
						set_data_ptr_seg(dpseg, wr->sg_list + i,
								 sg_copy_ptr.offset);
					}
					sg_copy_ptr.offset = 0;
					++dpseg;
					size += sizeof(struct mlx5_wqe_data_seg) / 16;
				}
			}
		}

		mlx5_opcode = mlx5_ib_opcode[wr->opcode];
		ctrl->opmod_idx_opcode = htobe32(((qp->sq.cur_post & 0xffff) << 8) |
					       mlx5_opcode			 |
					       (opmod << 24));
		ctrl->qpn_ds = htobe32(size | (ibqp->qp_num << 8));

		if (unlikely(qp->wq_sig))
			ctrl->signature = wq_sig(ctrl);

		qp->sq.wrid[idx] = wr->wr_id;
		qp->sq.wqe_head[idx] = qp->sq.head + nreq;
		qp->sq.cur_post += DIV_ROUND_UP(size * 16, MLX5_SEND_WQE_BB);

#ifdef MLX5_DEBUG
		if (mlx5_debug_mask & MLX5_DBG_QP_SEND)
			dump_wqe(to_mctx(ibqp->context)->dbg_fp, idx, size, qp);
#endif
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;
		qp->fm_cache = next_fence;

		/*
		 * Make sure that descriptors are written before
		 * updating doorbell record and ringing the doorbell
		 */
		udma_to_device_barrier();
		qp->db[MLX5_SND_DBR] = htobe32(qp->sq.cur_post & 0xffff);

		/* Make sure that the doorbell write happens before the memcpy
		 * to WC memory below */
		ctx = to_mctx(ibqp->context);
		if (bf->need_lock)
			mmio_wc_spinlock(&bf->lock.lock);
		else
			mmio_wc_start();

		if (!ctx->shut_up_bf && nreq == 1 && bf->uuarn &&
		    (inl || ctx->prefer_bf) && size > 1 &&
		    size <= bf->buf_size / 16)
			mlx5_bf_copy(bf->reg + bf->offset, (unsigned long long *)ctrl,
				     align(size * 16, 64), qp);
		else
			mlx5_write64((__be32 *)ctrl, bf->reg + bf->offset,
				     &ctx->lock32);

		/*
		 * use mmio_flush_writes() to ensure write combining buffers are flushed out
		 * of the running CPU. This must be carried inside the spinlock.
		 * Otherwise, there is a potential race. In the race, CPU A
		 * writes doorbell 1, which is waiting in the WC buffer. CPU B
		 * writes doorbell 2, and it's write is flushed earlier. Since
		 * the mmio_flush_writes is CPU local, this will result in the HCA seeing
		 * doorbell 2, followed by doorbell 1.
		 * Flush before toggling bf_offset to be latency oriented.
		 */
		mmio_flush_writes();
		bf->offset ^= bf->buf_size;
		if (bf->need_lock)
			mlx5_spin_unlock(&bf->lock);
	}

	mlx5_spin_unlock(&qp->sq.lock);

	return err;
}

int mlx5_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
		   struct ibv_send_wr **bad_wr)
{
#ifdef MW_DEBUG
	if (wr->opcode == IBV_WR_BIND_MW) {
		if (wr->bind_mw.mw->type == IBV_MW_TYPE_1)
			return EINVAL;

		if (!wr->bind_mw.bind_info.mr ||
		    !wr->bind_mw.bind_info.addr ||
		    !wr->bind_mw.bind_info.length)
			return EINVAL;

		if (wr->bind_mw.bind_info.mr->pd != wr->bind_mw.mw->pd)
			return EINVAL;
	}
#endif

	return _mlx5_post_send(ibqp, wr, bad_wr);
}

int mlx5_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
		 struct ibv_mw_bind *mw_bind)
{
	struct ibv_mw_bind_info	*bind_info = &mw_bind->bind_info;
	struct ibv_send_wr wr = {};
	struct ibv_send_wr *bad_wr = NULL;
	int ret;

	if (!bind_info->mr && (bind_info->addr || bind_info->length)) {
		errno = EINVAL;
		return errno;
	}

	if (bind_info->mw_access_flags & IBV_ACCESS_ZERO_BASED) {
		errno = EINVAL;
		return errno;
	}

	if (bind_info->mr) {
		if (to_mmr(bind_info->mr)->alloc_flags & IBV_ACCESS_ZERO_BASED) {
			errno = EINVAL;
			return errno;
		}

		if (mw->pd != bind_info->mr->pd) {
			errno = EPERM;
			return errno;
		}
	}

	wr.opcode = IBV_WR_BIND_MW;
	wr.next = NULL;
	wr.wr_id = mw_bind->wr_id;
	wr.send_flags = mw_bind->send_flags;
	wr.bind_mw.bind_info = mw_bind->bind_info;
	wr.bind_mw.mw = mw;
	wr.bind_mw.rkey = ibv_inc_rkey(mw->rkey);

	ret = _mlx5_post_send(qp, &wr, &bad_wr);
	if (ret)
		return ret;

	mw->rkey = wr.bind_mw.rkey;

	return 0;
}

static void set_sig_seg(struct mlx5_qp *qp, struct mlx5_rwqe_sig *sig,
			int size, uint16_t idx)
{
	uint8_t  sign;
	uint32_t qpn = qp->ibv_qp->qp_num;

	sign = calc_sig(sig, size);
	sign ^= calc_sig(&qpn, 4);
	sign ^= calc_sig(&idx, 2);
	sig->signature = sign;
}

static void set_wq_sig_seg(struct mlx5_rwq *rwq, struct mlx5_rwqe_sig *sig,
			   int size, uint16_t idx)
{
	uint8_t  sign;
	uint32_t qpn = rwq->wq.wq_num;

	sign = calc_sig(sig, size);
	sign ^= calc_sig(&qpn, 4);
	sign ^= calc_sig(&idx, 2);
	sig->signature = sign;
}

int mlx5_post_wq_recv(struct ibv_wq *ibwq, struct ibv_recv_wr *wr,
		      struct ibv_recv_wr **bad_wr)
{
	struct mlx5_rwq *rwq = to_mrwq(ibwq);
	struct mlx5_wqe_data_seg *scat;
	int err = 0;
	int nreq;
	int ind;
	int i, j;
	struct mlx5_rwqe_sig *sig;

	mlx5_spin_lock(&rwq->rq.lock);

	ind = rwq->rq.head & (rwq->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(mlx5_wq_overflow(&rwq->rq, nreq,
					      to_mcq(rwq->wq.cq)))) {
			err = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > rwq->rq.max_gs)) {
			err = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		scat = get_wq_recv_wqe(rwq, ind);
		sig = (struct mlx5_rwqe_sig *)scat;
		if (unlikely(rwq->wq_sig)) {
			memset(sig, 0, 1 << rwq->rq.wqe_shift);
			++scat;
		}

		for (i = 0, j = 0; i < wr->num_sge; ++i) {
			if (unlikely(!wr->sg_list[i].length))
				continue;
			set_data_ptr_seg(scat + j++, wr->sg_list + i, 0);
		}

		if (j < rwq->rq.max_gs) {
			scat[j].byte_count = 0;
			scat[j].lkey       = htobe32(MLX5_INVALID_LKEY);
			scat[j].addr       = 0;
		}

		if (unlikely(rwq->wq_sig))
			set_wq_sig_seg(rwq, sig, (wr->num_sge + 1) << 4,
				       rwq->rq.head & 0xffff);

		rwq->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (rwq->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		rwq->rq.head += nreq;
		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		udma_to_device_barrier();
		*(rwq->recv_db) = htobe32(rwq->rq.head & 0xffff);
	}

	mlx5_spin_unlock(&rwq->rq.lock);

	return err;
}

int mlx5_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		   struct ibv_recv_wr **bad_wr)
{
	struct mlx5_qp *qp = to_mqp(ibqp);
	struct mlx5_wqe_data_seg *scat;
	int err = 0;
	int nreq;
	int ind;
	int i, j;
	struct mlx5_rwqe_sig *sig;

	mlx5_spin_lock(&qp->rq.lock);

	ind = qp->rq.head & (qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(mlx5_wq_overflow(&qp->rq, nreq,
					      to_mcq(qp->ibv_qp->recv_cq)))) {
			err = ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->rq.max_gs)) {
			err = EINVAL;
			*bad_wr = wr;
			goto out;
		}

		scat = get_recv_wqe(qp, ind);
		sig = (struct mlx5_rwqe_sig *)scat;
		if (unlikely(qp->wq_sig)) {
			memset(sig, 0, 1 << qp->rq.wqe_shift);
			++scat;
		}

		for (i = 0, j = 0; i < wr->num_sge; ++i) {
			if (unlikely(!wr->sg_list[i].length))
				continue;
			set_data_ptr_seg(scat + j++, wr->sg_list + i, 0);
		}

		if (j < qp->rq.max_gs) {
			scat[j].byte_count = 0;
			scat[j].lkey       = htobe32(MLX5_INVALID_LKEY);
			scat[j].addr       = 0;
		}

		if (unlikely(qp->wq_sig))
			set_sig_seg(qp, sig, (wr->num_sge + 1) << 4,
				    qp->rq.head & 0xffff);

		qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (qp->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		qp->rq.head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		udma_to_device_barrier();

		/*
		 * For Raw Packet QP, avoid updating the doorbell record
		 * as long as the QP isn't in RTR state, to avoid receiving
		 * packets in illegal states.
		 * This is only for Raw Packet QPs since they are represented
		 * differently in the hardware.
		 */
		if (likely(!(ibqp->qp_type == IBV_QPT_RAW_PACKET &&
			     ibqp->state < IBV_QPS_RTR)))
			qp->db[MLX5_RCV_DBR] = htobe32(qp->rq.head & 0xffff);
	}

	mlx5_spin_unlock(&qp->rq.lock);

	return err;
}

int mlx5_use_huge(const char *key)
{
	char *e;
	e = getenv(key);
	if (e && !strcmp(e, "y"))
		return 1;

	return 0;
}

struct mlx5_qp *mlx5_find_qp(struct mlx5_context *ctx, uint32_t qpn)
{
	int tind = qpn >> MLX5_QP_TABLE_SHIFT;

	if (ctx->qp_table[tind].refcnt)
		return ctx->qp_table[tind].table[qpn & MLX5_QP_TABLE_MASK];
	else
		return NULL;
}

int mlx5_store_qp(struct mlx5_context *ctx, uint32_t qpn, struct mlx5_qp *qp)
{
	int tind = qpn >> MLX5_QP_TABLE_SHIFT;

	if (!ctx->qp_table[tind].refcnt) {
		ctx->qp_table[tind].table = calloc(MLX5_QP_TABLE_MASK + 1,
						   sizeof(struct mlx5_qp *));
		if (!ctx->qp_table[tind].table)
			return -1;
	}

	++ctx->qp_table[tind].refcnt;
	ctx->qp_table[tind].table[qpn & MLX5_QP_TABLE_MASK] = qp;
	return 0;
}

void mlx5_clear_qp(struct mlx5_context *ctx, uint32_t qpn)
{
	int tind = qpn >> MLX5_QP_TABLE_SHIFT;

	if (!--ctx->qp_table[tind].refcnt)
		free(ctx->qp_table[tind].table);
	else
		ctx->qp_table[tind].table[qpn & MLX5_QP_TABLE_MASK] = NULL;
}
