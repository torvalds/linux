/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
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

#include <linux/spinlock.h>

#include "ipath_verbs.h"
#include "ipath_kernel.h"

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
			if (l->next == &dev->rnrwait) {
				nqp = NULL;
				break;
			}
			nqp = list_entry(l->next, struct ipath_qp,
					 timerwait);
		}
		if (nqp)
			nqp->s_rnr_timeout -= qp->s_rnr_timeout;
		list_add(&qp->timerwait, l);
	}
	spin_unlock_irqrestore(&dev->pending_lock, flags);
}

/**
 * ipath_init_sge - Validate a RWQE and fill in the SGE state
 * @qp: the QP
 *
 * Return 1 if OK.
 */
int ipath_init_sge(struct ipath_qp *qp, struct ipath_rwqe *wqe,
		   u32 *lengthp, struct ipath_sge_state *ss)
{
	int i, j, ret;
	struct ib_wc wc;

	*lengthp = 0;
	for (i = j = 0; i < wqe->num_sge; i++) {
		if (wqe->sg_list[i].length == 0)
			continue;
		/* Check LKEY */
		if (!ipath_lkey_ok(qp, j ? &ss->sg_list[j - 1] : &ss->sge,
				   &wqe->sg_list[i], IB_ACCESS_LOCAL_WRITE))
			goto bad_lkey;
		*lengthp += wqe->sg_list[i].length;
		j++;
	}
	ss->num_sge = j;
	ret = 1;
	goto bail;

bad_lkey:
	wc.wr_id = wqe->wr_id;
	wc.status = IB_WC_LOC_PROT_ERR;
	wc.opcode = IB_WC_RECV;
	wc.vendor_err = 0;
	wc.byte_len = 0;
	wc.imm_data = 0;
	wc.qp = &qp->ibqp;
	wc.src_qp = 0;
	wc.wc_flags = 0;
	wc.pkey_index = 0;
	wc.slid = 0;
	wc.sl = 0;
	wc.dlid_path_bits = 0;
	wc.port_num = 0;
	/* Signal solicited completion event. */
	ipath_cq_enter(to_icq(qp->ibqp.recv_cq), &wc, 1);
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_get_rwqe - copy the next RWQE into the QP's RWQE
 * @qp: the QP
 * @wr_id_only: update wr_id only, not SGEs
 *
 * Return 0 if no RWQE is available, otherwise return 1.
 *
 * Can be called from interrupt level.
 */
int ipath_get_rwqe(struct ipath_qp *qp, int wr_id_only)
{
	unsigned long flags;
	struct ipath_rq *rq;
	struct ipath_rwq *wq;
	struct ipath_srq *srq;
	struct ipath_rwqe *wqe;
	void (*handler)(struct ib_event *, void *);
	u32 tail;
	int ret;

	qp->r_sge.sg_list = qp->r_sg_list;

	if (qp->ibqp.srq) {
		srq = to_isrq(qp->ibqp.srq);
		handler = srq->ibsrq.event_handler;
		rq = &srq->rq;
	} else {
		srq = NULL;
		handler = NULL;
		rq = &qp->r_rq;
	}

	spin_lock_irqsave(&rq->lock, flags);
	wq = rq->wq;
	tail = wq->tail;
	/* Validate tail before using it since it is user writable. */
	if (tail >= rq->size)
		tail = 0;
	do {
		if (unlikely(tail == wq->head)) {
			spin_unlock_irqrestore(&rq->lock, flags);
			ret = 0;
			goto bail;
		}
		/* Make sure entry is read after head index is read. */
		smp_rmb();
		wqe = get_rwqe_ptr(rq, tail);
		if (++tail >= rq->size)
			tail = 0;
	} while (!wr_id_only && !ipath_init_sge(qp, wqe, &qp->r_len,
						&qp->r_sge));
	qp->r_wr_id = wqe->wr_id;
	wq->tail = tail;

	ret = 1;
	qp->r_wrid_valid = 1;
	if (handler) {
		u32 n;

		/*
		 * validate head pointer value and compute
		 * the number of remaining WQEs.
		 */
		n = wq->head;
		if (n >= rq->size)
			n = 0;
		if (n < tail)
			n += rq->size - tail;
		else
			n -= tail;
		if (n < srq->limit) {
			struct ib_event ev;

			srq->limit = 0;
			spin_unlock_irqrestore(&rq->lock, flags);
			ev.device = qp->ibqp.device;
			ev.element.srq = qp->ibqp.srq;
			ev.event = IB_EVENT_SRQ_LIMIT_REACHED;
			handler(&ev, srq->ibsrq.srq_context);
			goto bail;
		}
	}
	spin_unlock_irqrestore(&rq->lock, flags);

bail:
	return ret;
}

/**
 * ipath_ruc_loopback - handle UC and RC lookback requests
 * @sqp: the sending QP
 *
 * This is called from ipath_do_send() to
 * forward a WQE addressed to the same HCA.
 * Note that although we are single threaded due to the tasklet, we still
 * have to protect against post_send().  We don't have to worry about
 * receive interrupts since this is a connected protocol and all packets
 * will pass through here.
 */
static void ipath_ruc_loopback(struct ipath_qp *sqp)
{
	struct ipath_ibdev *dev = to_idev(sqp->ibqp.device);
	struct ipath_qp *qp;
	struct ipath_swqe *wqe;
	struct ipath_sge *sge;
	unsigned long flags;
	struct ib_wc wc;
	u64 sdata;
	atomic64_t *maddr;

	qp = ipath_lookup_qpn(&dev->qp_table, sqp->remote_qpn);
	if (!qp) {
		dev->n_pkt_drops++;
		return;
	}

again:
	spin_lock_irqsave(&sqp->s_lock, flags);

	if (!(ib_ipath_state_ops[sqp->state] & IPATH_PROCESS_SEND_OK) ||
	    sqp->s_rnr_timeout) {
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

	wc.wc_flags = 0;
	wc.imm_data = 0;

	sqp->s_sge.sge = wqe->sg_list[0];
	sqp->s_sge.sg_list = wqe->sg_list + 1;
	sqp->s_sge.num_sge = wqe->wr.num_sge;
	sqp->s_len = wqe->length;
	switch (wqe->wr.opcode) {
	case IB_WR_SEND_WITH_IMM:
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.imm_data = wqe->wr.imm_data;
		/* FALLTHROUGH */
	case IB_WR_SEND:
		if (!ipath_get_rwqe(qp, 0)) {
		rnr_nak:
			/* Handle RNR NAK */
			if (qp->ibqp.qp_type == IB_QPT_UC)
				goto send_comp;
			if (sqp->s_rnr_retry == 0) {
				wc.status = IB_WC_RNR_RETRY_EXC_ERR;
				goto err;
			}
			if (sqp->s_rnr_retry_cnt < 7)
				sqp->s_rnr_retry--;
			dev->n_rnr_naks++;
			sqp->s_rnr_timeout =
				ib_ipath_rnr_table[qp->r_min_rnr_timer];
			ipath_insert_rnr_queue(sqp);
			goto done;
		}
		break;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_WRITE))) {
			wc.status = IB_WC_REM_INV_REQ_ERR;
			goto err;
		}
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.imm_data = wqe->wr.imm_data;
		if (!ipath_get_rwqe(qp, 1))
			goto rnr_nak;
		/* FALLTHROUGH */
	case IB_WR_RDMA_WRITE:
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_WRITE))) {
			wc.status = IB_WC_REM_INV_REQ_ERR;
			goto err;
		}
		if (wqe->length == 0)
			break;
		if (unlikely(!ipath_rkey_ok(qp, &qp->r_sge, wqe->length,
					    wqe->wr.wr.rdma.remote_addr,
					    wqe->wr.wr.rdma.rkey,
					    IB_ACCESS_REMOTE_WRITE))) {
		acc_err:
			wc.status = IB_WC_REM_ACCESS_ERR;
		err:
			wc.wr_id = wqe->wr.wr_id;
			wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
			wc.vendor_err = 0;
			wc.byte_len = 0;
			wc.qp = &sqp->ibqp;
			wc.src_qp = sqp->remote_qpn;
			wc.pkey_index = 0;
			wc.slid = sqp->remote_ah_attr.dlid;
			wc.sl = sqp->remote_ah_attr.sl;
			wc.dlid_path_bits = 0;
			wc.port_num = 0;
			spin_lock_irqsave(&sqp->s_lock, flags);
			ipath_sqerror_qp(sqp, &wc);
			spin_unlock_irqrestore(&sqp->s_lock, flags);
			goto done;
		}
		break;

	case IB_WR_RDMA_READ:
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_READ))) {
			wc.status = IB_WC_REM_INV_REQ_ERR;
			goto err;
		}
		if (unlikely(!ipath_rkey_ok(qp, &sqp->s_sge, wqe->length,
					    wqe->wr.wr.rdma.remote_addr,
					    wqe->wr.wr.rdma.rkey,
					    IB_ACCESS_REMOTE_READ)))
			goto acc_err;
		qp->r_sge.sge = wqe->sg_list[0];
		qp->r_sge.sg_list = wqe->sg_list + 1;
		qp->r_sge.num_sge = wqe->wr.num_sge;
		break;

	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_ATOMIC))) {
			wc.status = IB_WC_REM_INV_REQ_ERR;
			goto err;
		}
		if (unlikely(!ipath_rkey_ok(qp, &qp->r_sge, sizeof(u64),
					    wqe->wr.wr.atomic.remote_addr,
					    wqe->wr.wr.atomic.rkey,
					    IB_ACCESS_REMOTE_ATOMIC)))
			goto acc_err;
		/* Perform atomic OP and save result. */
		maddr = (atomic64_t *) qp->r_sge.sge.vaddr;
		sdata = wqe->wr.wr.atomic.compare_add;
		*(u64 *) sqp->s_sge.sge.vaddr =
			(wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) ?
			(u64) atomic64_add_return(sdata, maddr) - sdata :
			(u64) cmpxchg((u64 *) qp->r_sge.sge.vaddr,
				      sdata, wqe->wr.wr.atomic.swap);
		goto send_comp;

	default:
		goto done;
	}

	sge = &sqp->s_sge.sge;
	while (sqp->s_len) {
		u32 len = sqp->s_len;

		if (len > sge->length)
			len = sge->length;
		if (len > sge->sge_length)
			len = sge->sge_length;
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
		wc.opcode = IB_WC_RECV_RDMA_WITH_IMM;
	else
		wc.opcode = IB_WC_RECV;
	wc.wr_id = qp->r_wr_id;
	wc.status = IB_WC_SUCCESS;
	wc.vendor_err = 0;
	wc.byte_len = wqe->length;
	wc.qp = &qp->ibqp;
	wc.src_qp = qp->remote_qpn;
	wc.pkey_index = 0;
	wc.slid = qp->remote_ah_attr.dlid;
	wc.sl = qp->remote_ah_attr.sl;
	wc.dlid_path_bits = 0;
	wc.port_num = 1;
	/* Signal completion event if the solicited bit is set. */
	ipath_cq_enter(to_icq(qp->ibqp.recv_cq), &wc,
		       wqe->wr.send_flags & IB_SEND_SOLICITED);

send_comp:
	sqp->s_rnr_retry = sqp->s_rnr_retry_cnt;
	ipath_send_complete(sqp, wqe, IB_WC_SUCCESS);
	goto again;

done:
	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
}

