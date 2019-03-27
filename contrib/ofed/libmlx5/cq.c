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

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <infiniband/opcode.h>

#include "mlx5.h"
#include "wqe.h"
#include "doorbell.h"

enum {
	CQ_OK					=  0,
	CQ_EMPTY				= -1,
	CQ_POLL_ERR				= -2
};

enum {
	MLX5_CQ_MODIFY_RESEIZE = 0,
	MLX5_CQ_MODIFY_MODER = 1,
	MLX5_CQ_MODIFY_MAPPING = 2,
};

int mlx5_stall_num_loop = 60;
int mlx5_stall_cq_poll_min = 60;
int mlx5_stall_cq_poll_max = 100000;
int mlx5_stall_cq_inc_step = 100;
int mlx5_stall_cq_dec_step = 10;

static inline uint8_t get_cqe_l3_hdr_type(struct mlx5_cqe64 *cqe)
{
	return (cqe->l4_hdr_type_etc >> 2) & 0x3;
}

static void *get_buf_cqe(struct mlx5_buf *buf, int n, int cqe_sz)
{
	return buf->buf + n * cqe_sz;
}

static void *get_cqe(struct mlx5_cq *cq, int n)
{
	return cq->active_buf->buf + n * cq->cqe_sz;
}

static void *get_sw_cqe(struct mlx5_cq *cq, int n)
{
	void *cqe = get_cqe(cq, n & cq->ibv_cq.cqe);
	struct mlx5_cqe64 *cqe64;

	cqe64 = (cq->cqe_sz == 64) ? cqe : cqe + 64;

	if (likely(mlx5dv_get_cqe_opcode(cqe64) != MLX5_CQE_INVALID) &&
	    !((cqe64->op_own & MLX5_CQE_OWNER_MASK) ^ !!(n & (cq->ibv_cq.cqe + 1)))) {
		return cqe;
	} else {
		return NULL;
	}
}

static void *next_cqe_sw(struct mlx5_cq *cq)
{
	return get_sw_cqe(cq, cq->cons_index);
}

static void update_cons_index(struct mlx5_cq *cq)
{
	cq->dbrec[MLX5_CQ_SET_CI] = htobe32(cq->cons_index & 0xffffff);
}

static inline void handle_good_req(struct ibv_wc *wc, struct mlx5_cqe64 *cqe, struct mlx5_wq *wq, int idx)
{
	switch (be32toh(cqe->sop_drop_qpn) >> 24) {
	case MLX5_OPCODE_RDMA_WRITE_IMM:
		wc->wc_flags |= IBV_WC_WITH_IMM;
		SWITCH_FALLTHROUGH;
	case MLX5_OPCODE_RDMA_WRITE:
		wc->opcode    = IBV_WC_RDMA_WRITE;
		break;
	case MLX5_OPCODE_SEND_IMM:
		wc->wc_flags |= IBV_WC_WITH_IMM;
		SWITCH_FALLTHROUGH;
	case MLX5_OPCODE_SEND:
	case MLX5_OPCODE_SEND_INVAL:
		wc->opcode    = IBV_WC_SEND;
		break;
	case MLX5_OPCODE_RDMA_READ:
		wc->opcode    = IBV_WC_RDMA_READ;
		wc->byte_len  = be32toh(cqe->byte_cnt);
		break;
	case MLX5_OPCODE_ATOMIC_CS:
		wc->opcode    = IBV_WC_COMP_SWAP;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_ATOMIC_FA:
		wc->opcode    = IBV_WC_FETCH_ADD;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_UMR:
		wc->opcode = wq->wr_data[idx];
		break;
	case MLX5_OPCODE_TSO:
		wc->opcode    = IBV_WC_TSO;
		break;
	}
}

static inline int handle_responder_lazy(struct mlx5_cq *cq, struct mlx5_cqe64 *cqe,
					struct mlx5_resource *cur_rsc, struct mlx5_srq *srq)
{
	uint16_t	wqe_ctr;
	struct mlx5_wq *wq;
	struct mlx5_qp *qp = rsc_to_mqp(cur_rsc);
	int err = IBV_WC_SUCCESS;

	if (srq) {
		wqe_ctr = be16toh(cqe->wqe_counter);
		cq->ibv_cq.wr_id = srq->wrid[wqe_ctr];
		mlx5_free_srq_wqe(srq, wqe_ctr);
		if (cqe->op_own & MLX5_INLINE_SCATTER_32)
			err = mlx5_copy_to_recv_srq(srq, wqe_ctr, cqe,
						    be32toh(cqe->byte_cnt));
		else if (cqe->op_own & MLX5_INLINE_SCATTER_64)
			err = mlx5_copy_to_recv_srq(srq, wqe_ctr, cqe - 1,
						    be32toh(cqe->byte_cnt));
	} else {
		if (likely(cur_rsc->type == MLX5_RSC_TYPE_QP)) {
			wq = &qp->rq;
			if (qp->qp_cap_cache & MLX5_RX_CSUM_VALID)
				cq->flags |= MLX5_CQ_FLAGS_RX_CSUM_VALID;
		} else {
			wq = &(rsc_to_mrwq(cur_rsc)->rq);
		}

		wqe_ctr = wq->tail & (wq->wqe_cnt - 1);
		cq->ibv_cq.wr_id = wq->wrid[wqe_ctr];
		++wq->tail;
		if (cqe->op_own & MLX5_INLINE_SCATTER_32)
			err = mlx5_copy_to_recv_wqe(qp, wqe_ctr, cqe,
						    be32toh(cqe->byte_cnt));
		else if (cqe->op_own & MLX5_INLINE_SCATTER_64)
			err = mlx5_copy_to_recv_wqe(qp, wqe_ctr, cqe - 1,
						    be32toh(cqe->byte_cnt));
	}

	return err;
}

static inline int handle_responder(struct ibv_wc *wc, struct mlx5_cqe64 *cqe,
				   struct mlx5_resource *cur_rsc, struct mlx5_srq *srq)
{
	uint16_t	wqe_ctr;
	struct mlx5_wq *wq;
	struct mlx5_qp *qp = rsc_to_mqp(cur_rsc);
	uint8_t g;
	int err = 0;

