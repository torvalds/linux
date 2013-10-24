/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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

#include <linux/kref.h>
#include <rdma/ib_umem.h>
#include "mlx5_ib.h"
#include "user.h"

static void mlx5_ib_cq_comp(struct mlx5_core_cq *cq)
{
	struct ib_cq *ibcq = &to_mibcq(cq)->ibcq;

	ibcq->comp_handler(ibcq, ibcq->cq_context);
}

static void mlx5_ib_cq_event(struct mlx5_core_cq *mcq, enum mlx5_event type)
{
	struct mlx5_ib_cq *cq = container_of(mcq, struct mlx5_ib_cq, mcq);
	struct mlx5_ib_dev *dev = to_mdev(cq->ibcq.device);
	struct ib_cq *ibcq = &cq->ibcq;
	struct ib_event event;

	if (type != MLX5_EVENT_TYPE_CQ_ERROR) {
		mlx5_ib_warn(dev, "Unexpected event type %d on CQ %06x\n",
			     type, mcq->cqn);
		return;
	}

	if (ibcq->event_handler) {
		event.device     = &dev->ib_dev;
		event.event      = IB_EVENT_CQ_ERR;
		event.element.cq = ibcq;
		ibcq->event_handler(&event, ibcq->cq_context);
	}
}

static void *get_cqe_from_buf(struct mlx5_ib_cq_buf *buf, int n, int size)
{
	return mlx5_buf_offset(&buf->buf, n * size);
}

static void *get_cqe(struct mlx5_ib_cq *cq, int n)
{
	return get_cqe_from_buf(&cq->buf, n, cq->mcq.cqe_sz);
}

static void *get_sw_cqe(struct mlx5_ib_cq *cq, int n)
{
	void *cqe = get_cqe(cq, n & cq->ibcq.cqe);
	struct mlx5_cqe64 *cqe64;

	cqe64 = (cq->mcq.cqe_sz == 64) ? cqe : cqe + 64;
	return ((cqe64->op_own & MLX5_CQE_OWNER_MASK) ^
		!!(n & (cq->ibcq.cqe + 1))) ? NULL : cqe;
}

static void *next_cqe_sw(struct mlx5_ib_cq *cq)
{
	return get_sw_cqe(cq, cq->mcq.cons_index);
}

static enum ib_wc_opcode get_umr_comp(struct mlx5_ib_wq *wq, int idx)
{
	switch (wq->wr_data[idx]) {
	case MLX5_IB_WR_UMR:
		return 0;

	case IB_WR_LOCAL_INV:
		return IB_WC_LOCAL_INV;

	case IB_WR_FAST_REG_MR:
		return IB_WC_FAST_REG_MR;

	default:
		pr_warn("unknown completion status\n");
		return 0;
	}
}

static void handle_good_req(struct ib_wc *wc, struct mlx5_cqe64 *cqe,
			    struct mlx5_ib_wq *wq, int idx)
{
	wc->wc_flags = 0;
	switch (be32_to_cpu(cqe->sop_drop_qpn) >> 24) {
	case MLX5_OPCODE_RDMA_WRITE_IMM:
		wc->wc_flags |= IB_WC_WITH_IMM;
	case MLX5_OPCODE_RDMA_WRITE:
		wc->opcode    = IB_WC_RDMA_WRITE;
		break;
	case MLX5_OPCODE_SEND_IMM:
		wc->wc_flags |= IB_WC_WITH_IMM;
	case MLX5_OPCODE_SEND:
	case MLX5_OPCODE_SEND_INVAL:
		wc->opcode    = IB_WC_SEND;
		break;
	case MLX5_OPCODE_RDMA_READ:
		wc->opcode    = IB_WC_RDMA_READ;
		wc->byte_len  = be32_to_cpu(cqe->byte_cnt);
		break;
	case MLX5_OPCODE_ATOMIC_CS:
		wc->opcode    = IB_WC_COMP_SWAP;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_ATOMIC_FA:
		wc->opcode    = IB_WC_FETCH_ADD;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_ATOMIC_MASKED_CS:
		wc->opcode    = IB_WC_MASKED_COMP_SWAP;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_ATOMIC_MASKED_FA:
		wc->opcode    = IB_WC_MASKED_FETCH_ADD;
		wc->byte_len  = 8;
		break;
	case MLX5_OPCODE_BIND_MW:
		wc->opcode    = IB_WC_BIND_MW;
		break;
	case MLX5_OPCODE_UMR:
		wc->opcode = get_umr_comp(wq, idx);
		break;
	}
}

