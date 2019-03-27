/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems.  All rights reserved.
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

#include <infiniband/opcode.h>

#include "mlx4.h"
#include "doorbell.h"

enum {
	MLX4_CQ_DOORBELL			= 0x20
};

enum {
	CQ_OK					=  0,
	CQ_EMPTY				= -1,
	CQ_POLL_ERR				= -2
};

#define MLX4_CQ_DB_REQ_NOT_SOL			(1 << 24)
#define MLX4_CQ_DB_REQ_NOT			(2 << 24)

enum {
	MLX4_CQE_VLAN_PRESENT_MASK		= 1 << 29,
	MLX4_CQE_QPN_MASK			= 0xffffff,
};

enum {
	MLX4_CQE_OWNER_MASK			= 0x80,
	MLX4_CQE_IS_SEND_MASK			= 0x40,
	MLX4_CQE_OPCODE_MASK			= 0x1f
};

enum {
	MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX4_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX4_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX4_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX4_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX4_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

struct mlx4_err_cqe {
	uint32_t	vlan_my_qpn;
	uint32_t	reserved1[5];
	uint16_t	wqe_index;
	uint8_t		vendor_err;
	uint8_t		syndrome;
	uint8_t		reserved2[3];
	uint8_t		owner_sr_opcode;
};

static struct mlx4_cqe *get_cqe(struct mlx4_cq *cq, int entry)
{
	return cq->buf.buf + entry * cq->cqe_size;
}

static void *get_sw_cqe(struct mlx4_cq *cq, int n)
{
	struct mlx4_cqe *cqe = get_cqe(cq, n & cq->ibv_cq.cqe);
	struct mlx4_cqe *tcqe = cq->cqe_size == 64 ? cqe + 1 : cqe;

	return (!!(tcqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
		!!(n & (cq->ibv_cq.cqe + 1))) ? NULL : cqe;
}

static struct mlx4_cqe *next_cqe_sw(struct mlx4_cq *cq)
{
	return get_sw_cqe(cq, cq->cons_index);
}

static enum ibv_wc_status mlx4_handle_error_cqe(struct mlx4_err_cqe *cqe)
{
	if (cqe->syndrome == MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR)
		printf(PFX "local QP operation err "
		       "(QPN %06x, WQE index %x, vendor syndrome %02x, "
		       "opcode = %02x)\n",
		       htobe32(cqe->vlan_my_qpn), htobe32(cqe->wqe_index),
		       cqe->vendor_err,
		       cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);

	switch (cqe->syndrome) {
	case MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		return IBV_WC_LOC_LEN_ERR;
	case MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		return IBV_WC_LOC_QP_OP_ERR;
	case MLX4_CQE_SYNDROME_LOCAL_PROT_ERR:
		return IBV_WC_LOC_PROT_ERR;
	case MLX4_CQE_SYNDROME_WR_FLUSH_ERR:
		return IBV_WC_WR_FLUSH_ERR;
	case MLX4_CQE_SYNDROME_MW_BIND_ERR:
		return IBV_WC_MW_BIND_ERR;
	case MLX4_CQE_SYNDROME_BAD_RESP_ERR:
		return IBV_WC_BAD_RESP_ERR;
	case MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		return IBV_WC_LOC_ACCESS_ERR;
	case MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		return IBV_WC_REM_INV_REQ_ERR;
	case MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		return IBV_WC_REM_ACCESS_ERR;
	case MLX4_CQE_SYNDROME_REMOTE_OP_ERR:
		return IBV_WC_REM_OP_ERR;
	case MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		return IBV_WC_RETRY_EXC_ERR;
	case MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		return IBV_WC_RNR_RETRY_EXC_ERR;
	case MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		return IBV_WC_REM_ABORT_ERR;
	default:
		return IBV_WC_GENERAL_ERR;
	}
}

static inline void handle_good_req(struct ibv_wc *wc, struct mlx4_cqe *cqe)
{
	wc->wc_flags = 0;
	switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
	case MLX4_OPCODE_RDMA_WRITE_IMM:
		wc->wc_flags |= IBV_WC_WITH_IMM;
		SWITCH_FALLTHROUGH;
	case MLX4_OPCODE_RDMA_WRITE:
		wc->opcode    = IBV_WC_RDMA_WRITE;
		break;
	case MLX4_OPCODE_SEND_IMM:
		wc->wc_flags |= IBV_WC_WITH_IMM;
		SWITCH_FALLTHROUGH;
	case MLX4_OPCODE_SEND:
	case MLX4_OPCODE_SEND_INVAL:
		wc->opcode    = IBV_WC_SEND;
		break;
	case MLX4_OPCODE_RDMA_READ:
		wc->opcode    = IBV_WC_RDMA_READ;
		wc->byte_len  = be32toh(cqe->byte_cnt);
		break;
	case MLX4_OPCODE_ATOMIC_CS:
		wc->opcode    = IBV_WC_COMP_SWAP;
		wc->byte_len  = 8;
		break;
	case MLX4_OPCODE_ATOMIC_FA:
		wc->opcode    = IBV_WC_FETCH_ADD;
		wc->byte_len  = 8;
		break;
	case MLX4_OPCODE_LOCAL_INVAL:
		wc->opcode    = IBV_WC_LOCAL_INV;
		break;
	case MLX4_OPCODE_BIND_MW:
		wc->opcode    = IBV_WC_BIND_MW;
		break;
	default:
		/* assume it's a send completion */
		wc->opcode    = IBV_WC_SEND;
		break;
	}
}

static inline int mlx4_get_next_cqe(struct mlx4_cq *cq,
				    struct mlx4_cqe **pcqe)
				    ALWAYS_INLINE;
static inline int mlx4_get_next_cqe(struct mlx4_cq *cq,
				    struct mlx4_cqe **pcqe)
{
	struct mlx4_cqe *cqe;

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return CQ_EMPTY;

