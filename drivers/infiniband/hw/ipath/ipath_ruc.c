/*
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#include "ipath_verbs.h"

/*
 * Convert the AETH RNR timeout code into the number of milliseconds.
 */
const u32 ib_ipath_rnr_table[32] = {
	656,			/* 0 */
	1,			/* 1 */
	1,			/* 2 */
	1,			/* 3 */
	1,			/* 4 */
	1,			/* 5 */
	1,			/* 6 */
	1,			/* 7 */
	1,			/* 8 */
	1,			/* 9 */
	1,			/* A */
	1,			/* B */
	1,			/* C */
	1,			/* D */
	2,			/* E */
	2,			/* F */
	3,			/* 10 */
	4,			/* 11 */
	6,			/* 12 */
	8,			/* 13 */
	11,			/* 14 */
	16,			/* 15 */
	21,			/* 16 */
	31,			/* 17 */
	41,			/* 18 */
	62,			/* 19 */
	82,			/* 1A */
	123,			/* 1B */
	164,			/* 1C */
	246,			/* 1D */
	328,			/* 1E */
	492			/* 1F */
};

/**
 * ipath_insert_rnr_queue - put QP on the RNR timeout list for the device
 * @qp: the QP
 *
 * XXX Use a simple list for now.  We might need a priority
 * queue if we have lots of QPs waiting for RNR timeouts
 * but that should be rare.
 */
void ipath_insert_rnr_queue(struct ipath_qp *qp)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	unsigned long flags;

	spin_lock_irqsave(&dev->pending_lock, flags);
	if (list_empty(&dev->rnrwait))
		list_add(&qp->timerwait, &dev->rnrwait);
	else {
		struct list_head *l = &dev->rnrwait;
		struct ipath_qp *nqp = list_entry(l->next, struct ipath_qp,
						  timerwait);

		while (qp->s_rnr_timeout >= nqp->s_rnr_timeout) {
			qp->s_rnr_timeout -= nqp->s_rnr_timeout;
			l = l->next;
			if (l->next == &dev->rnrwait)
				break;
			nqp = list_entry(l->next, struct ipath_qp,
					 timerwait);
		}
		list_add(&qp->timerwait, l);
	}
	spin_unlock_irqrestore(&dev->pending_lock, flags);
}

/**
 * ipath_get_rwqe - copy the next RWQE into the QP's RWQE
 * @qp: the QP
 * @wr_id_only: update wr_id only, not SGEs
 *
 * Return 0 if no RWQE is available, otherwise return 1.
 *
 * Called at interrupt level with the QP r_rq.lock held.
 */
int ipath_get_rwqe(struct ipath_qp *qp, int wr_id_only)
{
	struct ipath_rq *rq;
	struct ipath_srq *srq;
	struct ipath_rwqe *wqe;
	int ret;

	if (!qp->ibqp.srq) {
		rq = &qp->r_rq;
		if (unlikely(rq->tail == rq->head)) {
			ret = 0;
			goto bail;
		}
		wqe = get_rwqe_ptr(rq, rq->tail);
		qp->r_wr_id = wqe->wr_id;
		if (!wr_id_only) {
			qp->r_sge.sge = wqe->sg_list[0];
			qp->r_sge.sg_list = wqe->sg_list + 1;
			qp->r_sge.num_sge = wqe->num_sge;
			qp->r_len = wqe->length;
		}
		if (++rq->tail >= rq->size)
			rq->tail = 0;
		ret = 1;
		goto bail;
	}

	srq = to_isrq(qp->ibqp.srq);
	rq = &srq->rq;
	spin_lock(&rq->lock);
	if (unlikely(rq->tail == rq->head)) {
		spin_unlock(&rq->lock);
		ret = 0;
		goto bail;
	}
	wqe = get_rwqe_ptr(rq, rq->tail);
	qp->r_wr_id = wqe->wr_id;
	if (!wr_id_only) {
		qp->r_sge.sge = wqe->sg_list[0];
		qp->r_sge.sg_list = wqe->sg_list + 1;
		qp->r_sge.num_sge = wqe->num_sge;
		qp->r_len = wqe->length;
	}
	if (++rq->tail >= rq->size)
		rq->tail = 0;
	if (srq->ibsrq.event_handler) {
		struct ib_event ev;
		u32 n;

		if (rq->head < rq->tail)
			n = rq->size + rq->head - rq->tail;
		else
			n = rq->head - rq->tail;
		if (n < srq->limit) {
			srq->limit = 0;
			spin_unlock(&rq->lock);
			ev.device = qp->ibqp.device;
			ev.element.srq = qp->ibqp.srq;
			ev.event = IB_EVENT_SRQ_LIMIT_REACHED;
			srq->ibsrq.event_handler(&ev,
						 srq->ibsrq.srq_context);
		} else
			spin_unlock(&rq->lock);
	} else
		spin_unlock(&rq->lock);
	ret = 1;

bail:
	return ret;
}