static void want_buffer(struct ipath_devdata *dd)
{
	unsigned long flags;

	spin_lock_irqsave(&dd->ipath_sendctrl_lock, flags);
	dd->ipath_sendctrl |= INFINIPATH_S_PIOINTBUFAVAIL;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_sendctrl,
			 dd->ipath_sendctrl);
	ipath_read_kreg64(dd, dd->ipath_kregs->kr_scratch);
	spin_unlock_irqrestore(&dd->ipath_sendctrl_lock, flags);
}

/**
 * ipath_no_bufs_available - tell the layer driver we need buffers
 * @qp: the QP that caused the problem
 * @dev: the device we ran out of buffers on
 *
 * Called when we run out of PIO buffers.
 */
static void ipath_no_bufs_available(struct ipath_qp *qp,
				    struct ipath_ibdev *dev)
{
	unsigned long flags;

	/*
	 * Note that as soon as want_buffer() is called and
	 * possibly before it returns, ipath_ib_piobufavail()
	 * could be called.  If we are still in the tasklet function,
	 * tasklet_hi_schedule() will not call us until the next time
	 * tasklet_hi_schedule() is called.
	 * We leave the busy flag set so that another post send doesn't
	 * try to put the same QP on the piowait list again.
	 */
	spin_lock_irqsave(&dev->pending_lock, flags);
	list_add_tail(&qp->piowait, &dev->piowait);
	spin_unlock_irqrestore(&dev->pending_lock, flags);
	want_buffer(dev->dd);
	dev->n_piowait++;
}