	if (cq->cqe_size == 64)
		++cqe;

	++cq->cons_index;

	VALGRIND_MAKE_MEM_DEFINED(cqe, sizeof *cqe);

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	udma_from_device_barrier();

	*pcqe = cqe;

	return CQ_OK;
}

static inline int mlx4_parse_cqe(struct mlx4_cq *cq,
					struct mlx4_cqe *cqe,
					struct mlx4_qp **cur_qp,
					struct ibv_wc *wc, int lazy)
					ALWAYS_INLINE;
static inline int mlx4_parse_cqe(struct mlx4_cq *cq,
					struct mlx4_cqe *cqe,
					struct mlx4_qp **cur_qp,
					struct ibv_wc *wc, int lazy)
{
	struct mlx4_wq *wq;
	struct mlx4_srq *srq;
	uint32_t qpn;
	uint32_t g_mlpath_rqpn;
	uint64_t *pwr_id;
	uint16_t wqe_index;
	struct mlx4_err_cqe *ecqe;
	struct mlx4_context *mctx;
	int is_error;
	int is_send;
	enum ibv_wc_status *pstatus;

	mctx = to_mctx(cq->ibv_cq.context);
	qpn = be32toh(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK;
	if (lazy) {
		cq->cqe = cqe;
		cq->flags &= (~MLX4_CQ_FLAGS_RX_CSUM_VALID);
	} else
		wc->qp_num = qpn;

	is_send  = cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;
	is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
		MLX4_CQE_OPCODE_ERROR;

	if ((qpn & MLX4_XRC_QPN_BIT) && !is_send) {
		/*
		 * We do not have to take the XSRQ table lock here,
		 * because CQs will be locked while SRQs are removed
		 * from the table.
		 */
		srq = mlx4_find_xsrq(&mctx->xsrq_table,
				     be32toh(cqe->g_mlpath_rqpn) & MLX4_CQE_QPN_MASK);
		if (!srq)
			return CQ_POLL_ERR;
	} else {
		if (!*cur_qp || (qpn != (*cur_qp)->verbs_qp.qp.qp_num)) {
			/*
			 * We do not have to take the QP table lock here,
			 * because CQs will be locked while QPs are removed
			 * from the table.
			 */
			*cur_qp = mlx4_find_qp(mctx, qpn);
			if (!*cur_qp)
				return CQ_POLL_ERR;
		}
		srq = ((*cur_qp)->verbs_qp.qp.srq) ? to_msrq((*cur_qp)->verbs_qp.qp.srq) : NULL;
	}

	pwr_id = lazy ? &cq->ibv_cq.wr_id : &wc->wr_id;
	if (is_send) {
		wq = &(*cur_qp)->sq;
		wqe_index = be16toh(cqe->wqe_index);
		wq->tail += (uint16_t) (wqe_index - (uint16_t) wq->tail);
		*pwr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	} else if (srq) {
		wqe_index = be16toh(cqe->wqe_index);
		*pwr_id = srq->wrid[wqe_index];
		mlx4_free_srq_wqe(srq, wqe_index);
	} else {
		wq = &(*cur_qp)->rq;
		*pwr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	}

	pstatus = lazy ? &cq->ibv_cq.status : &wc->status;
	if (is_error) {
		ecqe = (struct mlx4_err_cqe *)cqe;
		*pstatus = mlx4_handle_error_cqe(ecqe);
		if (!lazy)
			wc->vendor_err = ecqe->vendor_err;
		return CQ_OK;
	}

	*pstatus = IBV_WC_SUCCESS;
	if (lazy) {
		if (!is_send)
			if ((*cur_qp) && ((*cur_qp)->qp_cap_cache & MLX4_RX_CSUM_VALID))
				cq->flags |= MLX4_CQ_FLAGS_RX_CSUM_VALID;
	} else if (is_send) {
		handle_good_req(wc, cqe);
	} else {
		wc->byte_len = be32toh(cqe->byte_cnt);

		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			wc->opcode   = IBV_WC_RECV_RDMA_WITH_IMM;
			wc->wc_flags = IBV_WC_WITH_IMM;
			wc->imm_data = cqe->immed_rss_invalid;
			break;
		case MLX4_RECV_OPCODE_SEND_INVAL:
			wc->opcode   = IBV_WC_RECV;
			wc->wc_flags |= IBV_WC_WITH_INV;
			wc->imm_data = be32toh(cqe->immed_rss_invalid);
			break;
		case MLX4_RECV_OPCODE_SEND:
			wc->opcode   = IBV_WC_RECV;
			wc->wc_flags = 0;
			break;
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc->opcode   = IBV_WC_RECV;
			wc->wc_flags = IBV_WC_WITH_IMM;
			wc->imm_data = cqe->immed_rss_invalid;
			break;
		}

		wc->slid	   = be16toh(cqe->rlid);
		g_mlpath_rqpn	   = be32toh(cqe->g_mlpath_rqpn);
		wc->src_qp	   = g_mlpath_rqpn & 0xffffff;
		wc->dlid_path_bits = (g_mlpath_rqpn >> 24) & 0x7f;
		wc->wc_flags	  |= g_mlpath_rqpn & 0x80000000 ? IBV_WC_GRH : 0;
		wc->pkey_index     = be32toh(cqe->immed_rss_invalid) & 0x7f;
		/* When working with xrc srqs, don't have qp to check link layer.
		* Using IB SL, should consider Roce. (TBD)
		*/
		if ((*cur_qp) && (*cur_qp)->link_layer == IBV_LINK_LAYER_ETHERNET)
			wc->sl	   = be16toh(cqe->sl_vid) >> 13;
		else
			wc->sl	   = be16toh(cqe->sl_vid) >> 12;

		if ((*cur_qp) && ((*cur_qp)->qp_cap_cache & MLX4_RX_CSUM_VALID)) {
			wc->wc_flags |= ((cqe->status & htobe32(MLX4_CQE_STATUS_IPV4_CSUM_OK)) ==
				 htobe32(MLX4_CQE_STATUS_IPV4_CSUM_OK)) <<
				IBV_WC_IP_CSUM_OK_SHIFT;
		}
	}