	wc->byte_len = be32toh(cqe->byte_cnt);
	if (srq) {
		wqe_ctr = be16toh(cqe->wqe_counter);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx5_free_srq_wqe(srq, wqe_ctr);
		if (cqe->op_own & MLX5_INLINE_SCATTER_32)
			err = mlx5_copy_to_recv_srq(srq, wqe_ctr, cqe,
						    wc->byte_len);
		else if (cqe->op_own & MLX5_INLINE_SCATTER_64)
			err = mlx5_copy_to_recv_srq(srq, wqe_ctr, cqe - 1,
						    wc->byte_len);
	} else {
		if (likely(cur_rsc->type == MLX5_RSC_TYPE_QP)) {
			wq = &qp->rq;
			if (qp->qp_cap_cache & MLX5_RX_CSUM_VALID)
				wc->wc_flags |= (!!(cqe->hds_ip_ext & MLX5_CQE_L4_OK) &
						 !!(cqe->hds_ip_ext & MLX5_CQE_L3_OK) &
						(get_cqe_l3_hdr_type(cqe) ==
						MLX5_CQE_L3_HDR_TYPE_IPV4)) <<
						IBV_WC_IP_CSUM_OK_SHIFT;
		} else {
			wq = &(rsc_to_mrwq(cur_rsc)->rq);
		}

		wqe_ctr = wq->tail & (wq->wqe_cnt - 1);
		wc->wr_id = wq->wrid[wqe_ctr];
		++wq->tail;
		if (cqe->op_own & MLX5_INLINE_SCATTER_32)
			err = mlx5_copy_to_recv_wqe(qp, wqe_ctr, cqe,
						    wc->byte_len);
		else if (cqe->op_own & MLX5_INLINE_SCATTER_64)
			err = mlx5_copy_to_recv_wqe(qp, wqe_ctr, cqe - 1,
						    wc->byte_len);
	}
	if (err)
		return err;

	switch (cqe->op_own >> 4) {
	case MLX5_CQE_RESP_WR_IMM:
		wc->opcode	= IBV_WC_RECV_RDMA_WITH_IMM;
		wc->wc_flags	|= IBV_WC_WITH_IMM;
		wc->imm_data = cqe->imm_inval_pkey;
		break;
	case MLX5_CQE_RESP_SEND:
		wc->opcode   = IBV_WC_RECV;
		break;
	case MLX5_CQE_RESP_SEND_IMM:
		wc->opcode	= IBV_WC_RECV;
		wc->wc_flags	|= IBV_WC_WITH_IMM;
		wc->imm_data = cqe->imm_inval_pkey;
		break;
	case MLX5_CQE_RESP_SEND_INV:
		wc->opcode = IBV_WC_RECV;
		wc->wc_flags |= IBV_WC_WITH_INV;
		wc->imm_data = be32toh(cqe->imm_inval_pkey);
		break;
	}
	wc->slid	   = be16toh(cqe->slid);
	wc->sl		   = (be32toh(cqe->flags_rqpn) >> 24) & 0xf;
	wc->src_qp	   = be32toh(cqe->flags_rqpn) & 0xffffff;
	wc->dlid_path_bits = cqe->ml_path & 0x7f;
	g = (be32toh(cqe->flags_rqpn) >> 28) & 3;
	wc->wc_flags |= g ? IBV_WC_GRH : 0;
	wc->pkey_index     = be32toh(cqe->imm_inval_pkey) & 0xffff;

	return IBV_WC_SUCCESS;
}

static void dump_cqe(FILE *fp, void *buf)
{
	uint32_t *p = buf;
	int i;

	for (i = 0; i < 16; i += 4)
		fprintf(fp, "%08x %08x %08x %08x\n", be32toh(p[i]), be32toh(p[i + 1]),
			be32toh(p[i + 2]), be32toh(p[i + 3]));
}

static enum ibv_wc_status mlx5_handle_error_cqe(struct mlx5_err_cqe *cqe)
{
	switch (cqe->syndrome) {
	case MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		return IBV_WC_LOC_LEN_ERR;
	case MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		return IBV_WC_LOC_QP_OP_ERR;
	case MLX5_CQE_SYNDROME_LOCAL_PROT_ERR:
		return IBV_WC_LOC_PROT_ERR;
	case MLX5_CQE_SYNDROME_WR_FLUSH_ERR:
		return IBV_WC_WR_FLUSH_ERR;
	case MLX5_CQE_SYNDROME_MW_BIND_ERR:
		return IBV_WC_MW_BIND_ERR;
	case MLX5_CQE_SYNDROME_BAD_RESP_ERR:
		return IBV_WC_BAD_RESP_ERR;
	case MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		return IBV_WC_LOC_ACCESS_ERR;
	case MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		return IBV_WC_REM_INV_REQ_ERR;
	case MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		return IBV_WC_REM_ACCESS_ERR;
	case MLX5_CQE_SYNDROME_REMOTE_OP_ERR:
		return IBV_WC_REM_OP_ERR;
	case MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		return IBV_WC_RETRY_EXC_ERR;
	case MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		return IBV_WC_RNR_RETRY_EXC_ERR;
	case MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		return IBV_WC_REM_ABORT_ERR;
	default:
		return IBV_WC_GENERAL_ERR;
	}
}

#if defined(__x86_64__) || defined (__i386__)
static inline unsigned long get_cycles(void)
{
	uint32_t low, high;
	uint64_t val;
	asm volatile ("rdtsc" : "=a" (low), "=d" (high));
	val = high;
	val = (val << 32) | low;
	return val;
}

static void mlx5_stall_poll_cq(void)
{
	int i;

	for (i = 0; i < mlx5_stall_num_loop; i++)
		(void)get_cycles();
}
static void mlx5_stall_cycles_poll_cq(uint64_t cycles)
{
	while (get_cycles()  <  cycles)
		; /* Nothing */
}
static void mlx5_get_cycles(uint64_t *cycles)
{
	*cycles = get_cycles();
}
#else
static void mlx5_stall_poll_cq(void)
{
}
static void mlx5_stall_cycles_poll_cq(uint64_t cycles)
{
}
static void mlx5_get_cycles(uint64_t *cycles)
{
}
#endif

static inline struct mlx5_qp *get_req_context(struct mlx5_context *mctx,
					      struct mlx5_resource **cur_rsc,
					      uint32_t rsn, int cqe_ver)
					      ALWAYS_INLINE;
static inline struct mlx5_qp *get_req_context(struct mlx5_context *mctx,
					      struct mlx5_resource **cur_rsc,
					      uint32_t rsn, int cqe_ver)
{
	if (!*cur_rsc || (rsn != (*cur_rsc)->rsn))
		*cur_rsc = cqe_ver ? mlx5_find_uidx(mctx, rsn) :
				      (struct mlx5_resource *)mlx5_find_qp(mctx, rsn);

	return rsc_to_mqp(*cur_rsc);
}

static inline int get_resp_ctx_v1(struct mlx5_context *mctx,
				  struct mlx5_resource **cur_rsc,
				  struct mlx5_srq **cur_srq,
				  uint32_t uidx, uint8_t *is_srq)
				  ALWAYS_INLINE;