/**
 * ipath_ruc_loopback - handle UC and RC lookback requests
 * @sqp: the loopback QP
 * @wc: the work completion entry
 *
 * This is called from ipath_do_uc_send() or ipath_do_rc_send() to
 * forward a WQE addressed to the same HCA.
 * Note that although we are single threaded due to the tasklet, we still
 * have to protect against post_send().  We don't have to worry about
 * receive interrupts since this is a connected protocol and all packets
 * will pass through here.
 */
void ipath_ruc_loopback(struct ipath_qp *sqp, struct ib_wc *wc)
{
	struct ipath_ibdev *dev = to_idev(sqp->ibqp.device);
	struct ipath_qp *qp;
	struct ipath_swqe *wqe;
	struct ipath_sge *sge;
	unsigned long flags;
	u64 sdata;

	qp = ipath_lookup_qpn(&dev->qp_table, sqp->remote_qpn);
	if (!qp) {
		dev->n_pkt_drops++;
		return;
	}

again:
	spin_lock_irqsave(&sqp->s_lock, flags);

	if (!(ib_ipath_state_ops[sqp->state] & IPATH_PROCESS_SEND_OK)) {
		spin_unlock_irqrestore(&sqp->s_lock, flags);
		goto done;
	}

	/* Get the next send request. */
	if (sqp->s_last == sqp->s_head) {
		/* Send work queue is empty. */
		spin_unlock_irqrestore(&sqp->s_lock, flags);
		goto done;
	}

	/*
	 * We can rely on the entry not changing without the s_lock
	 * being held until we update s_last.
	 */
	wqe = get_swqe_ptr(sqp, sqp->s_last);
	spin_unlock_irqrestore(&sqp->s_lock, flags);

	wc->wc_flags = 0;
	wc->imm_data = 0;

	sqp->s_sge.sge = wqe->sg_list[0];
	sqp->s_sge.sg_list = wqe->sg_list + 1;
	sqp->s_sge.num_sge = wqe->wr.num_sge;
	sqp->s_len = wqe->length;
	switch (wqe->wr.opcode) {
	case IB_WR_SEND_WITH_IMM:
		wc->wc_flags = IB_WC_WITH_IMM;
		wc->imm_data = wqe->wr.imm_data;
		/* FALLTHROUGH */
	case IB_WR_SEND:
		spin_lock_irqsave(&qp->r_rq.lock, flags);
		if (!ipath_get_rwqe(qp, 0)) {
		rnr_nak:
			spin_unlock_irqrestore(&qp->r_rq.lock, flags);
			/* Handle RNR NAK */
			if (qp->ibqp.qp_type == IB_QPT_UC)
				goto send_comp;
			if (sqp->s_rnr_retry == 0) {
				wc->status = IB_WC_RNR_RETRY_EXC_ERR;
				goto err;
			}
			if (sqp->s_rnr_retry_cnt < 7)
				sqp->s_rnr_retry--;
			dev->n_rnr_naks++;
			sqp->s_rnr_timeout =
				ib_ipath_rnr_table[sqp->s_min_rnr_timer];
			ipath_insert_rnr_queue(sqp);
			goto done;
		}
		spin_unlock_irqrestore(&qp->r_rq.lock, flags);
		break;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		wc->wc_flags = IB_WC_WITH_IMM;
		wc->imm_data = wqe->wr.imm_data;
		spin_lock_irqsave(&qp->r_rq.lock, flags);
		if (!ipath_get_rwqe(qp, 1))
			goto rnr_nak;
		spin_unlock_irqrestore(&qp->r_rq.lock, flags);
		/* FALLTHROUGH */
	case IB_WR_RDMA_WRITE:
		if (wqe->length == 0)
			break;
		if (unlikely(!ipath_rkey_ok(dev, &qp->r_sge, wqe->length,
					    wqe->wr.wr.rdma.remote_addr,
					    wqe->wr.wr.rdma.rkey,
					    IB_ACCESS_REMOTE_WRITE))) {
		acc_err:
			wc->status = IB_WC_REM_ACCESS_ERR;
		err:
			wc->wr_id = wqe->wr.wr_id;
			wc->opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
			wc->vendor_err = 0;
			wc->byte_len = 0;
			wc->qp_num = sqp->ibqp.qp_num;
			wc->src_qp = sqp->remote_qpn;
			wc->pkey_index = 0;
			wc->slid = sqp->remote_ah_attr.dlid;
			wc->sl = sqp->remote_ah_attr.sl;
			wc->dlid_path_bits = 0;
			wc->port_num = 0;
			ipath_sqerror_qp(sqp, wc);
			goto done;
		}
		break;

	case IB_WR_RDMA_READ:
		if (unlikely(!ipath_rkey_ok(dev, &sqp->s_sge, wqe->length,
					    wqe->wr.wr.rdma.remote_addr,
					    wqe->wr.wr.rdma.rkey,
					    IB_ACCESS_REMOTE_READ)))
			goto acc_err;
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_READ)))
			goto acc_err;
		qp->r_sge.sge = wqe->sg_list[0];
		qp->r_sge.sg_list = wqe->sg_list + 1;
		qp->r_sge.num_sge = wqe->wr.num_sge;
		break;

	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		if (unlikely(!ipath_rkey_ok(dev, &qp->r_sge, sizeof(u64),
					    wqe->wr.wr.rdma.remote_addr,
					    wqe->wr.wr.rdma.rkey,
					    IB_ACCESS_REMOTE_ATOMIC)))
			goto acc_err;
		/* Perform atomic OP and save result. */
		sdata = wqe->wr.wr.atomic.swap;
		spin_lock_irqsave(&dev->pending_lock, flags);
		qp->r_atomic_data = *(u64 *) qp->r_sge.sge.vaddr;
		if (wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD)
			*(u64 *) qp->r_sge.sge.vaddr =
				qp->r_atomic_data + sdata;
		else if (qp->r_atomic_data == wqe->wr.wr.atomic.compare_add)
			*(u64 *) qp->r_sge.sge.vaddr = sdata;
		spin_unlock_irqrestore(&dev->pending_lock, flags);
		*(u64 *) sqp->s_sge.sge.vaddr = qp->r_atomic_data;
		goto send_comp;

	default:
		goto done;
	}

	sge = &sqp->s_sge.sge;
	while (sqp->s_len) {
		u32 len = sqp->s_len;

		if (len > sge->length)
			len = sge->length;
		BUG_ON(len == 0);
		ipath_copy_sge(&qp->r_sge, sge->vaddr, len);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--sqp->s_sge.num_sge)
				*sge = *sqp->s_sge.sg_list++;
		} else if (sge->length == 0 && sge->mr != NULL) {
			if (++sge->n >= IPATH_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}
		sqp->s_len -= len;
	}

	if (wqe->wr.opcode == IB_WR_RDMA_WRITE ||
	    wqe->wr.opcode == IB_WR_RDMA_READ)
		goto send_comp;

	if (wqe->wr.opcode == IB_WR_RDMA_WRITE_WITH_IMM)
		wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
	else
		wc->opcode = IB_WC_RECV;
	wc->wr_id = qp->r_wr_id;
	wc->status = IB_WC_SUCCESS;
	wc->vendor_err = 0;
	wc->byte_len = wqe->length;
	wc->qp_num = qp->ibqp.qp_num;
	wc->src_qp = qp->remote_qpn;
	/* XXX do we know which pkey matched? Only needed for GSI. */
	wc->pkey_index = 0;
	wc->slid = qp->remote_ah_attr.dlid;
	wc->sl = qp->remote_ah_attr.sl;
	wc->dlid_path_bits = 0;
	/* Signal completion event if the solicited bit is set. */
	ipath_cq_enter(to_icq(qp->ibqp.recv_cq), wc,
		       wqe->wr.send_flags & IB_SEND_SOLICITED);