	return CQ_OK;
}

static inline int mlx4_parse_lazy_cqe(struct mlx4_cq *cq,
				      struct mlx4_cqe *cqe)
				      ALWAYS_INLINE;
static inline int mlx4_parse_lazy_cqe(struct mlx4_cq *cq,
				      struct mlx4_cqe *cqe)
{
	return mlx4_parse_cqe(cq, cqe, &cq->cur_qp, NULL, 1);
}

static inline int mlx4_poll_one(struct mlx4_cq *cq,
			 struct mlx4_qp **cur_qp,
			 struct ibv_wc *wc)
			 ALWAYS_INLINE;
static inline int mlx4_poll_one(struct mlx4_cq *cq,
			 struct mlx4_qp **cur_qp,
			 struct ibv_wc *wc)
{
	struct mlx4_cqe *cqe;
	int err;

	err = mlx4_get_next_cqe(cq, &cqe);
	if (err == CQ_EMPTY)
		return err;

	return mlx4_parse_cqe(cq, cqe, cur_qp, wc, 0);
}

int mlx4_poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
	struct mlx4_cq *cq = to_mcq(ibcq);
	struct mlx4_qp *qp = NULL;
	int npolled;
	int err = CQ_OK;

	pthread_spin_lock(&cq->lock);

	for (npolled = 0; npolled < ne; ++npolled) {
		err = mlx4_poll_one(cq, &qp, wc + npolled);
		if (err != CQ_OK)
			break;
	}