static inline int get_resp_ctx_v1(struct mlx5_context *mctx,
				  struct mlx5_resource **cur_rsc,
				  struct mlx5_srq **cur_srq,
				  uint32_t uidx, uint8_t *is_srq)
{
	struct mlx5_qp *mqp;

	if (!*cur_rsc || (uidx != (*cur_rsc)->rsn)) {
		*cur_rsc = mlx5_find_uidx(mctx, uidx);
		if (unlikely(!*cur_rsc))
			return CQ_POLL_ERR;
	}

	switch ((*cur_rsc)->type) {
	case MLX5_RSC_TYPE_QP:
		mqp = rsc_to_mqp(*cur_rsc);
		if (mqp->verbs_qp.qp.srq) {
			*cur_srq = to_msrq(mqp->verbs_qp.qp.srq);
			*is_srq = 1;
		}
		break;
	case MLX5_RSC_TYPE_XSRQ:
		*cur_srq = rsc_to_msrq(*cur_rsc);
		*is_srq = 1;
		break;
	case MLX5_RSC_TYPE_RWQ:
		break;
	default:
		return CQ_POLL_ERR;
	}

	return CQ_OK;
}

static inline int get_qp_ctx(struct mlx5_context *mctx,
			     struct mlx5_resource **cur_rsc,
			     uint32_t qpn)
			     ALWAYS_INLINE;
static inline int get_qp_ctx(struct mlx5_context *mctx,
			     struct mlx5_resource **cur_rsc,
			     uint32_t qpn)
{
	if (!*cur_rsc || (qpn != (*cur_rsc)->rsn)) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		*cur_rsc = (struct mlx5_resource *)mlx5_find_qp(mctx, qpn);
		if (unlikely(!*cur_rsc))
			return CQ_POLL_ERR;
	}

	return CQ_OK;
}

static inline int get_srq_ctx(struct mlx5_context *mctx,
			      struct mlx5_srq **cur_srq,
			      uint32_t srqn_uidx)
			      ALWAYS_INLINE;
static inline int get_srq_ctx(struct mlx5_context *mctx,
			      struct mlx5_srq **cur_srq,
			      uint32_t srqn)
{
	if (!*cur_srq || (srqn != (*cur_srq)->srqn)) {
		*cur_srq = mlx5_find_srq(mctx, srqn);
		if (unlikely(!*cur_srq))
			return CQ_POLL_ERR;
	}

	return CQ_OK;
}

static inline int get_cur_rsc(struct mlx5_context *mctx,
			      int cqe_ver,
			      uint32_t qpn,
			      uint32_t srqn_uidx,
			      struct mlx5_resource **cur_rsc,
			      struct mlx5_srq **cur_srq,
			      uint8_t *is_srq)
{
	int err;

	if (cqe_ver) {
		err = get_resp_ctx_v1(mctx, cur_rsc, cur_srq, srqn_uidx,
				      is_srq);
	} else {
		if (srqn_uidx) {
			*is_srq = 1;
			err = get_srq_ctx(mctx, cur_srq, srqn_uidx);
		} else {
			err = get_qp_ctx(mctx, cur_rsc, qpn);
		}
	}

	return err;

}

static inline int mlx5_get_next_cqe(struct mlx5_cq *cq,
				    struct mlx5_cqe64 **pcqe64,
				    void **pcqe)
				    ALWAYS_INLINE;
static inline int mlx5_get_next_cqe(struct mlx5_cq *cq,
				    struct mlx5_cqe64 **pcqe64,
				    void **pcqe)
{
	void *cqe;
	struct mlx5_cqe64 *cqe64;

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return CQ_EMPTY;

	cqe64 = (cq->cqe_sz == 64) ? cqe : cqe + 64;

	++cq->cons_index;

	VALGRIND_MAKE_MEM_DEFINED(cqe64, sizeof *cqe64);

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	udma_from_device_barrier();

#ifdef MLX5_DEBUG
	{
		struct mlx5_context *mctx = to_mctx(cq->ibv_cq.context);

		if (mlx5_debug_mask & MLX5_DBG_CQ_CQE) {
			FILE *fp = mctx->dbg_fp;

			mlx5_dbg(fp, MLX5_DBG_CQ_CQE, "dump cqe for cqn 0x%x:\n", cq->cqn);
			dump_cqe(fp, cqe64);
		}
	}
#endif
	*pcqe64 = cqe64;
	*pcqe = cqe;

	return CQ_OK;
}

static inline int mlx5_parse_cqe(struct mlx5_cq *cq,
				 struct mlx5_cqe64 *cqe64,
				 void *cqe,
				 struct mlx5_resource **cur_rsc,
				 struct mlx5_srq **cur_srq,
				 struct ibv_wc *wc,
				 int cqe_ver, int lazy)
				 ALWAYS_INLINE;