/**
 * ipath_make_grh - construct a GRH header
 * @dev: a pointer to the ipath device
 * @hdr: a pointer to the GRH header being constructed
 * @grh: the global route address to send to
 * @hwords: the number of 32 bit words of header being sent
 * @nwords: the number of 32 bit words of data being sent
 *
 * Return the size of the header in 32 bit words.
 */
u32 ipath_make_grh(struct ipath_ibdev *dev, struct ib_grh *hdr,
		   struct ib_global_route *grh, u32 hwords, u32 nwords)
{
	hdr->version_tclass_flow =
		cpu_to_be32((6 << 28) |
			    (grh->traffic_class << 20) |
			    grh->flow_label);
	hdr->paylen = cpu_to_be16((hwords - 2 + nwords + SIZE_OF_CRC) << 2);
	/* next_hdr is defined by C8-7 in ch. 8.4.1 */
	hdr->next_hdr = 0x1B;
	hdr->hop_limit = grh->hop_limit;
	/* The SGID is 32-bit aligned. */
	hdr->sgid.global.subnet_prefix = dev->gid_prefix;
	hdr->sgid.global.interface_id = dev->dd->ipath_guid;
	hdr->dgid = grh->dgid;

	/* GRH header size in 32-bit words. */
	return sizeof(struct ib_grh) / sizeof(u32);
}