	if (npolled || err == CQ_POLL_ERR)
		mlx4_update_cons_index(cq);

	pthread_spin_unlock(&cq->lock);

	return err == CQ_POLL_ERR ? err : npolled;
}

static inline void _mlx4_end_poll(struct ibv_cq_ex *ibcq, int lock)
				  ALWAYS_INLINE;
static inline void _mlx4_end_poll(struct ibv_cq_ex *ibcq, int lock)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	mlx4_update_cons_index(cq);

	if (lock)
		pthread_spin_unlock(&cq->lock);
}

static inline int _mlx4_start_poll(struct ibv_cq_ex *ibcq,
				   struct ibv_poll_cq_attr *attr,
				   int lock)
				   ALWAYS_INLINE;
static inline int _mlx4_start_poll(struct ibv_cq_ex *ibcq,
				   struct ibv_poll_cq_attr *attr,
				   int lock)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	struct mlx4_cqe *cqe;
	int err;

	if (unlikely(attr->comp_mask))
		return EINVAL;

	if (lock)
		pthread_spin_lock(&cq->lock);

	cq->cur_qp = NULL;

	err = mlx4_get_next_cqe(cq, &cqe);
	if (err == CQ_EMPTY) {
		if (lock)
			pthread_spin_unlock(&cq->lock);
		return ENOENT;
	}

	err = mlx4_parse_lazy_cqe(cq, cqe);
	if (lock && err)
		pthread_spin_unlock(&cq->lock);

	return err;
}

static int mlx4_next_poll(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	struct mlx4_cqe *cqe;
	int err;

	err = mlx4_get_next_cqe(cq, &cqe);
	if (err == CQ_EMPTY)
		return ENOENT;

	return mlx4_parse_lazy_cqe(cq, cqe);
}

static void mlx4_end_poll(struct ibv_cq_ex *ibcq)
{
	_mlx4_end_poll(ibcq, 0);
}

static void mlx4_end_poll_lock(struct ibv_cq_ex *ibcq)
{
	_mlx4_end_poll(ibcq, 1);
}

static int mlx4_start_poll(struct ibv_cq_ex *ibcq,
		    struct ibv_poll_cq_attr *attr)
{
	return _mlx4_start_poll(ibcq, attr, 0);
}

static int mlx4_start_poll_lock(struct ibv_cq_ex *ibcq,
			 struct ibv_poll_cq_attr *attr)
{
	return _mlx4_start_poll(ibcq, attr, 1);
}

static enum ibv_wc_opcode mlx4_cq_read_wc_opcode(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	if (cq->cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK) {
		switch (cq->cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_RDMA_WRITE_IMM:
		case MLX4_OPCODE_RDMA_WRITE:
			return IBV_WC_RDMA_WRITE;
		case MLX4_OPCODE_SEND_INVAL:
		case MLX4_OPCODE_SEND_IMM:
		case MLX4_OPCODE_SEND:
			return IBV_WC_SEND;
		case MLX4_OPCODE_RDMA_READ:
			return IBV_WC_RDMA_READ;
		case MLX4_OPCODE_ATOMIC_CS:
			return IBV_WC_COMP_SWAP;
		case MLX4_OPCODE_ATOMIC_FA:
			return IBV_WC_FETCH_ADD;
		case MLX4_OPCODE_LOCAL_INVAL:
			return IBV_WC_LOCAL_INV;
		case MLX4_OPCODE_BIND_MW:
			return IBV_WC_BIND_MW;
		}
	} else {
		switch (cq->cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			return IBV_WC_RECV_RDMA_WITH_IMM;
		case MLX4_RECV_OPCODE_SEND_INVAL:
		case MLX4_RECV_OPCODE_SEND_IMM:
		case MLX4_RECV_OPCODE_SEND:
			return IBV_WC_RECV;
		}
	}

	return 0;
}

static uint32_t mlx4_cq_read_wc_qp_num(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK;
}