static inline int mlx5_parse_cqe(struct mlx5_cq *cq,
				 struct mlx5_cqe64 *cqe64,
				 void *cqe,
				 struct mlx5_resource **cur_rsc,
				 struct mlx5_srq **cur_srq,
				 struct ibv_wc *wc,
				 int cqe_ver, int lazy)
{
	struct mlx5_wq *wq;
	uint16_t wqe_ctr;
	uint32_t qpn;
	uint32_t srqn_uidx;
	int idx;
	uint8_t opcode;
	struct mlx5_err_cqe *ecqe;
	int err = 0;
	struct mlx5_qp *mqp;
	struct mlx5_context *mctx;
	uint8_t is_srq = 0;

	mctx = to_mctx(ibv_cq_ex_to_cq(&cq->ibv_cq)->context);
	qpn = be32toh(cqe64->sop_drop_qpn) & 0xffffff;
	if (lazy) {
		cq->cqe64 = cqe64;
		cq->flags &= (~MLX5_CQ_FLAGS_RX_CSUM_VALID);
	} else {
		wc->wc_flags = 0;
		wc->qp_num = qpn;
	}

	opcode = mlx5dv_get_cqe_opcode(cqe64);
	switch (opcode) {
	case MLX5_CQE_REQ:
	{
		mqp = get_req_context(mctx, cur_rsc,
				      (cqe_ver ? (be32toh(cqe64->srqn_uidx) & 0xffffff) : qpn),
				      cqe_ver);
		if (unlikely(!mqp))
			return CQ_POLL_ERR;
		wq = &mqp->sq;
		wqe_ctr = be16toh(cqe64->wqe_counter);
		idx = wqe_ctr & (wq->wqe_cnt - 1);
		if (lazy) {
			uint32_t wc_byte_len;

			switch (be32toh(cqe64->sop_drop_qpn) >> 24) {
			case MLX5_OPCODE_UMR:
				cq->umr_opcode = wq->wr_data[idx];
				break;

			case MLX5_OPCODE_RDMA_READ:
				wc_byte_len = be32toh(cqe64->byte_cnt);
				goto scatter_out;
			case MLX5_OPCODE_ATOMIC_CS:
			case MLX5_OPCODE_ATOMIC_FA:
				wc_byte_len = 8;

			scatter_out:
				if (cqe64->op_own & MLX5_INLINE_SCATTER_32)
					err = mlx5_copy_to_send_wqe(
					    mqp, wqe_ctr, cqe, wc_byte_len);
				else if (cqe64->op_own & MLX5_INLINE_SCATTER_64)
					err = mlx5_copy_to_send_wqe(
					    mqp, wqe_ctr, cqe - 1, wc_byte_len);
				break;
			}

			cq->ibv_cq.wr_id = wq->wrid[idx];
			cq->ibv_cq.status = err;
		} else {
			handle_good_req(wc, cqe64, wq, idx);

			if (cqe64->op_own & MLX5_INLINE_SCATTER_32)
				err = mlx5_copy_to_send_wqe(mqp, wqe_ctr, cqe,
							    wc->byte_len);
			else if (cqe64->op_own & MLX5_INLINE_SCATTER_64)
				err = mlx5_copy_to_send_wqe(
				    mqp, wqe_ctr, cqe - 1, wc->byte_len);

			wc->wr_id = wq->wrid[idx];
			wc->status = err;
		}

		wq->tail = wq->wqe_head[idx] + 1;
		break;
	}
	case MLX5_CQE_RESP_WR_IMM:
	case MLX5_CQE_RESP_SEND:
	case MLX5_CQE_RESP_SEND_IMM:
	case MLX5_CQE_RESP_SEND_INV:
		srqn_uidx = be32toh(cqe64->srqn_uidx) & 0xffffff;
		err = get_cur_rsc(mctx, cqe_ver, qpn, srqn_uidx, cur_rsc,
				  cur_srq, &is_srq);
		if (unlikely(err))
			return CQ_POLL_ERR;

		if (lazy)
			cq->ibv_cq.status = handle_responder_lazy(cq, cqe64,
							      *cur_rsc,
							      is_srq ? *cur_srq : NULL);
		else
			wc->status = handle_responder(wc, cqe64, *cur_rsc,
					      is_srq ? *cur_srq : NULL);
		break;
	case MLX5_CQE_RESIZE_CQ:
		break;
	case MLX5_CQE_REQ_ERR:
	case MLX5_CQE_RESP_ERR:
		srqn_uidx = be32toh(cqe64->srqn_uidx) & 0xffffff;
		ecqe = (struct mlx5_err_cqe *)cqe64;
		{
			enum ibv_wc_status *pstatus = lazy ? &cq->ibv_cq.status : &wc->status;

			*pstatus = mlx5_handle_error_cqe(ecqe);
		}

		if (!lazy)
			wc->vendor_err = ecqe->vendor_err_synd;

		if (unlikely(ecqe->syndrome != MLX5_CQE_SYNDROME_WR_FLUSH_ERR &&
			     ecqe->syndrome != MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR)) {
			FILE *fp = mctx->dbg_fp;
			fprintf(fp, PFX "%s: got completion with error:\n",
				mctx->hostname);
			dump_cqe(fp, ecqe);
			if (mlx5_freeze_on_error_cqe) {
				fprintf(fp, PFX "freezing at poll cq...");
				while (1)
					sleep(10);
			}
		}

		if (opcode == MLX5_CQE_REQ_ERR) {
			mqp = get_req_context(mctx, cur_rsc,
					      (cqe_ver ? srqn_uidx : qpn), cqe_ver);
			if (unlikely(!mqp))
				return CQ_POLL_ERR;
			wq = &mqp->sq;
			wqe_ctr = be16toh(cqe64->wqe_counter);
			idx = wqe_ctr & (wq->wqe_cnt - 1);
			if (lazy)
				cq->ibv_cq.wr_id = wq->wrid[idx];
			else
				wc->wr_id = wq->wrid[idx];
			wq->tail = wq->wqe_head[idx] + 1;
		} else {
			err = get_cur_rsc(mctx, cqe_ver, qpn, srqn_uidx,
					  cur_rsc, cur_srq, &is_srq);
			if (unlikely(err))
				return CQ_POLL_ERR;

			if (is_srq) {
				wqe_ctr = be16toh(cqe64->wqe_counter);
				if (lazy)
					cq->ibv_cq.wr_id = (*cur_srq)->wrid[wqe_ctr];
				else
					wc->wr_id = (*cur_srq)->wrid[wqe_ctr];
				mlx5_free_srq_wqe(*cur_srq, wqe_ctr);
			} else {
				switch ((*cur_rsc)->type) {
				case MLX5_RSC_TYPE_RWQ:
					wq = &(rsc_to_mrwq(*cur_rsc)->rq);
					break;
				default:
					wq = &(rsc_to_mqp(*cur_rsc)->rq);
					break;
				}

				if (lazy)
					cq->ibv_cq.wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
				else
					wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
				++wq->tail;
			}
		}
		break;
	}

	return CQ_OK;
}

static inline int mlx5_parse_lazy_cqe(struct mlx5_cq *cq,
				      struct mlx5_cqe64 *cqe64,
				      void *cqe, int cqe_ver)
				      ALWAYS_INLINE;
static inline int mlx5_parse_lazy_cqe(struct mlx5_cq *cq,
				      struct mlx5_cqe64 *cqe64,
				      void *cqe, int cqe_ver)
{
	return mlx5_parse_cqe(cq, cqe64, cqe, &cq->cur_rsc, &cq->cur_srq, NULL, cqe_ver, 1);
}

static inline int mlx5_poll_one(struct mlx5_cq *cq,
				struct mlx5_resource **cur_rsc,
				struct mlx5_srq **cur_srq,
				struct ibv_wc *wc, int cqe_ver)
				ALWAYS_INLINE;
static inline int mlx5_poll_one(struct mlx5_cq *cq,
				struct mlx5_resource **cur_rsc,
				struct mlx5_srq **cur_srq,
				struct ibv_wc *wc, int cqe_ver)
{
	struct mlx5_cqe64 *cqe64;
	void *cqe;
	int err;

	err = mlx5_get_next_cqe(cq, &cqe64, &cqe);
	if (err == CQ_EMPTY)
		return err;

	return mlx5_parse_cqe(cq, cqe64, cqe, cur_rsc, cur_srq, wc, cqe_ver, 0);
}

static inline int poll_cq(struct ibv_cq *ibcq, int ne,
		      struct ibv_wc *wc, int cqe_ver)
		      ALWAYS_INLINE;
static inline int poll_cq(struct ibv_cq *ibcq, int ne,
		      struct ibv_wc *wc, int cqe_ver)
{
	struct mlx5_cq *cq = to_mcq(ibcq);
	struct mlx5_resource *rsc = NULL;
	struct mlx5_srq *srq = NULL;
	int npolled;
	int err = CQ_OK;

	if (cq->stall_enable) {
		if (cq->stall_adaptive_enable) {
			if (cq->stall_last_count)
				mlx5_stall_cycles_poll_cq(cq->stall_last_count + cq->stall_cycles);
		} else if (cq->stall_next_poll) {
			cq->stall_next_poll = 0;
			mlx5_stall_poll_cq();
		}
	}

	mlx5_spin_lock(&cq->lock);

	for (npolled = 0; npolled < ne; ++npolled) {
		err = mlx5_poll_one(cq, &rsc, &srq, wc + npolled, cqe_ver);
		if (err != CQ_OK)
			break;
	}