send_comp:
	sqp->s_rnr_retry = sqp->s_rnr_retry_cnt;

	if (!test_bit(IPATH_S_SIGNAL_REQ_WR, &sqp->s_flags) ||
	    (wqe->wr.send_flags & IB_SEND_SIGNALED)) {
		wc->wr_id = wqe->wr.wr_id;
		wc->status = IB_WC_SUCCESS;
		wc->opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		wc->vendor_err = 0;
		wc->byte_len = wqe->length;
		wc->qp_num = sqp->ibqp.qp_num;
		wc->src_qp = 0;
		wc->pkey_index = 0;
		wc->slid = 0;
		wc->sl = 0;
		wc->dlid_path_bits = 0;
		wc->port_num = 0;
		ipath_cq_enter(to_icq(sqp->ibqp.send_cq), wc, 0);
	}

	/* Update s_last now that we are finished with the SWQE */
	spin_lock_irqsave(&sqp->s_lock, flags);
	if (++sqp->s_last >= sqp->s_size)
		sqp->s_last = 0;
	spin_unlock_irqrestore(&sqp->s_lock, flags);
	goto again;

done:
	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
}

/**
 * ipath_no_bufs_available - tell the layer driver we need buffers
 * @qp: the QP that caused the problem
 * @dev: the device we ran out of buffers on
 *
 * Called when we run out of PIO buffers.
 */