static int mlx4_cq_read_wc_flags(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	int is_send  = cq->cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;
	int wc_flags = 0;

	if (is_send) {
		switch (cq->cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_RDMA_WRITE_IMM:
		case MLX4_OPCODE_SEND_IMM:
			wc_flags |= IBV_WC_WITH_IMM;
			break;
		}
	} else {
		if (cq->flags & MLX4_CQ_FLAGS_RX_CSUM_VALID)
			wc_flags |= ((cq->cqe->status &
				htobe32(MLX4_CQE_STATUS_IPV4_CSUM_OK)) ==
				htobe32(MLX4_CQE_STATUS_IPV4_CSUM_OK)) <<
				IBV_WC_IP_CSUM_OK_SHIFT;

		switch (cq->cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc_flags |= IBV_WC_WITH_IMM;
			break;
		case MLX4_RECV_OPCODE_SEND_INVAL:
			wc_flags |= IBV_WC_WITH_INV;
			break;
		}
		wc_flags |= (be32toh(cq->cqe->g_mlpath_rqpn) & 0x80000000) ? IBV_WC_GRH : 0;
	}

	return wc_flags;
}

static uint32_t mlx4_cq_read_wc_byte_len(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe->byte_cnt);
}

static uint32_t mlx4_cq_read_wc_vendor_err(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));
	struct mlx4_err_cqe *ecqe = (struct mlx4_err_cqe *)cq->cqe;

	return ecqe->vendor_err;
}

static uint32_t mlx4_cq_read_wc_imm_data(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	switch (cq->cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
	case MLX4_RECV_OPCODE_SEND_INVAL:
		return be32toh(cq->cqe->immed_rss_invalid);
	default:
		return cq->cqe->immed_rss_invalid;
	}
}

static uint32_t mlx4_cq_read_wc_slid(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return (uint32_t)be16toh(cq->cqe->rlid);
}

static uint8_t mlx4_cq_read_wc_sl(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	if ((cq->cur_qp) && (cq->cur_qp->link_layer == IBV_LINK_LAYER_ETHERNET))
		return be16toh(cq->cqe->sl_vid) >> 13;
	else
		return be16toh(cq->cqe->sl_vid) >> 12;
}

static uint32_t mlx4_cq_read_wc_src_qp(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return be32toh(cq->cqe->g_mlpath_rqpn) & 0xffffff;
}

static uint8_t mlx4_cq_read_wc_dlid_path_bits(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return (be32toh(cq->cqe->g_mlpath_rqpn) >> 24) & 0x7f;
}

static uint64_t mlx4_cq_read_wc_completion_ts(struct ibv_cq_ex *ibcq)
{
	struct mlx4_cq *cq = to_mcq(ibv_cq_ex_to_cq(ibcq));

	return ((uint64_t)be32toh(cq->cqe->ts_47_16) << 16) |
			       (cq->cqe->ts_15_8   <<  8) |
			       (cq->cqe->ts_7_0);
}

void mlx4_cq_fill_pfns(struct mlx4_cq *cq, const struct ibv_cq_init_attr_ex *cq_attr)
{

	if (cq->flags & MLX4_CQ_FLAGS_SINGLE_THREADED) {
		cq->ibv_cq.start_poll = mlx4_start_poll;
		cq->ibv_cq.end_poll = mlx4_end_poll;
	} else {
		cq->ibv_cq.start_poll = mlx4_start_poll_lock;
		cq->ibv_cq.end_poll = mlx4_end_poll_lock;
	}
	cq->ibv_cq.next_poll = mlx4_next_poll;

	cq->ibv_cq.read_opcode = mlx4_cq_read_wc_opcode;
	cq->ibv_cq.read_vendor_err = mlx4_cq_read_wc_vendor_err;
	cq->ibv_cq.read_wc_flags = mlx4_cq_read_wc_flags;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_BYTE_LEN)
		cq->ibv_cq.read_byte_len = mlx4_cq_read_wc_byte_len;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_IMM)
		cq->ibv_cq.read_imm_data = mlx4_cq_read_wc_imm_data;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_QP_NUM)
		cq->ibv_cq.read_qp_num = mlx4_cq_read_wc_qp_num;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_SRC_QP)
		cq->ibv_cq.read_src_qp = mlx4_cq_read_wc_src_qp;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_SLID)
		cq->ibv_cq.read_slid = mlx4_cq_read_wc_slid;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_SL)
		cq->ibv_cq.read_sl = mlx4_cq_read_wc_sl;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_DLID_PATH_BITS)
		cq->ibv_cq.read_dlid_path_bits = mlx4_cq_read_wc_dlid_path_bits;
	if (cq_attr->wc_flags & IBV_WC_EX_WITH_COMPLETION_TIMESTAMP)
		cq->ibv_cq.read_completion_ts = mlx4_cq_read_wc_completion_ts;
}