	update_cons_index(cq);

	mlx5_spin_unlock(&cq->lock);

	if (cq->stall_enable) {
		if (cq->stall_adaptive_enable) {
			if (npolled == 0) {
				cq->stall_cycles = max(cq->stall_cycles-mlx5_stall_cq_dec_step,
						       mlx5_stall_cq_poll_min);
				mlx5_get_cycles(&cq->stall_last_count);
			} else if (npolled < ne) {
				cq->stall_cycles = min(cq->stall_cycles+mlx5_stall_cq_inc_step,
						       mlx5_stall_cq_poll_max);
				mlx5_get_cycles(&cq->stall_last_count);
			} else {
				cq->stall_cycles = max(cq->stall_cycles-mlx5_stall_cq_dec_step,
						       mlx5_stall_cq_poll_min);
				cq->stall_last_count = 0;
			}
		} else if (err == CQ_EMPTY) {
			cq->stall_next_poll = 1;
		}
	}

	return err == CQ_POLL_ERR ? err : npolled;
}

enum  polling_mode {
	POLLING_MODE_NO_STALL,
	POLLING_MODE_STALL,
	POLLING_MODE_STALL_ADAPTIVE
};

static inline void _mlx5_end_poll(struct ibv_cq_ex *ibcq,
				  int lock, enum polling_mode stall)
				  ALWAYS_INLINE;
static inline void _mlx5_end_poll(struct ibv_cq_ex *ibcq,
				  int lock, enum polling_mode stall)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	update_cons_index(cq);

	if (lock)
		mlx5_spin_unlock(&cq->lock);

	if (stall) {
		if (stall == POLLING_MODE_STALL_ADAPTIVE) {
			if (!(cq->flags & MLX5_CQ_FLAGS_FOUND_CQES)) {
				cq->stall_cycles = max(cq->stall_cycles - mlx5_stall_cq_dec_step,
						       mlx5_stall_cq_poll_min);
				mlx5_get_cycles(&cq->stall_last_count);
			} else if (cq->flags & MLX5_CQ_FLAGS_EMPTY_DURING_POLL) {
				cq->stall_cycles = min(cq->stall_cycles + mlx5_stall_cq_inc_step,
						       mlx5_stall_cq_poll_max);
				mlx5_get_cycles(&cq->stall_last_count);
			} else {
				cq->stall_cycles = max(cq->stall_cycles - mlx5_stall_cq_dec_step,
						       mlx5_stall_cq_poll_min);
				cq->stall_last_count = 0;
			}
		} else if (!(cq->flags & MLX5_CQ_FLAGS_FOUND_CQES)) {
			cq->stall_next_poll = 1;
		}

		cq->flags &= ~(MLX5_CQ_FLAGS_FOUND_CQES | MLX5_CQ_FLAGS_EMPTY_DURING_POLL);
	}
}

static inline int mlx5_start_poll(struct ibv_cq_ex *ibcq, struct ibv_poll_cq_attr *attr,
				  int lock, enum polling_mode stall, int cqe_version)
				  ALWAYS_INLINE;
static inline int mlx5_start_poll(struct ibv_cq_ex *ibcq, struct ibv_poll_cq_attr *attr,
				  int lock, enum polling_mode stall, int cqe_version)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	struct mlx5_cqe64 *cqe64;
	void *cqe;
	int err;

	if (unlikely(attr->comp_mask))
		return EINVAL;

	if (stall) {
		if (stall == POLLING_MODE_STALL_ADAPTIVE) {
			if (cq->stall_last_count)
				mlx5_stall_cycles_poll_cq(cq->stall_last_count + cq->stall_cycles);
		} else if (cq->stall_next_poll) {
			cq->stall_next_poll = 0;
			mlx5_stall_poll_cq();
		}
	}

	if (lock)
		mlx5_spin_lock(&cq->lock);

	cq->cur_rsc = NULL;
	cq->cur_srq = NULL;

	err = mlx5_get_next_cqe(cq, &cqe64, &cqe);
	if (err == CQ_EMPTY) {
		if (lock)
			mlx5_spin_unlock(&cq->lock);

		if (stall) {
			if (stall == POLLING_MODE_STALL_ADAPTIVE) {
				cq->stall_cycles = max(cq->stall_cycles - mlx5_stall_cq_dec_step,
						mlx5_stall_cq_poll_min);
				mlx5_get_cycles(&cq->stall_last_count);
			} else {
				cq->stall_next_poll = 1;
			}
		}

		return ENOENT;
	}

	if (stall)
		cq->flags |= MLX5_CQ_FLAGS_FOUND_CQES;

	err = mlx5_parse_lazy_cqe(cq, cqe64, cqe, cqe_version);
	if (lock && err)
		mlx5_spin_unlock(&cq->lock);

	if (stall && err) {
		if (stall == POLLING_MODE_STALL_ADAPTIVE) {
			cq->stall_cycles = max(cq->stall_cycles - mlx5_stall_cq_dec_step,
						mlx5_stall_cq_poll_min);
			cq->stall_last_count = 0;
		}

		cq->flags &= ~(MLX5_CQ_FLAGS_FOUND_CQES);
	}

	return err;
}

static inline int mlx5_next_poll(struct ibv_cq_ex *ibcq,
				 enum polling_mode stall, int cqe_version)
				 ALWAYS_INLINE;
static inline int mlx5_next_poll(struct ibv_cq_ex *ibcq,
				 enum polling_mode stall,
				 int cqe_version)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	struct mlx5_cqe64 *cqe64;
	void *cqe;
	int err;

	err = mlx5_get_next_cqe(cq, &cqe64, &cqe);
	if (err == CQ_EMPTY) {
		if (stall == POLLING_MODE_STALL_ADAPTIVE)
			cq->flags |= MLX5_CQ_FLAGS_EMPTY_DURING_POLL;

		return ENOENT;
	}

	return mlx5_parse_lazy_cqe(cq, cqe64, cqe, cqe_version);
}

static inline int mlx5_next_poll_adaptive_v0(struct ibv_cq_ex *ibcq)
{
	return mlx5_next_poll(ibcq, POLLING_MODE_STALL_ADAPTIVE, 0);
}

static inline int mlx5_next_poll_adaptive_v1(struct ibv_cq_ex *ibcq)
{
	return mlx5_next_poll(ibcq, POLLING_MODE_STALL_ADAPTIVE, 1);
}

static inline int mlx5_next_poll_v0(struct ibv_cq_ex *ibcq)
{
	return mlx5_next_poll(ibcq, 0, 0);
}

static inline int mlx5_next_poll_v1(struct ibv_cq_ex *ibcq)
{
	return mlx5_next_poll(ibcq, 0, 1);
}

static inline int mlx5_start_poll_v0(struct ibv_cq_ex *ibcq,
				     struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 0, 0, 0);
}