void ipath_no_bufs_available(struct ipath_qp *qp, struct ipath_ibdev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->pending_lock, flags);
	if (qp->piowait.next == LIST_POISON1)
		list_add_tail(&qp->piowait, &dev->piowait);
	spin_unlock_irqrestore(&dev->pending_lock, flags);
	/*
	 * Note that as soon as ipath_layer_want_buffer() is called and
	 * possibly before it returns, ipath_ib_piobufavail()
	 * could be called.  If we are still in the tasklet function,
	 * tasklet_hi_schedule() will not call us until the next time
	 * tasklet_hi_schedule() is called.
	 * We clear the tasklet flag now since we are committing to return
	 * from the tasklet function.
	 */
	clear_bit(IPATH_S_BUSY, &qp->s_flags);
	tasklet_unlock(&qp->s_task);
	ipath_layer_want_buffer(dev->dd);
	dev->n_piowait++;
}

/**
 * ipath_post_rc_send - post RC and UC sends
 * @qp: the QP to post on
 * @wr: the work request to send
 */
int ipath_post_rc_send(struct ipath_qp *qp, struct ib_send_wr *wr)
{
	struct ipath_swqe *wqe;
	unsigned long flags;
	u32 next;
	int i, j;
	int acc;
	int ret;

	/*
	 * Don't allow RDMA reads or atomic operations on UC or
	 * undefined operations.
	 * Make sure buffer is large enough to hold the result for atomics.
	 */
	if (qp->ibqp.qp_type == IB_QPT_UC) {
		if ((unsigned) wr->opcode >= IB_WR_RDMA_READ) {
			ret = -EINVAL;
			goto bail;
		}
	} else if ((unsigned) wr->opcode > IB_WR_ATOMIC_FETCH_AND_ADD) {
		ret = -EINVAL;
		goto bail;
	} else if (wr->opcode >= IB_WR_ATOMIC_CMP_AND_SWP &&
		   (wr->num_sge == 0 ||
		    wr->sg_list[0].length < sizeof(u64) ||
		    wr->sg_list[0].addr & (sizeof(u64) - 1))) {
		ret = -EINVAL;
		goto bail;
	}
	/* IB spec says that num_sge == 0 is OK. */
	if (wr->num_sge > qp->s_max_sge) {
		ret = -ENOMEM;
		goto bail;
	}
	spin_lock_irqsave(&qp->s_lock, flags);
	next = qp->s_head + 1;
	if (next >= qp->s_size)
		next = 0;
	if (next == qp->s_last) {
		spin_unlock_irqrestore(&qp->s_lock, flags);
		ret = -EINVAL;
		goto bail;
	}

	wqe = get_swqe_ptr(qp, qp->s_head);
	wqe->wr = *wr;
	wqe->ssn = qp->s_ssn++;
	wqe->sg_list[0].mr = NULL;
	wqe->sg_list[0].vaddr = NULL;
	wqe->sg_list[0].length = 0;
	wqe->sg_list[0].sge_length = 0;
	wqe->length = 0;
	acc = wr->opcode >= IB_WR_RDMA_READ ? IB_ACCESS_LOCAL_WRITE : 0;
	for (i = 0, j = 0; i < wr->num_sge; i++) {
		if (to_ipd(qp->ibqp.pd)->user && wr->sg_list[i].lkey == 0) {
			spin_unlock_irqrestore(&qp->s_lock, flags);
			ret = -EINVAL;
			goto bail;
		}
		if (wr->sg_list[i].length == 0)
			continue;
		if (!ipath_lkey_ok(&to_idev(qp->ibqp.device)->lk_table,
				   &wqe->sg_list[j], &wr->sg_list[i],
				   acc)) {
			spin_unlock_irqrestore(&qp->s_lock, flags);
			ret = -EINVAL;
			goto bail;
		}
		wqe->length += wr->sg_list[i].length;
		j++;
	}
	wqe->wr.num_sge = j;
	qp->s_head = next;
	spin_unlock_irqrestore(&qp->s_lock, flags);

	if (qp->ibqp.qp_type == IB_QPT_UC)
		ipath_do_uc_send((unsigned long) qp);
	else
		ipath_do_rc_send((unsigned long) qp);

	ret = 0;

bail:
	return ret;
}
