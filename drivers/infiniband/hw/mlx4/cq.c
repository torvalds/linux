/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include <linux/mlx4/cq.h>
#include <linux/mlx4/qp.h>

#include "mlx4_ib.h"
#include "user.h"

static void mlx4_ib_cq_comp(struct mlx4_cq *cq)
{
	struct ib_cq *ibcq = &to_mibcq(cq)->ibcq;
	ibcq->comp_handler(ibcq, ibcq->cq_context);
}

static void mlx4_ib_cq_event(struct mlx4_cq *cq, enum mlx4_event type)
{
	struct ib_event event;
	struct ib_cq *ibcq;

	if (type != MLX4_EVENT_TYPE_CQ_ERROR) {
		printk(KERN_WARNING "mlx4_ib: Unexpected event type %d "
		       "on CQ %06x\n", type, cq->cqn);
		return;
	}

	ibcq = &to_mibcq(cq)->ibcq;
	if (ibcq->event_handler) {
		event.device     = ibcq->device;
		event.event      = IB_EVENT_CQ_ERR;
		event.element.cq = ibcq;
		ibcq->event_handler(&event, ibcq->cq_context);
	}
}

static void *get_cqe_from_buf(struct mlx4_ib_cq_buf *buf, int n)
{
	return mlx4_buf_offset(&buf->buf, n * sizeof (struct mlx4_cqe));
}

static void *get_cqe(struct mlx4_ib_cq *cq, int n)
{
	return get_cqe_from_buf(&cq->buf, n);
}

static void *get_sw_cqe(struct mlx4_ib_cq *cq, int n)
{
	struct mlx4_cqe *cqe = get_cqe(cq, n & cq->ibcq.cqe);

	return (!!(cqe->owner_sr_opcode & MLX4_CQE_OWNER_MASK) ^
		!!(n & (cq->ibcq.cqe + 1))) ? NULL : cqe;
}

static struct mlx4_cqe *next_cqe_sw(struct mlx4_ib_cq *cq)
{
	return get_sw_cqe(cq, cq->mcq.cons_index);
}

struct ib_cq *mlx4_ib_create_cq(struct ib_device *ibdev, int entries, int vector,
				struct ib_ucontext *context,
				struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_cq *cq;
	struct mlx4_uar *uar;
	int buf_size;
	int err;

	if (entries < 1 || entries > dev->dev->caps.max_cqes)
		return ERR_PTR(-EINVAL);

	cq = kmalloc(sizeof *cq, GFP_KERNEL);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	entries      = roundup_pow_of_two(entries + 1);
	cq->ibcq.cqe = entries - 1;
	buf_size     = entries * sizeof (struct mlx4_cqe);
	spin_lock_init(&cq->lock);

	if (context) {
		struct mlx4_ib_create_cq ucmd;

		if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
			err = -EFAULT;
			goto err_cq;
		}

		cq->umem = ib_umem_get(context, ucmd.buf_addr, buf_size,
				       IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(cq->umem)) {
			err = PTR_ERR(cq->umem);
			goto err_cq;
		}

		err = mlx4_mtt_init(dev->dev, ib_umem_page_count(cq->umem),
				    ilog2(cq->umem->page_size), &cq->buf.mtt);
		if (err)
			goto err_buf;

		err = mlx4_ib_umem_write_mtt(dev, &cq->buf.mtt, cq->umem);
		if (err)
			goto err_mtt;

		err = mlx4_ib_db_map_user(to_mucontext(context), ucmd.db_addr,
					  &cq->db);
		if (err)
			goto err_mtt;

		uar = &to_mucontext(context)->uar;
	} else {
		err = mlx4_ib_db_alloc(dev, &cq->db, 1);
		if (err)
			goto err_cq;

		cq->mcq.set_ci_db  = cq->db.db;
		cq->mcq.arm_db     = cq->db.db + 1;
		*cq->mcq.set_ci_db = 0;
		*cq->mcq.arm_db    = 0;

		if (mlx4_buf_alloc(dev->dev, buf_size, PAGE_SIZE * 2, &cq->buf.buf)) {
			err = -ENOMEM;
			goto err_db;
		}

		err = mlx4_mtt_init(dev->dev, cq->buf.buf.npages, cq->buf.buf.page_shift,
				    &cq->buf.mtt);
		if (err)
			goto err_buf;

		err = mlx4_buf_write_mtt(dev->dev, &cq->buf.mtt, &cq->buf.buf);
		if (err)
			goto err_mtt;

		uar = &dev->priv_uar;
	}

	err = mlx4_cq_alloc(dev->dev, entries, &cq->buf.mtt, uar,
			    cq->db.dma, &cq->mcq);
	if (err)
		goto err_dbmap;

	cq->mcq.comp  = mlx4_ib_cq_comp;
	cq->mcq.event = mlx4_ib_cq_event;

	if (context)
		if (ib_copy_to_udata(udata, &cq->mcq.cqn, sizeof (__u32))) {
			err = -EFAULT;
			goto err_dbmap;
		}

	return &cq->ibcq;