enum {
	MLX5_GRH_IN_BUFFER = 1,
	MLX5_GRH_IN_CQE	   = 2,
};

static void handle_responder(struct ib_wc *wc, struct mlx5_cqe64 *cqe,
			     struct mlx5_ib_qp *qp)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->ibqp.device);
	struct mlx5_ib_srq *srq;
	struct mlx5_ib_wq *wq;
	u16 wqe_ctr;
	u8 g;

	if (qp->ibqp.srq || qp->ibqp.xrcd) {
		struct mlx5_core_srq *msrq = NULL;

		if (qp->ibqp.xrcd) {
			msrq = mlx5_core_get_srq(&dev->mdev,
						 be32_to_cpu(cqe->srqn));
			srq = to_mibsrq(msrq);
		} else {
			srq = to_msrq(qp->ibqp.srq);
		}
		if (srq) {
			wqe_ctr = be16_to_cpu(cqe->wqe_counter);
			wc->wr_id = srq->wrid[wqe_ctr];
			mlx5_ib_free_srq_wqe(srq, wqe_ctr);
			if (msrq && atomic_dec_and_test(&msrq->refcount))
				complete(&msrq->free);
		}
	} else {
		wq	  = &qp->rq;
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	}
	wc->byte_len = be32_to_cpu(cqe->byte_cnt);

	switch (cqe->op_own >> 4) {
	case MLX5_CQE_RESP_WR_IMM:
		wc->opcode	= IB_WC_RECV_RDMA_WITH_IMM;
		wc->wc_flags	= IB_WC_WITH_IMM;
		wc->ex.imm_data = cqe->imm_inval_pkey;
		break;
	case MLX5_CQE_RESP_SEND:
		wc->opcode   = IB_WC_RECV;
		wc->wc_flags = 0;
		break;
	case MLX5_CQE_RESP_SEND_IMM:
		wc->opcode	= IB_WC_RECV;
		wc->wc_flags	= IB_WC_WITH_IMM;
		wc->ex.imm_data = cqe->imm_inval_pkey;
		break;
	case MLX5_CQE_RESP_SEND_INV:
		wc->opcode	= IB_WC_RECV;
		wc->wc_flags	= IB_WC_WITH_INVALIDATE;
		wc->ex.invalidate_rkey = be32_to_cpu(cqe->imm_inval_pkey);
		break;
	}
	wc->slid	   = be16_to_cpu(cqe->slid);
	wc->sl		   = (be32_to_cpu(cqe->flags_rqpn) >> 24) & 0xf;
	wc->src_qp	   = be32_to_cpu(cqe->flags_rqpn) & 0xffffff;
	wc->dlid_path_bits = cqe->ml_path;
	g = (be32_to_cpu(cqe->flags_rqpn) >> 28) & 3;
	wc->wc_flags |= g ? IB_WC_GRH : 0;
	wc->pkey_index     = be32_to_cpu(cqe->imm_inval_pkey) & 0xffff;
}

static void dump_cqe(struct mlx5_ib_dev *dev, struct mlx5_err_cqe *cqe)
{
	__be32 *p = (__be32 *)cqe;
	int i;

	mlx5_ib_warn(dev, "dump error cqe\n");
	for (i = 0; i < sizeof(*cqe) / 16; i++, p += 4)
		pr_info("%08x %08x %08x %08x\n", be32_to_cpu(p[0]),
			be32_to_cpu(p[1]), be32_to_cpu(p[2]),
			be32_to_cpu(p[3]));
}