int mlx4_arm_cq(struct ibv_cq *ibvcq, int solicited)
{
	struct mlx4_cq *cq = to_mcq(ibvcq);
	uint32_t doorbell[2];
	uint32_t sn;
	uint32_t ci;
	uint32_t cmd;

	sn  = cq->arm_sn & 3;
	ci  = cq->cons_index & 0xffffff;
	cmd = solicited ? MLX4_CQ_DB_REQ_NOT_SOL : MLX4_CQ_DB_REQ_NOT;

	*cq->arm_db = htobe32(sn << 28 | cmd | ci);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	udma_to_device_barrier();

	doorbell[0] = htobe32(sn << 28 | cmd | cq->cqn);
	doorbell[1] = htobe32(ci);

	mlx4_write64(doorbell, to_mctx(ibvcq->context), MLX4_CQ_DOORBELL);

	return 0;
}

void mlx4_cq_event(struct ibv_cq *cq)
{
	to_mcq(cq)->arm_sn++;
}

void __mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq)
{
	struct mlx4_cqe *cqe, *dest;
	uint32_t prod_index;
	uint8_t owner_bit;
	int nfreed = 0;
	int cqe_inc = cq->cqe_size == 64 ? 1 : 0;

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
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
		cqe += cqe_inc;
		if (srq && srq->ext_srq &&
		    (be32toh(cqe->g_mlpath_rqpn) & MLX4_CQE_QPN_MASK) == srq->verbs_srq.srq_num &&
		    !(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK)) {
			mlx4_free_srq_wqe(srq, be16toh(cqe->wqe_index));
			++nfreed;
		} else if ((be32toh(cqe->vlan_my_qpn) & MLX4_CQE_QPN_MASK) == qpn) {
			if (srq && !(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
				mlx4_free_srq_wqe(srq, be16toh(cqe->wqe_index));
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe);
			dest += cqe_inc;
			owner_bit = dest->owner_sr_opcode & MLX4_CQE_OWNER_MASK;
			memcpy(dest, cqe, sizeof *cqe);
			dest->owner_sr_opcode = owner_bit |
				(dest->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		udma_to_device_barrier();
		mlx4_update_cons_index(cq);
	}
}

void mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq)
{
	pthread_spin_lock(&cq->lock);
	__mlx4_cq_clean(cq, qpn, srq);
	pthread_spin_unlock(&cq->lock);
}

int mlx4_get_outstanding_cqes(struct mlx4_cq *cq)
{
	uint32_t i;

	for (i = cq->cons_index; get_sw_cqe(cq, i); ++i)
		;

	return i - cq->cons_index;
}

void mlx4_cq_resize_copy_cqes(struct mlx4_cq *cq, void *buf, int old_cqe)
{
	struct mlx4_cqe *cqe;
	int i;
	int cqe_inc = cq->cqe_size == 64 ? 1 : 0;

	i = cq->cons_index;
	cqe = get_cqe(cq, (i & old_cqe));
	cqe += cqe_inc;

	while ((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) != MLX4_CQE_OPCODE_RESIZE) {
		cqe->owner_sr_opcode = (cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK) |
			(((i + 1) & (cq->ibv_cq.cqe + 1)) ? MLX4_CQE_OWNER_MASK : 0);
		memcpy(buf + ((i + 1) & cq->ibv_cq.cqe) * cq->cqe_size,
		       cqe - cqe_inc, cq->cqe_size);
		++i;
		cqe = get_cqe(cq, (i & old_cqe));
		cqe += cqe_inc;
	}

	++cq->cons_index;
}

int mlx4_alloc_cq_buf(struct mlx4_device *dev, struct mlx4_buf *buf, int nent,
		      int entry_size)
{
	if (mlx4_alloc_buf(buf, align(nent * entry_size, dev->page_size),
			   dev->page_size))
		return -1;
	memset(buf->buf, 0, nent * entry_size);

	return 0;
}