static inline int mlx5_start_poll_v1(struct ibv_cq_ex *ibcq,
				     struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 0, 0, 1);
}

static inline int mlx5_start_poll_v0_lock(struct ibv_cq_ex *ibcq,
					  struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 1, 0, 0);
}

static inline int mlx5_start_poll_v1_lock(struct ibv_cq_ex *ibcq,
					  struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 1, 0, 1);
}

static inline int mlx5_start_poll_adaptive_stall_v0_lock(struct ibv_cq_ex *ibcq,
							 struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 1, POLLING_MODE_STALL_ADAPTIVE, 0);
}

static inline int mlx5_start_poll_stall_v0_lock(struct ibv_cq_ex *ibcq,
						struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 1, POLLING_MODE_STALL, 0);
}

static inline int mlx5_start_poll_adaptive_stall_v1_lock(struct ibv_cq_ex *ibcq,
							 struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 1, POLLING_MODE_STALL_ADAPTIVE, 1);
}

static inline int mlx5_start_poll_stall_v1_lock(struct ibv_cq_ex *ibcq,
						struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 1, POLLING_MODE_STALL, 1);
}

static inline int mlx5_start_poll_stall_v0(struct ibv_cq_ex *ibcq,
					   struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 0, POLLING_MODE_STALL, 0);
}

static inline int mlx5_start_poll_adaptive_stall_v0(struct ibv_cq_ex *ibcq,
						    struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 0, POLLING_MODE_STALL_ADAPTIVE, 0);
}

static inline int mlx5_start_poll_adaptive_stall_v1(struct ibv_cq_ex *ibcq,
						    struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 0, POLLING_MODE_STALL_ADAPTIVE, 1);
}

static inline int mlx5_start_poll_stall_v1(struct ibv_cq_ex *ibcq,
					   struct ibv_poll_cq_attr *attr)
{
	return mlx5_start_poll(ibcq, attr, 0, POLLING_MODE_STALL, 1);
}

static inline void mlx5_end_poll_adaptive_stall_lock(struct ibv_cq_ex *ibcq)
{
	_mlx5_end_poll(ibcq, 1, POLLING_MODE_STALL_ADAPTIVE);
}

static inline void mlx5_end_poll_stall_lock(struct ibv_cq_ex *ibcq)
{
	_mlx5_end_poll(ibcq, 1, POLLING_MODE_STALL);
}

static inline void mlx5_end_poll_adaptive_stall(struct ibv_cq_ex *ibcq)
{
	_mlx5_end_poll(ibcq, 0, POLLING_MODE_STALL_ADAPTIVE);
}

static inline void mlx5_end_poll_stall(struct ibv_cq_ex *ibcq)
{
	_mlx5_end_poll(ibcq, 0, POLLING_MODE_STALL);
}

static inline void mlx5_end_poll(struct ibv_cq_ex *ibcq)
{
	_mlx5_end_poll(ibcq, 0, 0);
}

static inline void mlx5_end_poll_lock(struct ibv_cq_ex *ibcq)
{
	_mlx5_end_poll(ibcq, 1, 0);
}

int mlx5_poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
	return poll_cq(ibcq, ne, wc, 0);
}

int mlx5_poll_cq_v1(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
	return poll_cq(ibcq, ne, wc, 1);
}

static inline enum ibv_wc_opcode mlx5_cq_read_wc_opcode(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	switch (mlx5dv_get_cqe_opcode(cq->cqe64)) {
	case MLX5_CQE_RESP_WR_IMM:
		return IBV_WC_RECV_RDMA_WITH_IMM;
	case MLX5_CQE_RESP_SEND:
	case MLX5_CQE_RESP_SEND_IMM:
	case MLX5_CQE_RESP_SEND_INV:
		return IBV_WC_RECV;
	case MLX5_CQE_REQ:
		switch (be32toh(cq->cqe64->sop_drop_qpn) >> 24) {
		case MLX5_OPCODE_RDMA_WRITE_IMM:
		case MLX5_OPCODE_RDMA_WRITE:
			return IBV_WC_RDMA_WRITE;
		case MLX5_OPCODE_SEND_IMM:
		case MLX5_OPCODE_SEND:
		case MLX5_OPCODE_SEND_INVAL:
			return IBV_WC_SEND;
		case MLX5_OPCODE_RDMA_READ:
			return IBV_WC_RDMA_READ;
		case MLX5_OPCODE_ATOMIC_CS:
			return IBV_WC_COMP_SWAP;
		case MLX5_OPCODE_ATOMIC_FA:
			return IBV_WC_FETCH_ADD;
		case MLX5_OPCODE_UMR:
			return cq->umr_opcode;
		case MLX5_OPCODE_TSO:
			return IBV_WC_TSO;
		}
	}

#ifdef MLX5_DEBUG
{
	struct mlx5_context *ctx = to_mctx(ibcq->context);

	mlx5_dbg(ctx->dbg_fp, MLX5_DBG_CQ_CQE, "un-expected opcode in cqe\n");
}
#endif
	return 0;
}

static inline uint32_t mlx5_cq_read_wc_qp_num(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe64->sop_drop_qpn) & 0xffffff;
}

static inline int mlx5_cq_read_wc_flags(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	int wc_flags = 0;

	if (cq->flags & MLX5_CQ_FLAGS_RX_CSUM_VALID)
		wc_flags = (!!(cq->cqe64->hds_ip_ext & MLX5_CQE_L4_OK) &
				 !!(cq->cqe64->hds_ip_ext & MLX5_CQE_L3_OK) &
				 (get_cqe_l3_hdr_type(cq->cqe64) ==
				  MLX5_CQE_L3_HDR_TYPE_IPV4)) <<
				IBV_WC_IP_CSUM_OK_SHIFT;

	switch (mlx5dv_get_cqe_opcode(cq->cqe64)) {
	case MLX5_CQE_RESP_WR_IMM:
	case MLX5_CQE_RESP_SEND_IMM:
		wc_flags	|= IBV_WC_WITH_IMM;
		break;
	case MLX5_CQE_RESP_SEND_INV:
		wc_flags |= IBV_WC_WITH_INV;
		break;
	}

	wc_flags |= ((be32toh(cq->cqe64->flags_rqpn) >> 28) & 3) ? IBV_WC_GRH : 0;
	return wc_flags;
}

static inline uint32_t mlx5_cq_read_wc_byte_len(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe64->byte_cnt);
}

static inline uint32_t mlx5_cq_read_wc_vendor_err(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	struct mlx5_err_cqe *ecqe = (struct mlx5_err_cqe *)cq->cqe64;

	return ecqe->vendor_err_synd;
}

static inline uint32_t mlx5_cq_read_wc_imm_data(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	switch (mlx5dv_get_cqe_opcode(cq->cqe64)) {
	case MLX5_CQE_RESP_SEND_INV:
		return be32toh(cq->cqe64->imm_inval_pkey);
	default:
		return cq->cqe64->imm_inval_pkey;
	}
}