static void mlx5_handle_error_cqe(struct mlx5_ib_dev *dev,
				  struct mlx5_err_cqe *cqe,
				  struct ib_wc *wc)
{
	int dump = 1;

	switch (cqe->syndrome) {
	case MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IB_WC_LOC_LEN_ERR;
		break;
	case MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case MLX5_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IB_WC_LOC_PROT_ERR;
		break;
	case MLX5_CQE_SYNDROME_WR_FLUSH_ERR:
		dump = 0;
		wc->status = IB_WC_WR_FLUSH_ERR;
		break;
	case MLX5_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IB_WC_MW_BIND_ERR;
		break;
	case MLX5_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IB_WC_BAD_RESP_ERR;
		break;
	case MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IB_WC_REM_ACCESS_ERR;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IB_WC_REM_OP_ERR;
		break;
	case MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WC_RETRY_EXC_ERR;
		dump = 0;
		break;
	case MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IB_WC_RNR_RETRY_EXC_ERR;
		dump = 0;
		break;
	case MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IB_WC_REM_ABORT_ERR;
		break;
	default:
		wc->status = IB_WC_GENERAL_ERR;
		break;
	}

	wc->vendor_err = cqe->vendor_err_synd;
	if (dump)
		dump_cqe(dev, cqe);
}

static int is_atomic_response(struct mlx5_ib_qp *qp, uint16_t idx)
{
	/* TBD: waiting decision
	*/
	return 0;
}

static void *mlx5_get_atomic_laddr(struct mlx5_ib_qp *qp, uint16_t idx)
{
	struct mlx5_wqe_data_seg *dpseg;
	void *addr;

	dpseg = mlx5_get_send_wqe(qp, idx) + sizeof(struct mlx5_wqe_ctrl_seg) +
		sizeof(struct mlx5_wqe_raddr_seg) +
		sizeof(struct mlx5_wqe_atomic_seg);
	addr = (void *)(unsigned long)be64_to_cpu(dpseg->addr);
	return addr;
}

static void handle_atomic(struct mlx5_ib_qp *qp, struct mlx5_cqe64 *cqe64,
			  uint16_t idx)
{
	void *addr;
	int byte_count;
	int i;

	if (!is_atomic_response(qp, idx))
		return;

	byte_count = be32_to_cpu(cqe64->byte_cnt);
	addr = mlx5_get_atomic_laddr(qp, idx);

	if (byte_count == 4) {
		*(uint32_t *)addr = be32_to_cpu(*((__be32 *)addr));
	} else {
		for (i = 0; i < byte_count; i += 8) {
			*(uint64_t *)addr = be64_to_cpu(*((__be64 *)addr));
			addr += 8;
		}
	}

	return;
}

static void handle_atomics(struct mlx5_ib_qp *qp, struct mlx5_cqe64 *cqe64,
			   u16 tail, u16 head)
{
	int idx;

	do {
		idx = tail & (qp->sq.wqe_cnt - 1);
		handle_atomic(qp, cqe64, idx);
		if (idx == head)
			break;

		tail = qp->sq.w_list[idx].next;
	} while (1);
	tail = qp->sq.w_list[idx].next;
	qp->sq.last_poll = tail;
}

static int mlx5_poll_one(struct mlx5_ib_cq *cq,
			 struct mlx5_ib_qp **cur_qp,
			 struct ib_wc *wc)
{
	struct mlx5_ib_dev *dev = to_mdev(cq->ibcq.device);
	struct mlx5_err_cqe *err_cqe;
	struct mlx5_cqe64 *cqe64;
	struct mlx5_core_qp *mqp;
	struct mlx5_ib_wq *wq;
	uint8_t opcode;
	uint32_t qpn;
	u16 wqe_ctr;
	void *cqe;
	int idx;

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	cqe64 = (cq->mcq.cqe_sz == 64) ? cqe : cqe + 64;