void ipath_make_ruc_header(struct ipath_ibdev *dev, struct ipath_qp *qp,
			   struct ipath_other_headers *ohdr,
			   u32 bth0, u32 bth2)
{
	u16 lrh0;
	u32 nwords;
	u32 extra_bytes;

	/* Construct the header. */
	extra_bytes = -qp->s_cur_size & 3;
	nwords = (qp->s_cur_size + extra_bytes) >> 2;
	lrh0 = IPATH_LRH_BTH;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		qp->s_hdrwords += ipath_make_grh(dev, &qp->s_hdr.u.l.grh,
						 &qp->remote_ah_attr.grh,
						 qp->s_hdrwords, nwords);
		lrh0 = IPATH_LRH_GRH;
	}
	lrh0 |= qp->remote_ah_attr.sl << 4;
	qp->s_hdr.lrh[0] = cpu_to_be16(lrh0);
	qp->s_hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	qp->s_hdr.lrh[2] = cpu_to_be16(qp->s_hdrwords + nwords + SIZE_OF_CRC);
	qp->s_hdr.lrh[3] = cpu_to_be16(dev->dd->ipath_lid);
	bth0 |= ipath_get_pkey(dev->dd, qp->s_pkey_index);
	bth0 |= extra_bytes << 20;
	ohdr->bth[0] = cpu_to_be32(bth0 | (1 << 22));
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(bth2);
}

/**
 * ipath_do_send - perform a send on a QP
 * @data: contains a pointer to the QP
 *
 * Process entries in the send work queue until credit or queue is
 * exhausted.  Only allow one CPU to send a packet per QP (tasklet).
 * Otherwise, two threads could send packets out of order.
 */
void ipath_do_send(unsigned long data)
{
	struct ipath_qp *qp = (struct ipath_qp *)data;
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	int (*make_req)(struct ipath_qp *qp);

	if (test_and_set_bit(IPATH_S_BUSY, &qp->s_busy))
		goto bail;

	if ((qp->ibqp.qp_type == IB_QPT_RC ||
	     qp->ibqp.qp_type == IB_QPT_UC) &&
	    qp->remote_ah_attr.dlid == dev->dd->ipath_lid) {
		ipath_ruc_loopback(qp);
		goto clear;
	}

	if (qp->ibqp.qp_type == IB_QPT_RC)
	       make_req = ipath_make_rc_req;
	else if (qp->ibqp.qp_type == IB_QPT_UC)
	       make_req = ipath_make_uc_req;
	else
	       make_req = ipath_make_ud_req;

again:
	/* Check for a constructed packet to be sent. */
	if (qp->s_hdrwords != 0) {
		/*
		 * If no PIO bufs are available, return.  An interrupt will
		 * call ipath_ib_piobufavail() when one is available.
		 */
		if (ipath_verbs_send(qp, &qp->s_hdr, qp->s_hdrwords,
				     qp->s_cur_sge, qp->s_cur_size)) {
			ipath_no_bufs_available(qp, dev);
			goto bail;
		}
		dev->n_unicast_xmit++;
		/* Record that we sent the packet and s_hdr is empty. */
		qp->s_hdrwords = 0;
	}

	if (make_req(qp))
		goto again;
clear:
	clear_bit(IPATH_S_BUSY, &qp->s_busy);
bail:;
}

void ipath_send_complete(struct ipath_qp *qp, struct ipath_swqe *wqe,
			 enum ib_wc_status status)
{
	unsigned long flags;
	u32 last;

	/* See ch. 11.2.4.1 and 10.7.3.1 */
	if (!(qp->s_flags & IPATH_S_SIGNAL_REQ_WR) ||
	    (wqe->wr.send_flags & IB_SEND_SIGNALED) ||
	    status != IB_WC_SUCCESS) {
		struct ib_wc wc;

		wc.wr_id = wqe->wr.wr_id;
		wc.status = status;
		wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		wc.vendor_err = 0;
		wc.byte_len = wqe->length;
		wc.imm_data = 0;
		wc.qp = &qp->ibqp;
		wc.src_qp = 0;
		wc.wc_flags = 0;
		wc.pkey_index = 0;
		wc.slid = 0;
		wc.sl = 0;
		wc.dlid_path_bits = 0;
		wc.port_num = 0;
		ipath_cq_enter(to_icq(qp->ibqp.send_cq), &wc, 0);
	}

	spin_lock_irqsave(&qp->s_lock, flags);
	last = qp->s_last;
	if (++last >= qp->s_size)
		last = 0;
	qp->s_last = last;
	spin_unlock_irqrestore(&qp->s_lock, flags);
}