static inline uint32_t mlx5_cq_read_wc_slid(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return (uint32_t)be16toh(cq->cqe64->slid);
}

static inline uint8_t mlx5_cq_read_wc_sl(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return (be32toh(cq->cqe64->flags_rqpn) >> 24) & 0xf;
}

static inline uint32_t mlx5_cq_read_wc_src_qp(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe64->flags_rqpn) & 0xffffff;
}

static inline uint8_t mlx5_cq_read_wc_dlid_path_bits(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return cq->cqe64->ml_path & 0x7f;
}

static inline uint64_t mlx5_cq_read_wc_completion_ts(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be64toh(cq->cqe64->timestamp);
}

static inline uint16_t mlx5_cq_read_wc_cvlan(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be16toh(cq->cqe64->vlan_info);
}

static inline uint32_t mlx5_cq_read_flow_tag(struct ibv_cq_ex *ibcq)
{
	struct mlx5_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe64->sop_drop_qpn) & MLX5_FLOW_TAG_MASK;
}

#define BIT(i) (1UL << (i))

#define SINGLE_THREADED BIT(0)
#define STALL BIT(1)
#define V1 BIT(2)
#define ADAPTIVE BIT(3)

#define mlx5_start_poll_name(cqe_ver, lock, stall, adaptive) \
	mlx5_start_poll##adaptive##stall##cqe_ver##lock
#define mlx5_next_poll_name(cqe_ver, adaptive) \
	mlx5_next_poll##adaptive##cqe_ver
#define mlx5_end_poll_name(lock, stall, adaptive) \
	mlx5_end_poll##adaptive##stall##lock

#define POLL_FN_ENTRY(cqe_ver, lock, stall, adaptive) { \
		.start_poll = &mlx5_start_poll_name(cqe_ver, lock, stall, adaptive), \
		.next_poll = &mlx5_next_poll_name(cqe_ver, adaptive), \
		.end_poll = &mlx5_end_poll_name(lock, stall, adaptive), \
	}

static const struct op
{
	int (*start_poll)(struct ibv_cq_ex *ibcq, struct ibv_poll_cq_attr *attr);
	int (*next_poll)(struct ibv_cq_ex *ibcq);
	void (*end_poll)(struct ibv_cq_ex *ibcq);
} ops[ADAPTIVE + V1 + STALL + SINGLE_THREADED + 1] = {
	[V1] =  POLL_FN_ENTRY(_v1, _lock, , ),
	[0] =  POLL_FN_ENTRY(_v0, _lock, , ),
	[V1 | SINGLE_THREADED] =  POLL_FN_ENTRY(_v1, , , ),
	[SINGLE_THREADED] =  POLL_FN_ENTRY(_v0, , , ),
	[V1 | STALL] =  POLL_FN_ENTRY(_v1, _lock, _stall, ),
	[STALL] =  POLL_FN_ENTRY(_v0, _lock, _stall, ),
	[V1 | SINGLE_THREADED | STALL] =  POLL_FN_ENTRY(_v1, , _stall, ),
	[SINGLE_THREADED | STALL] =  POLL_FN_ENTRY(_v0, , _stall, ),
	[V1 | STALL | ADAPTIVE] =  POLL_FN_ENTRY(_v1, _lock, _stall, _adaptive),
	[STALL | ADAPTIVE] =  POLL_FN_ENTRY(_v0, _lock, _stall, _adaptive),
	[V1 | SINGLE_THREADED | STALL | ADAPTIVE] =  POLL_FN_ENTRY(_v1, , _stall, _adaptive),
	[SINGLE_THREADED | STALL | ADAPTIVE] =  POLL_FN_ENTRY(_v0, , _stall, _adaptive),
};

void mlx5_cq_fill_pfns(struct mlx5_cq *cq, const struct ibv_cq_init_attr_ex *cq_attr)
{
	struct mlx5_context *mctx = to_mctx(ibv_cq_ex_to_cq(&cq->ibv_cq)->context);
	const struct op *poll_ops = &ops[((cq->stall_enable && cq->stall_adaptive_enable) ? ADAPTIVE : 0) |
					 (mctx->cqe_version ? V1 : 0) |
					 (cq->flags & MLX5_CQ_FLAGS_SINGLE_THREADED ?
						      SINGLE_THREADED : 0) |
					 (cq->stall_enable ? STALL : 0)];

	cq->ibv_cq.start_poll = poll_ops->start_poll;
	cq->ibv_cq.next_poll = poll_ops->next_poll;
	cq->ibv_cq.end_poll = poll_ops->end_poll;

	cq->ibv_cq.read_opcode = mlx5_cq_read_wc_opcode;
	cq->ibv_cq.read_vendor_err = mlx5_cq_read_wc_vendor_err;
	cq->ibv_cq.read_wc_flags = mlx5_cq_read_wc_flags;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_BYTE_LEN)
		cq->ibv_cq.read_byte_len = mlx5_cq_read_wc_byte_len;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_IMM)
		cq->ibv_cq.read_imm_data = mlx5_cq_read_wc_imm_data;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_QP_NUM)
		cq->ibv_cq.read_qp_num = mlx5_cq_read_wc_qp_num;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_SRC_QP)
		cq->ibv_cq.read_src_qp = mlx5_cq_read_wc_src_qp;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_SLID)
		cq->ibv_cq.read_slid = mlx5_cq_read_wc_slid;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_SL)
		cq->ibv_cq.read_sl = mlx5_cq_read_wc_sl;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_DLID_PATH_BITS)
		cq->ibv_cq.read_dlid_path_bits = mlx5_cq_read_wc_dlid_path_bits;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_COMPLETION_TIMESTAMP)
		cq->ibv_cq.read_completion_ts = mlx5_cq_read_wc_completion_ts;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_CVLAN)
		cq->ibv_cq.read_cvlan = mlx5_cq_read_wc_cvlan;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_FLOW_TAG)
		cq->ibv_cq.read_flow_tag = mlx5_cq_read_flow_tag;
}

int mlx5_arm_cq(struct ibv_cq *ibvcq, int solicited)
{
	struct mlx5_cq *cq = to_mcq(ibvcq);
	struct mlx5_context *ctx = to_mctx(ibvcq->context);
	uint32_t doorbell[2];
	uint32_t sn;
	uint32_t ci;
	uint32_t cmd;

	sn  = cq->arm_sn & 3;
	ci  = cq->cons_index & 0xffffff;
	cmd = solicited ? MLX5_CQ_DB_REQ_NOT_SOL : MLX5_CQ_DB_REQ_NOT;

	cq->dbrec[MLX5_CQ_ARM_DB] = htobe32(sn << 28 | cmd | ci);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI WC MMIO.
	 */
	mmio_wc_start();

	doorbell[0] = htobe32(sn << 28 | cmd | ci);
	doorbell[1] = htobe32(cq->cqn);

	mlx5_write64(doorbell, ctx->uar[0] + MLX5_CQ_DOORBELL, &ctx->lock32);

	mmio_flush_writes();

	return 0;
}