err_dbmap:
	if (context)
		mlx4_ib_db_unmap_user(to_mucontext(context), &cq->db);

err_mtt:
	mlx4_mtt_cleanup(dev->dev, &cq->buf.mtt);

err_buf:
	if (context)
		ib_umem_release(cq->umem);
	else
		mlx4_buf_free(dev->dev, entries * sizeof (struct mlx4_cqe),
			      &cq->buf.buf);

err_db:
	if (!context)
		mlx4_ib_db_free(dev, &cq->db);

err_cq:
	kfree(cq);

	return ERR_PTR(err);
}

int mlx4_ib_destroy_cq(struct ib_cq *cq)
{
	struct mlx4_ib_dev *dev = to_mdev(cq->device);
	struct mlx4_ib_cq *mcq = to_mcq(cq);

	mlx4_cq_free(dev->dev, &mcq->mcq);
	mlx4_mtt_cleanup(dev->dev, &mcq->buf.mtt);

	if (cq->uobject) {
		mlx4_ib_db_unmap_user(to_mucontext(cq->uobject->context), &mcq->db);
		ib_umem_release(mcq->umem);
	} else {
		mlx4_buf_free(dev->dev, (cq->cqe + 1) * sizeof (struct mlx4_cqe),
			      &mcq->buf.buf);
		mlx4_ib_db_free(dev, &mcq->db);
	}

	kfree(mcq);

	return 0;
}

static void dump_cqe(void *cqe)
{
	__be32 *buf = cqe;

	printk(KERN_DEBUG "CQE contents %08x %08x %08x %08x %08x %08x %08x %08x\n",
	       be32_to_cpu(buf[0]), be32_to_cpu(buf[1]), be32_to_cpu(buf[2]),
	       be32_to_cpu(buf[3]), be32_to_cpu(buf[4]), be32_to_cpu(buf[5]),
	       be32_to_cpu(buf[6]), be32_to_cpu(buf[7]));
}

static void mlx4_ib_handle_error_cqe(struct mlx4_err_cqe *cqe,
				     struct ib_wc *wc)
{
	if (cqe->syndrome == MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR) {
		printk(KERN_DEBUG "local QP operation err "
		       "(QPN %06x, WQE index %x, vendor syndrome %02x, "
		       "opcode = %02x)\n",
		       be32_to_cpu(cqe->my_qpn), be16_to_cpu(cqe->wqe_index),
		       cqe->vendor_err_syndrome,
		       cqe->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);
		dump_cqe(cqe);
	}

	switch (cqe->syndrome) {
	case MLX4_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IB_WC_LOC_LEN_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IB_WC_LOC_PROT_ERR;
		break;
	case MLX4_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IB_WC_WR_FLUSH_ERR;
		break;
	case MLX4_CQE_SYNDROME_MW_BIND_ERR:
		wc->status = IB_WC_MW_BIND_ERR;
		break;
	case MLX4_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IB_WC_BAD_RESP_ERR;
		break;
	case MLX4_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IB_WC_REM_ACCESS_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IB_WC_REM_OP_ERR;
		break;
	case MLX4_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WC_RETRY_EXC_ERR;
		break;
	case MLX4_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	case MLX4_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		wc->status = IB_WC_REM_ABORT_ERR;
		break;
	default:
		wc->status = IB_WC_GENERAL_ERR;
		break;
	}