	++cq->mcq.cons_index;

	/* Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	/* TBD: resize CQ */

	qpn = ntohl(cqe64->sop_drop_qpn) & 0xffffff;
	if (!*cur_qp || (qpn != (*cur_qp)->ibqp.qp_num)) {
		/* We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		mqp = __mlx5_qp_lookup(&dev->mdev, qpn);
		if (unlikely(!mqp)) {
			mlx5_ib_warn(dev, "CQE@CQ %06x for unknown QPN %6x\n",
				     cq->mcq.cqn, qpn);
			return -EINVAL;
		}

		*cur_qp = to_mibqp(mqp);
	}

	wc->qp  = &(*cur_qp)->ibqp;
	opcode = cqe64->op_own >> 4;
	switch (opcode) {
	case MLX5_CQE_REQ:
		wq = &(*cur_qp)->sq;
		wqe_ctr = be16_to_cpu(cqe64->wqe_counter);
		idx = wqe_ctr & (wq->wqe_cnt - 1);
		handle_good_req(wc, cqe64, wq, idx);
		handle_atomics(*cur_qp, cqe64, wq->last_poll, idx);
		wc->wr_id = wq->wrid[idx];
		wq->tail = wq->wqe_head[idx] + 1;
		wc->status = IB_WC_SUCCESS;
		break;
	case MLX5_CQE_RESP_WR_IMM:
	case MLX5_CQE_RESP_SEND:
	case MLX5_CQE_RESP_SEND_IMM:
	case MLX5_CQE_RESP_SEND_INV:
		handle_responder(wc, cqe64, *cur_qp);
		wc->status = IB_WC_SUCCESS;
		break;
	case MLX5_CQE_RESIZE_CQ:
		break;
	case MLX5_CQE_REQ_ERR:
	case MLX5_CQE_RESP_ERR:
		err_cqe = (struct mlx5_err_cqe *)cqe64;
		mlx5_handle_error_cqe(dev, err_cqe, wc);
		mlx5_ib_dbg(dev, "%s error cqe on cqn 0x%x:\n",
			    opcode == MLX5_CQE_REQ_ERR ?
			    "Requestor" : "Responder", cq->mcq.cqn);
		mlx5_ib_dbg(dev, "syndrome 0x%x, vendor syndrome 0x%x\n",
			    err_cqe->syndrome, err_cqe->vendor_err_synd);
		if (opcode == MLX5_CQE_REQ_ERR) {
			wq = &(*cur_qp)->sq;
			wqe_ctr = be16_to_cpu(cqe64->wqe_counter);
			idx = wqe_ctr & (wq->wqe_cnt - 1);
			wc->wr_id = wq->wrid[idx];
			wq->tail = wq->wqe_head[idx] + 1;
		} else {
			struct mlx5_ib_srq *srq;

			if ((*cur_qp)->ibqp.srq) {
				srq = to_msrq((*cur_qp)->ibqp.srq);
				wqe_ctr = be16_to_cpu(cqe64->wqe_counter);
				wc->wr_id = srq->wrid[wqe_ctr];
				mlx5_ib_free_srq_wqe(srq, wqe_ctr);
			} else {
				wq = &(*cur_qp)->rq;
				wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
				++wq->tail;
			}
		}
		break;
	}

	return 0;
}

int mlx5_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct mlx5_ib_cq *cq = to_mcq(ibcq);
	struct mlx5_ib_qp *cur_qp = NULL;
	unsigned long flags;
	int npolled;
	int err = 0;

	spin_lock_irqsave(&cq->lock, flags);

	for (npolled = 0; npolled < num_entries; npolled++) {
		err = mlx5_poll_one(cq, &cur_qp, wc + npolled);
		if (err)
			break;
	}

	if (npolled)
		mlx5_cq_set_ci(&cq->mcq);

	spin_unlock_irqrestore(&cq->lock, flags);

	if (err == 0 || err == -EAGAIN)
		return npolled;
	else
		return err;
}

int mlx5_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	mlx5_cq_arm(&to_mcq(ibcq)->mcq,
		    (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		    MLX5_CQ_DB_REQ_NOT_SOL : MLX5_CQ_DB_REQ_NOT,
		    to_mdev(ibcq->device)->mdev.priv.uuari.uars[0].map,
		    MLX5_GET_DOORBELL_LOCK(&to_mdev(ibcq->device)->mdev.priv.cq_uar_lock));

	return 0;
}

static int alloc_cq_buf(struct mlx5_ib_dev *dev, struct mlx5_ib_cq_buf *buf,
			int nent, int cqe_size)
{
	int err;

	err = mlx5_buf_alloc(&dev->mdev, nent * cqe_size,
			     PAGE_SIZE * 2, &buf->buf);
	if (err)
		return err;

	buf->cqe_size = cqe_size;

	return 0;
}

static void free_cq_buf(struct mlx5_ib_dev *dev, struct mlx5_ib_cq_buf *buf)
{
	mlx5_buf_free(&dev->mdev, &buf->buf);
}

static int create_cq_user(struct mlx5_ib_dev *dev, struct ib_udata *udata,
			  struct ib_ucontext *context, struct mlx5_ib_cq *cq,
			  int entries, struct mlx5_create_cq_mbox_in **cqb,
			  int *cqe_size, int *index, int *inlen)
{
	struct mlx5_ib_create_cq ucmd;
	int page_shift;
	int npages;
	int ncont;
	int err;

	if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd)))
		return -EFAULT;

	if (ucmd.cqe_size != 64 && ucmd.cqe_size != 128)
		return -EINVAL;

	*cqe_size = ucmd.cqe_size;

	cq->buf.umem = ib_umem_get(context, ucmd.buf_addr,
				   entries * ucmd.cqe_size,
				   IB_ACCESS_LOCAL_WRITE, 1);
	if (IS_ERR(cq->buf.umem)) {
		err = PTR_ERR(cq->buf.umem);
		return err;
	}

	err = mlx5_ib_db_map_user(to_mucontext(context), ucmd.db_addr,
				  &cq->db);
	if (err)
		goto err_umem;

	mlx5_ib_cont_pages(cq->buf.umem, ucmd.buf_addr, &npages, &page_shift,
			   &ncont, NULL);
	mlx5_ib_dbg(dev, "addr 0x%llx, size %u, npages %d, page_shift %d, ncont %d\n",
		    ucmd.buf_addr, entries * ucmd.cqe_size, npages, page_shift, ncont);

	*inlen = sizeof(**cqb) + sizeof(*(*cqb)->pas) * ncont;
	*cqb = mlx5_vzalloc(*inlen);
	if (!*cqb) {
		err = -ENOMEM;
		goto err_db;
	}
	mlx5_ib_populate_pas(dev, cq->buf.umem, page_shift, (*cqb)->pas, 0);
	(*cqb)->ctx.log_pg_sz = page_shift - PAGE_SHIFT;

	*index = to_mucontext(context)->uuari.uars[0].index;

	return 0;

err_db:
	mlx5_ib_db_unmap_user(to_mucontext(context), &cq->db);

err_umem:
	ib_umem_release(cq->buf.umem);
	return err;
}

static void destroy_cq_user(struct mlx5_ib_cq *cq, struct ib_ucontext *context)
{
	mlx5_ib_db_unmap_user(to_mucontext(context), &cq->db);
	ib_umem_release(cq->buf.umem);
}

static void init_cq_buf(struct mlx5_ib_cq *cq, int nent)
{
	int i;
	void *cqe;
	struct mlx5_cqe64 *cqe64;

	for (i = 0; i < nent; i++) {
		cqe = get_cqe(cq, i);
		cqe64 = (cq->buf.cqe_size == 64) ? cqe : cqe + 64;
		cqe64->op_own = 0xf1;
	}
}

static int create_cq_kernel(struct mlx5_ib_dev *dev, struct mlx5_ib_cq *cq,
			    int entries, int cqe_size,
			    struct mlx5_create_cq_mbox_in **cqb,
			    int *index, int *inlen)
{
	int err;

	err = mlx5_db_alloc(&dev->mdev, &cq->db);
	if (err)
		return err;

	cq->mcq.set_ci_db  = cq->db.db;
	cq->mcq.arm_db     = cq->db.db + 1;
	*cq->mcq.set_ci_db = 0;
	*cq->mcq.arm_db    = 0;
	cq->mcq.cqe_sz = cqe_size;

	err = alloc_cq_buf(dev, &cq->buf, entries, cqe_size);
	if (err)
		goto err_db;

	init_cq_buf(cq, entries);

	*inlen = sizeof(**cqb) + sizeof(*(*cqb)->pas) * cq->buf.buf.npages;
	*cqb = mlx5_vzalloc(*inlen);
	if (!*cqb) {
		err = -ENOMEM;
		goto err_buf;
	}
	mlx5_fill_page_array(&cq->buf.buf, (*cqb)->pas);

	(*cqb)->ctx.log_pg_sz = cq->buf.buf.page_shift - MLX5_ADAPTER_PAGE_SHIFT;
	*index = dev->mdev.priv.uuari.uars[0].index;

	return 0;

err_buf:
	free_cq_buf(dev, &cq->buf);

err_db:
	mlx5_db_free(&dev->mdev, &cq->db);
	return err;
}

static void destroy_cq_kernel(struct mlx5_ib_dev *dev, struct mlx5_ib_cq *cq)
{
	free_cq_buf(dev, &cq->buf);
	mlx5_db_free(&dev->mdev, &cq->db);
}

struct ib_cq *mlx5_ib_create_cq(struct ib_device *ibdev, int entries,
				int vector, struct ib_ucontext *context,
				struct ib_udata *udata)
{
	struct mlx5_create_cq_mbox_in *cqb = NULL;
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_ib_cq *cq;
	int uninitialized_var(index);
	int uninitialized_var(inlen);
	int cqe_size;
	int irqn;
	int eqn;
	int err;

	if (entries < 0)
		return ERR_PTR(-EINVAL);

	entries = roundup_pow_of_two(entries + 1);
	if (entries > dev->mdev.caps.max_cqes)
		return ERR_PTR(-EINVAL);

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	cq->ibcq.cqe = entries - 1;
	mutex_init(&cq->resize_mutex);
	spin_lock_init(&cq->lock);
	cq->resize_buf = NULL;
	cq->resize_umem = NULL;

	if (context) {
		err = create_cq_user(dev, udata, context, cq, entries,
				     &cqb, &cqe_size, &index, &inlen);
		if (err)
			goto err_create;
	} else {
		/* for now choose 64 bytes till we have a proper interface */
		cqe_size = 64;
		err = create_cq_kernel(dev, cq, entries, cqe_size, &cqb,
				       &index, &inlen);
		if (err)
			goto err_create;
	}