void mlx5_cq_event(struct ibv_cq *cq)
{
	to_mcq(cq)->arm_sn++;
}

static int is_equal_rsn(struct mlx5_cqe64 *cqe64, uint32_t rsn)
{
	return rsn == (be32toh(cqe64->sop_drop_qpn) & 0xffffff);
}

static inline int is_equal_uidx(struct mlx5_cqe64 *cqe64, uint32_t uidx)
{
	return uidx == (be32toh(cqe64->srqn_uidx) & 0xffffff);
}

static inline int is_responder(uint8_t opcode)
{
	switch (opcode) {
	case MLX5_CQE_RESP_WR_IMM:
	case MLX5_CQE_RESP_SEND:
	case MLX5_CQE_RESP_SEND_IMM:
	case MLX5_CQE_RESP_SEND_INV:
	case MLX5_CQE_RESP_ERR:
		return 1;
	}

	return 0;
}

static inline int free_res_cqe(struct mlx5_cqe64 *cqe64, uint32_t rsn,
			       struct mlx5_srq *srq, int cqe_version)
{
	if (cqe_version) {
		if (is_equal_uidx(cqe64, rsn)) {
			if (srq && is_responder(mlx5dv_get_cqe_opcode(cqe64)))
				mlx5_free_srq_wqe(srq,
						  be16toh(cqe64->wqe_counter));
			return 1;
		}
	} else {
		if (is_equal_rsn(cqe64, rsn)) {
			if (srq && (be32toh(cqe64->srqn_uidx) & 0xffffff))
				mlx5_free_srq_wqe(srq,
						  be16toh(cqe64->wqe_counter));
			return 1;
		}
	}

	return 0;
}

void __mlx5_cq_clean(struct mlx5_cq *cq, uint32_t rsn, struct mlx5_srq *srq)
{
	uint32_t prod_index;
	int nfreed = 0;
	struct mlx5_cqe64 *cqe64, *dest64;
	void *cqe, *dest;
	uint8_t owner_bit;
	int cqe_version;

	if (!cq || cq->flags & MLX5_CQ_FLAGS_DV_OWNED)
		return;

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->cons_index; get_sw_cqe(cq, prod_index); ++prod_index)
		if (prod_index == cq->cons_index + cq->ibv_cq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	cqe_version = (to_mctx(cq->ibv_cq.context))->cqe_version;
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
		cqe64 = (cq->cqe_sz == 64) ? cqe : cqe + 64;
		if (free_res_cqe(cqe64, rsn, srq, cqe_version)) {
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe);
			dest64 = (cq->cqe_sz == 64) ? dest : dest + 64;
			owner_bit = dest64->op_own & MLX5_CQE_OWNER_MASK;
			memcpy(dest, cqe, cq->cqe_sz);
			dest64->op_own = owner_bit |
				(dest64->op_own & ~MLX5_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		udma_to_device_barrier();
		update_cons_index(cq);
	}
}

void mlx5_cq_clean(struct mlx5_cq *cq, uint32_t qpn, struct mlx5_srq *srq)
{
	mlx5_spin_lock(&cq->lock);
	__mlx5_cq_clean(cq, qpn, srq);
	mlx5_spin_unlock(&cq->lock);
}

static uint8_t sw_ownership_bit(int n, int nent)
{
	return (n & nent) ? 1 : 0;
}

static int is_hw(uint8_t own, int n, int mask)
{
	return (own & MLX5_CQE_OWNER_MASK) ^ !!(n & (mask + 1));
}

void mlx5_cq_resize_copy_cqes(struct mlx5_cq *cq)
{
	struct mlx5_cqe64 *scqe64;
	struct mlx5_cqe64 *dcqe64;
	void *start_cqe;
	void *scqe;
	void *dcqe;
	int ssize;
	int dsize;
	int i;
	uint8_t sw_own;

	ssize = cq->cqe_sz;
	dsize = cq->resize_cqe_sz;

	i = cq->cons_index;
	scqe = get_buf_cqe(cq->active_buf, i & cq->active_cqes, ssize);
	scqe64 = ssize == 64 ? scqe : scqe + 64;
	start_cqe = scqe;
	if (is_hw(scqe64->op_own, i, cq->active_cqes)) {
		fprintf(stderr, "expected cqe in sw ownership\n");
		return;
	}

	while ((scqe64->op_own >> 4) != MLX5_CQE_RESIZE_CQ) {
		dcqe = get_buf_cqe(cq->resize_buf, (i + 1) & (cq->resize_cqes - 1), dsize);
		dcqe64 = dsize == 64 ? dcqe : dcqe + 64;
		sw_own = sw_ownership_bit(i + 1, cq->resize_cqes);
		memcpy(dcqe, scqe, ssize);
		dcqe64->op_own = (dcqe64->op_own & ~MLX5_CQE_OWNER_MASK) | sw_own;

		++i;
		scqe = get_buf_cqe(cq->active_buf, i & cq->active_cqes, ssize);
		scqe64 = ssize == 64 ? scqe : scqe + 64;
		if (is_hw(scqe64->op_own, i, cq->active_cqes)) {
			fprintf(stderr, "expected cqe in sw ownership\n");
			return;
		}

		if (scqe == start_cqe) {
			fprintf(stderr, "resize CQ failed to get resize CQE\n");
			return;
		}
	}
	++cq->cons_index;
}

int mlx5_alloc_cq_buf(struct mlx5_context *mctx, struct mlx5_cq *cq,
		      struct mlx5_buf *buf, int nent, int cqe_sz)
{
	struct mlx5_cqe64 *cqe;
	int i;
	struct mlx5_device *dev = to_mdev(mctx->ibv_ctx.device);
	int ret;
	enum mlx5_alloc_type type;
	enum mlx5_alloc_type default_type = MLX5_ALLOC_TYPE_ANON;

	if (mlx5_use_huge("HUGE_CQ"))
		default_type = MLX5_ALLOC_TYPE_HUGE;

	mlx5_get_alloc_type(MLX5_CQ_PREFIX, &type, default_type);

	ret = mlx5_alloc_prefered_buf(mctx, buf,
				      align(nent * cqe_sz, dev->page_size),
				      dev->page_size,
				      type,
				      MLX5_CQ_PREFIX);

	if (ret)
		return -1;

	memset(buf->buf, 0, nent * cqe_sz);

	for (i = 0; i < nent; ++i) {
		cqe = buf->buf + i * cqe_sz;
		cqe += cqe_sz == 128 ? 1 : 0;
		cqe->op_own = MLX5_CQE_INVALID << 4;
	}

	return 0;
}

int mlx5_free_cq_buf(struct mlx5_context *ctx, struct mlx5_buf *buf)
{
	return mlx5_free_actual_buf(ctx, buf);
}