	wc->vendor_err = cqe->vendor_err_syndrome;
}

static int mlx4_ib_poll_one(struct mlx4_ib_cq *cq,
			    struct mlx4_ib_qp **cur_qp,
			    struct ib_wc *wc)
{
	struct mlx4_cqe *cqe;
	struct mlx4_qp *mqp;
	struct mlx4_ib_wq *wq;
	struct mlx4_ib_srq *srq;
	int is_send;
	int is_error;
	u32 g_mlpath_rqpn;
	u16 wqe_ctr;

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	++cq->mcq.cons_index;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	is_send  = cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK;
	is_error = (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) ==
		MLX4_CQE_OPCODE_ERROR;

	if (unlikely((cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) == MLX4_OPCODE_NOP &&
		     is_send)) {
		printk(KERN_WARNING "Completion for NOP opcode detected!\n");
		return -EINVAL;
	}

	if (!*cur_qp ||
	    (be32_to_cpu(cqe->my_qpn) & 0xffffff) != (*cur_qp)->mqp.qpn) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		mqp = __mlx4_qp_lookup(to_mdev(cq->ibcq.device)->dev,
				       be32_to_cpu(cqe->my_qpn));
		if (unlikely(!mqp)) {
			printk(KERN_WARNING "CQ %06x with entry for unknown QPN %06x\n",
			       cq->mcq.cqn, be32_to_cpu(cqe->my_qpn) & 0xffffff);
			return -EINVAL;
		}

		*cur_qp = to_mibqp(mqp);
	}

	wc->qp = &(*cur_qp)->ibqp;

	if (is_send) {
		wq = &(*cur_qp)->sq;
		if (!(*cur_qp)->sq_signal_bits) {
			wqe_ctr = be16_to_cpu(cqe->wqe_index);
			wq->tail += (u16) (wqe_ctr - (u16) wq->tail);
		}
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	} else if ((*cur_qp)->ibqp.srq) {
		srq = to_msrq((*cur_qp)->ibqp.srq);
		wqe_ctr = be16_to_cpu(cqe->wqe_index);
		wc->wr_id = srq->wrid[wqe_ctr];
		mlx4_ib_free_srq_wqe(srq, wqe_ctr);
	} else {
		wq	  = &(*cur_qp)->rq;
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	}

	if (unlikely(is_error)) {
		mlx4_ib_handle_error_cqe((struct mlx4_err_cqe *) cqe, wc);
		return 0;
	}

	wc->status = IB_WC_SUCCESS;

	if (is_send) {
		wc->wc_flags = 0;
		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_OPCODE_RDMA_WRITE_IMM:
			wc->wc_flags |= IB_WC_WITH_IMM;
		case MLX4_OPCODE_RDMA_WRITE:
			wc->opcode    = IB_WC_RDMA_WRITE;
			break;
		case MLX4_OPCODE_SEND_IMM:
			wc->wc_flags |= IB_WC_WITH_IMM;
		case MLX4_OPCODE_SEND:
			wc->opcode    = IB_WC_SEND;
			break;
		case MLX4_OPCODE_RDMA_READ:
			wc->opcode    = IB_WC_RDMA_READ;
			wc->byte_len  = be32_to_cpu(cqe->byte_cnt);
			break;
		case MLX4_OPCODE_ATOMIC_CS:
			wc->opcode    = IB_WC_COMP_SWAP;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_ATOMIC_FA:
			wc->opcode    = IB_WC_FETCH_ADD;
			wc->byte_len  = 8;
			break;
		case MLX4_OPCODE_BIND_MW:
			wc->opcode    = IB_WC_BIND_MW;
			break;
		}
	} else {
		wc->byte_len = be32_to_cpu(cqe->byte_cnt);

		switch (cqe->owner_sr_opcode & MLX4_CQE_OPCODE_MASK) {
		case MLX4_RECV_OPCODE_RDMA_WRITE_IMM:
			wc->opcode   = IB_WC_RECV_RDMA_WITH_IMM;
			wc->wc_flags = IB_WC_WITH_IMM;
			wc->imm_data = cqe->immed_rss_invalid;
			break;
		case MLX4_RECV_OPCODE_SEND:
			wc->opcode   = IB_WC_RECV;
			wc->wc_flags = 0;
			break;
		case MLX4_RECV_OPCODE_SEND_IMM:
			wc->opcode   = IB_WC_RECV;
			wc->wc_flags = IB_WC_WITH_IMM;
			wc->imm_data = cqe->immed_rss_invalid;
			break;
		}

		wc->slid	   = be16_to_cpu(cqe->rlid);
		wc->sl		   = cqe->sl >> 4;
		g_mlpath_rqpn	   = be32_to_cpu(cqe->g_mlpath_rqpn);
		wc->src_qp	   = g_mlpath_rqpn & 0xffffff;
		wc->dlid_path_bits = (g_mlpath_rqpn >> 24) & 0x7f;
		wc->wc_flags	  |= g_mlpath_rqpn & 0x80000000 ? IB_WC_GRH : 0;
		wc->pkey_index     = be32_to_cpu(cqe->immed_rss_invalid) & 0x7f;
	}

	return 0;
}