	cq->cqe_size = cqe_size;
	cqb->ctx.cqe_sz_flags = cqe_sz_to_mlx_sz(cqe_size) << 5;
	cqb->ctx.log_sz_usr_page = cpu_to_be32((ilog2(entries) << 24) | index);
	err = mlx5_vector2eqn(dev, vector, &eqn, &irqn);
	if (err)
		goto err_cqb;

	cqb->ctx.c_eqn = cpu_to_be16(eqn);
	cqb->ctx.db_record_addr = cpu_to_be64(cq->db.dma);

	err = mlx5_core_create_cq(&dev->mdev, &cq->mcq, cqb, inlen);
	if (err)
		goto err_cqb;

	mlx5_ib_dbg(dev, "cqn 0x%x\n", cq->mcq.cqn);
	cq->mcq.irqn = irqn;
	cq->mcq.comp  = mlx5_ib_cq_comp;
	cq->mcq.event = mlx5_ib_cq_event;

	if (context)
		if (ib_copy_to_udata(udata, &cq->mcq.cqn, sizeof(__u32))) {
			err = -EFAULT;
			goto err_cmd;
		}


	mlx5_vfree(cqb);
	return &cq->ibcq;

err_cmd:
	mlx5_core_destroy_cq(&dev->mdev, &cq->mcq);

err_cqb:
	mlx5_vfree(cqb);
	if (context)
		destroy_cq_user(cq, context);
	else
		destroy_cq_kernel(dev, cq);

err_create:
	kfree(cq);

	return ERR_PTR(err);
}


int mlx5_ib_destroy_cq(struct ib_cq *cq)
{
	struct mlx5_ib_dev *dev = to_mdev(cq->device);
	struct mlx5_ib_cq *mcq = to_mcq(cq);
	struct ib_ucontext *context = NULL;

	if (cq->uobject)
		context = cq->uobject->context;

	mlx5_core_destroy_cq(&dev->mdev, &mcq->mcq);
	if (context)
		destroy_cq_user(mcq, context);
	else
		destroy_cq_kernel(dev, mcq);

	kfree(mcq);

	return 0;
}

static int is_equal_rsn(struct mlx5_cqe64 *cqe64, u32 rsn)
{
	return rsn == (ntohl(cqe64->sop_drop_qpn) & 0xffffff);
}

void __mlx5_ib_cq_clean(struct mlx5_ib_cq *cq, u32 rsn, struct mlx5_ib_srq *srq)
{
	struct mlx5_cqe64 *cqe64, *dest64;
	void *cqe, *dest;
	u32 prod_index;
	int nfreed = 0;
	u8 owner_bit;

	if (!cq)
		return;

	/* First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->mcq.cons_index; get_sw_cqe(cq, prod_index); prod_index++)
		if (prod_index == cq->mcq.cons_index + cq->ibcq.cqe)
			break;

	/* Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->mcq.cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibcq.cqe);
		cqe64 = (cq->mcq.cqe_sz == 64) ? cqe : cqe + 64;
		if (is_equal_rsn(cqe64, rsn)) {
			if (srq && (ntohl(cqe64->srqn) & 0xffffff))
				mlx5_ib_free_srq_wqe(srq, be16_to_cpu(cqe64->wqe_counter));
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibcq.cqe);
			dest64 = (cq->mcq.cqe_sz == 64) ? dest : dest + 64;
			owner_bit = dest64->op_own & MLX5_CQE_OWNER_MASK;
			memcpy(dest, cqe, cq->mcq.cqe_sz);
			dest64->op_own = owner_bit |
				(dest64->op_own & ~MLX5_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->mcq.cons_index += nfreed;
		/* Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		mlx5_cq_set_ci(&cq->mcq);
	}
}

void mlx5_ib_cq_clean(struct mlx5_ib_cq *cq, u32 qpn, struct mlx5_ib_srq *srq)
{
	if (!cq)
		return;

	spin_lock_irq(&cq->lock);
	__mlx5_ib_cq_clean(cq, qpn, srq);
	spin_unlock_irq(&cq->lock);
}

int mlx5_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	return -ENOSYS;
}

int mlx5_ib_resize_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata)
{
	return -ENOSYS;
}

int mlx5_ib_get_cqe_size(struct mlx5_ib_dev *dev, struct ib_cq *ibcq)
{
	struct mlx5_ib_cq *cq;

	if (!ibcq)
		return 128;

	cq = to_mcq(ibcq);
	return cq->cqe_size;
}