int mlx4_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct mlx4_ib_cq *cq = to_mcq(ibcq);
	struct mlx4_ib_qp *cur_qp = NULL;
	unsigned long flags;
	int npolled;
	int err = 0;

	spin_lock_irqsave(&cq->lock, flags);

	for (npolled = 0; npolled < num_entries; ++npolled) {
		err = mlx4_ib_poll_one(cq, &cur_qp, wc + npolled);
		if (err)
			break;
	}

	if (npolled)
		mlx4_cq_set_ci(&cq->mcq);

	spin_unlock_irqrestore(&cq->lock, flags);

	if (err == 0 || err == -EAGAIN)
		return npolled;
	else
		return err;
}

int mlx4_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	mlx4_cq_arm(&to_mcq(ibcq)->mcq,
		    (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		    MLX4_CQ_DB_REQ_NOT_SOL : MLX4_CQ_DB_REQ_NOT,
		    to_mdev(ibcq->device)->uar_map,
		    MLX4_GET_DOORBELL_LOCK(&to_mdev(ibcq->device)->uar_lock));

	return 0;
}

void __mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq)
{
	u32 prod_index;
	int nfreed = 0;
	struct mlx4_cqe *cqe, *dest;
	u8 owner_bit;

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->mcq.cons_index; get_sw_cqe(cq, prod_index); ++prod_index)
		if (prod_index == cq->mcq.cons_index + cq->ibcq.cqe)
			break;

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->mcq.cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibcq.cqe);
		if ((be32_to_cpu(cqe->my_qpn) & 0xffffff) == qpn) {
			if (srq && !(cqe->owner_sr_opcode & MLX4_CQE_IS_SEND_MASK))
				mlx4_ib_free_srq_wqe(srq, be16_to_cpu(cqe->wqe_index));
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(cq, (prod_index + nfreed) & cq->ibcq.cqe);
			owner_bit = dest->owner_sr_opcode & MLX4_CQE_OWNER_MASK;
			memcpy(dest, cqe, sizeof *cqe);
			dest->owner_sr_opcode = owner_bit |
				(dest->owner_sr_opcode & ~MLX4_CQE_OWNER_MASK);
		}
	}

	if (nfreed) {
		cq->mcq.cons_index += nfreed;
		/*
		 * Make sure update of buffer contents is done before
		 * updating consumer index.
		 */
		wmb();
		mlx4_cq_set_ci(&cq->mcq);
	}
}

void mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq)
{
	spin_lock_irq(&cq->lock);
	__mlx4_ib_cq_clean(cq, qpn, srq);
	spin_unlock_irq(&cq->lock);
}
