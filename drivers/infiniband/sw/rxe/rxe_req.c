// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/skbuff.h>
#include <crypto/hash.h>

#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"

static int next_opcode(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
		       u32 opcode);

static inline void retry_first_write_send(struct rxe_qp *qp,
					  struct rxe_send_wqe *wqe, int npsn)
{
	int i;

	for (i = 0; i < npsn; i++) {
		int to_send = (wqe->dma.resid > qp->mtu) ?
				qp->mtu : wqe->dma.resid;

		qp->req.opcode = next_opcode(qp, wqe,
					     wqe->wr.opcode);

		if (wqe->wr.send_flags & IB_SEND_INLINE) {
			wqe->dma.resid -= to_send;
			wqe->dma.sge_offset += to_send;
		} else {
			advance_dma_data(&wqe->dma, to_send);
		}
	}
}

static void req_retry(struct rxe_qp *qp)
{
	struct rxe_send_wqe *wqe;
	unsigned int wqe_index;
	unsigned int mask;
	int npsn;
	int first = 1;
	struct rxe_queue *q = qp->sq.queue;
	unsigned int cons;
	unsigned int prod;

	cons = queue_get_consumer(q, QUEUE_TYPE_FROM_CLIENT);
	prod = queue_get_producer(q, QUEUE_TYPE_FROM_CLIENT);

	qp->req.wqe_index	= cons;
	qp->req.psn		= qp->comp.psn;
	qp->req.opcode		= -1;

	for (wqe_index = cons; wqe_index != prod;
			wqe_index = queue_next_index(q, wqe_index)) {
		wqe = queue_addr_from_index(qp->sq.queue, wqe_index);
		mask = wr_opcode_mask(wqe->wr.opcode, qp);

		if (wqe->state == wqe_state_posted)
			break;

		if (wqe->state == wqe_state_done)
			continue;

		wqe->iova = (mask & WR_ATOMIC_MASK) ?
			     wqe->wr.wr.atomic.remote_addr :
			     (mask & WR_READ_OR_WRITE_MASK) ?
			     wqe->wr.wr.rdma.remote_addr :
			     0;

		if (!first || (mask & WR_READ_MASK) == 0) {
			wqe->dma.resid = wqe->dma.length;
			wqe->dma.cur_sge = 0;
			wqe->dma.sge_offset = 0;
		}

		if (first) {
			first = 0;

			if (mask & WR_WRITE_OR_SEND_MASK) {
				npsn = (qp->comp.psn - wqe->first_psn) &
					BTH_PSN_MASK;
				retry_first_write_send(qp, wqe, npsn);
			}

			if (mask & WR_READ_MASK) {
				npsn = (wqe->dma.length - wqe->dma.resid) /
					qp->mtu;
				wqe->iova += npsn * qp->mtu;
			}
		}

		wqe->state = wqe_state_posted;
	}
}

void rnr_nak_timer(struct timer_list *t)
{
	struct rxe_qp *qp = from_timer(qp, t, rnr_nak_timer);
	unsigned long flags;

	rxe_dbg_qp(qp, "nak timer fired\n");

	spin_lock_irqsave(&qp->state_lock, flags);
	if (qp->valid) {
		/* request a send queue retry */
		qp->req.need_retry = 1;
		qp->req.wait_for_rnr_timer = 0;
		rxe_sched_task(&qp->send_task);
	}
	spin_unlock_irqrestore(&qp->state_lock, flags);
}

static void req_check_sq_drain_done(struct rxe_qp *qp)
{
	struct rxe_queue *q;
	unsigned int index;
	unsigned int cons;
	struct rxe_send_wqe *wqe;
	unsigned long flags;

	spin_lock_irqsave(&qp->state_lock, flags);
	if (qp_state(qp) == IB_QPS_SQD) {
		q = qp->sq.queue;
		index = qp->req.wqe_index;
		cons = queue_get_consumer(q, QUEUE_TYPE_FROM_CLIENT);
		wqe = queue_addr_from_index(q, cons);

		/* check to see if we are drained;
		 * state_lock used by requester and completer
		 */
		do {
			if (!qp->attr.sq_draining)
				/* comp just finished */
				break;

			if (wqe && ((index != cons) ||
				(wqe->state != wqe_state_posted)))
				/* comp not done yet */
				break;

			qp->attr.sq_draining = 0;
			spin_unlock_irqrestore(&qp->state_lock, flags);

			if (qp->ibqp.event_handler) {
				struct ib_event ev;

				ev.device = qp->ibqp.device;
				ev.element.qp = &qp->ibqp;
				ev.event = IB_EVENT_SQ_DRAINED;
				qp->ibqp.event_handler(&ev,
					qp->ibqp.qp_context);
			}
			return;
		} while (0);
	}
	spin_unlock_irqrestore(&qp->state_lock, flags);
}

static struct rxe_send_wqe *__req_next_wqe(struct rxe_qp *qp)
{
	struct rxe_queue *q = qp->sq.queue;
	unsigned int index = qp->req.wqe_index;
	unsigned int prod;

	prod = queue_get_producer(q, QUEUE_TYPE_FROM_CLIENT);
	if (index == prod)
		return NULL;
	else
		return queue_addr_from_index(q, index);
}

static struct rxe_send_wqe *req_next_wqe(struct rxe_qp *qp)
{
	struct rxe_send_wqe *wqe;
	unsigned long flags;

	req_check_sq_drain_done(qp);

	wqe = __req_next_wqe(qp);
	if (wqe == NULL)
		return NULL;

	spin_lock_irqsave(&qp->state_lock, flags);
	if (unlikely((qp_state(qp) == IB_QPS_SQD) &&
		     (wqe->state != wqe_state_processing))) {
		spin_unlock_irqrestore(&qp->state_lock, flags);
		return NULL;
	}
	spin_unlock_irqrestore(&qp->state_lock, flags);

	wqe->mask = wr_opcode_mask(wqe->wr.opcode, qp);
	return wqe;
}

/**
 * rxe_wqe_is_fenced - check if next wqe is fenced
 * @qp: the queue pair
 * @wqe: the next wqe
 *
 * Returns: 1 if wqe needs to wait
 *	    0 if wqe is ready to go
 */
static int rxe_wqe_is_fenced(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	/* Local invalidate fence (LIF) see IBA 10.6.5.1
	 * Requires ALL previous operations on the send queue
	 * are complete. Make mandatory for the rxe driver.
	 */
	if (wqe->wr.opcode == IB_WR_LOCAL_INV)
		return qp->req.wqe_index != queue_get_consumer(qp->sq.queue,
						QUEUE_TYPE_FROM_CLIENT);

	/* Fence see IBA 10.8.3.3
	 * Requires that all previous read and atomic operations
	 * are complete.
	 */
	return (wqe->wr.send_flags & IB_SEND_FENCE) &&
		atomic_read(&qp->req.rd_atomic) != qp->attr.max_rd_atomic;
}

static int next_opcode_rc(struct rxe_qp *qp, u32 opcode, int fits)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:
		if (qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_LAST :
				IB_OPCODE_RC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_ONLY :
				IB_OPCODE_RC_RDMA_WRITE_FIRST;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE :
				IB_OPCODE_RC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_RC_RDMA_WRITE_FIRST;

	case IB_WR_SEND:
		if (qp->req.opcode == IB_OPCODE_RC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_RC_SEND_LAST :
				IB_OPCODE_RC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_SEND_ONLY :
				IB_OPCODE_RC_SEND_FIRST;

	case IB_WR_SEND_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_RC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE :
				IB_OPCODE_RC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_SEND_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_RC_SEND_FIRST;

	case IB_WR_FLUSH:
		return IB_OPCODE_RC_FLUSH;

	case IB_WR_RDMA_READ:
		return IB_OPCODE_RC_RDMA_READ_REQUEST;

	case IB_WR_ATOMIC_CMP_AND_SWP:
		return IB_OPCODE_RC_COMPARE_SWAP;

	case IB_WR_ATOMIC_FETCH_AND_ADD:
		return IB_OPCODE_RC_FETCH_ADD;

	case IB_WR_SEND_WITH_INV:
		if (qp->req.opcode == IB_OPCODE_RC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_SEND_MIDDLE)
			return fits ? IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE :
				IB_OPCODE_RC_SEND_MIDDLE;
		else
			return fits ? IB_OPCODE_RC_SEND_ONLY_WITH_INVALIDATE :
				IB_OPCODE_RC_SEND_FIRST;

	case IB_WR_ATOMIC_WRITE:
		return IB_OPCODE_RC_ATOMIC_WRITE;

	case IB_WR_REG_MR:
	case IB_WR_LOCAL_INV:
		return opcode;
	}

	return -EINVAL;
}

static int next_opcode_uc(struct rxe_qp *qp, u32 opcode, int fits)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:
		if (qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_LAST :
				IB_OPCODE_UC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_ONLY :
				IB_OPCODE_UC_RDMA_WRITE_FIRST;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_LAST_WITH_IMMEDIATE :
				IB_OPCODE_UC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_UC_RDMA_WRITE_FIRST;

	case IB_WR_SEND:
		if (qp->req.opcode == IB_OPCODE_UC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_UC_SEND_LAST :
				IB_OPCODE_UC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_SEND_ONLY :
				IB_OPCODE_UC_SEND_FIRST;

	case IB_WR_SEND_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_UC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_UC_SEND_LAST_WITH_IMMEDIATE :
				IB_OPCODE_UC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_SEND_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_UC_SEND_FIRST;
	}

	return -EINVAL;
}

static int next_opcode(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
		       u32 opcode)
{
	int fits = (wqe->dma.resid <= qp->mtu);

	switch (qp_type(qp)) {
	case IB_QPT_RC:
		return next_opcode_rc(qp, opcode, fits);

	case IB_QPT_UC:
		return next_opcode_uc(qp, opcode, fits);

	case IB_QPT_UD:
	case IB_QPT_GSI:
		switch (opcode) {
		case IB_WR_SEND:
			return IB_OPCODE_UD_SEND_ONLY;

		case IB_WR_SEND_WITH_IMM:
			return IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
		}
		break;

	default:
		break;
	}

	return -EINVAL;
}

static inline int check_init_depth(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	int depth;

	if (wqe->has_rd_atomic)
		return 0;

	qp->req.need_rd_atomic = 1;
	depth = atomic_dec_return(&qp->req.rd_atomic);

	if (depth >= 0) {
		qp->req.need_rd_atomic = 0;
		wqe->has_rd_atomic = 1;
		return 0;
	}

	atomic_inc(&qp->req.rd_atomic);
	return -EAGAIN;
}

static inline int get_mtu(struct rxe_qp *qp)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);

	if ((qp_type(qp) == IB_QPT_RC) || (qp_type(qp) == IB_QPT_UC))
		return qp->mtu;

	return rxe->port.mtu_cap;
}

static struct sk_buff *init_req_packet(struct rxe_qp *qp,
				       struct rxe_av *av,
				       struct rxe_send_wqe *wqe,
				       int opcode, u32 payload,
				       struct rxe_pkt_info *pkt)
{
	struct rxe_dev		*rxe = to_rdev(qp->ibqp.device);
	struct sk_buff		*skb;
	struct rxe_send_wr	*ibwr = &wqe->wr;
	int			pad = (-payload) & 0x3;
	int			paylen;
	int			solicited;
	u32			qp_num;
	int			ack_req = 0;

	/* length from start of bth to end of icrc */
	paylen = rxe_opcode[opcode].length + payload + pad + RXE_ICRC_SIZE;
	pkt->paylen = paylen;

	/* init skb */
	skb = rxe_init_packet(rxe, av, paylen, pkt);
	if (unlikely(!skb))
		return NULL;

	/* init bth */
	solicited = (ibwr->send_flags & IB_SEND_SOLICITED) &&
			(pkt->mask & RXE_END_MASK) &&
			((pkt->mask & (RXE_SEND_MASK)) ||
			(pkt->mask & (RXE_WRITE_MASK | RXE_IMMDT_MASK)) ==
			(RXE_WRITE_MASK | RXE_IMMDT_MASK));

	qp_num = (pkt->mask & RXE_DETH_MASK) ? ibwr->wr.ud.remote_qpn :
					 qp->attr.dest_qp_num;

	if (qp_type(qp) != IB_QPT_UD && qp_type(qp) != IB_QPT_UC)
		ack_req = ((pkt->mask & RXE_END_MASK) ||
			   (qp->req.noack_pkts++ > RXE_MAX_PKT_PER_ACK));
	if (ack_req)
		qp->req.noack_pkts = 0;

	bth_init(pkt, pkt->opcode, solicited, 0, pad, IB_DEFAULT_PKEY_FULL, qp_num,
		 ack_req, pkt->psn);

	/* init optional headers */
	if (pkt->mask & RXE_RETH_MASK) {
		if (pkt->mask & RXE_FETH_MASK)
			reth_set_rkey(pkt, ibwr->wr.flush.rkey);
		else
			reth_set_rkey(pkt, ibwr->wr.rdma.rkey);
		reth_set_va(pkt, wqe->iova);
		reth_set_len(pkt, wqe->dma.resid);
	}

	/* Fill Flush Extension Transport Header */
	if (pkt->mask & RXE_FETH_MASK)
		feth_init(pkt, ibwr->wr.flush.type, ibwr->wr.flush.level);

	if (pkt->mask & RXE_IMMDT_MASK)
		immdt_set_imm(pkt, ibwr->ex.imm_data);

	if (pkt->mask & RXE_IETH_MASK)
		ieth_set_rkey(pkt, ibwr->ex.invalidate_rkey);

	if (pkt->mask & RXE_ATMETH_MASK) {
		atmeth_set_va(pkt, wqe->iova);
		if (opcode == IB_OPCODE_RC_COMPARE_SWAP) {
			atmeth_set_swap_add(pkt, ibwr->wr.atomic.swap);
			atmeth_set_comp(pkt, ibwr->wr.atomic.compare_add);
		} else {
			atmeth_set_swap_add(pkt, ibwr->wr.atomic.compare_add);
		}
		atmeth_set_rkey(pkt, ibwr->wr.atomic.rkey);
	}

	if (pkt->mask & RXE_DETH_MASK) {
		if (qp->ibqp.qp_num == 1)
			deth_set_qkey(pkt, GSI_QKEY);
		else
			deth_set_qkey(pkt, ibwr->wr.ud.remote_qkey);
		deth_set_sqp(pkt, qp->ibqp.qp_num);
	}

	return skb;
}

static int finish_packet(struct rxe_qp *qp, struct rxe_av *av,
			 struct rxe_send_wqe *wqe, struct rxe_pkt_info *pkt,
			 struct sk_buff *skb, u32 payload)
{
	int err;

	err = rxe_prepare(av, pkt, skb);
	if (err)
		return err;

	if (pkt->mask & RXE_WRITE_OR_SEND_MASK) {
		if (wqe->wr.send_flags & IB_SEND_INLINE) {
			u8 *tmp = &wqe->dma.inline_data[wqe->dma.sge_offset];

			memcpy(payload_addr(pkt), tmp, payload);

			wqe->dma.resid -= payload;
			wqe->dma.sge_offset += payload;
		} else {
			err = copy_data(qp->pd, 0, &wqe->dma,
					payload_addr(pkt), payload,
					RXE_FROM_MR_OBJ);
			if (err)
				return err;
		}
		if (bth_pad(pkt)) {
			u8 *pad = payload_addr(pkt) + payload;

			memset(pad, 0, bth_pad(pkt));
		}
	} else if (pkt->mask & RXE_FLUSH_MASK) {
		/* oA19-2: shall have no payload. */
		wqe->dma.resid = 0;
	}

	if (pkt->mask & RXE_ATOMIC_WRITE_MASK) {
		memcpy(payload_addr(pkt), wqe->dma.atomic_wr, payload);
		wqe->dma.resid -= payload;
	}

	return 0;
}

static void update_wqe_state(struct rxe_qp *qp,
		struct rxe_send_wqe *wqe,
		struct rxe_pkt_info *pkt)
{
	if (pkt->mask & RXE_END_MASK) {
		if (qp_type(qp) == IB_QPT_RC)
			wqe->state = wqe_state_pending;
		else
			wqe->state = wqe_state_done;
	} else {
		wqe->state = wqe_state_processing;
	}
}

static void update_wqe_psn(struct rxe_qp *qp,
			   struct rxe_send_wqe *wqe,
			   struct rxe_pkt_info *pkt,
			   u32 payload)
{
	/* number of packets left to send including current one */
	int num_pkt = (wqe->dma.resid + payload + qp->mtu - 1) / qp->mtu;

	/* handle zero length packet case */
	if (num_pkt == 0)
		num_pkt = 1;

	if (pkt->mask & RXE_START_MASK) {
		wqe->first_psn = qp->req.psn;
		wqe->last_psn = (qp->req.psn + num_pkt - 1) & BTH_PSN_MASK;
	}

	if (pkt->mask & RXE_READ_MASK)
		qp->req.psn = (wqe->first_psn + num_pkt) & BTH_PSN_MASK;
	else
		qp->req.psn = (qp->req.psn + 1) & BTH_PSN_MASK;
}

static void update_state(struct rxe_qp *qp, struct rxe_pkt_info *pkt)
{
	qp->req.opcode = pkt->opcode;

	if (pkt->mask & RXE_END_MASK)
		qp->req.wqe_index = queue_next_index(qp->sq.queue,
						     qp->req.wqe_index);

	qp->need_req_skb = 0;

	if (qp->qp_timeout_jiffies && !timer_pending(&qp->retrans_timer))
		mod_timer(&qp->retrans_timer,
			  jiffies + qp->qp_timeout_jiffies);
}

static int rxe_do_local_ops(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	u8 opcode = wqe->wr.opcode;
	u32 rkey;
	int ret;

	switch (opcode) {
	case IB_WR_LOCAL_INV:
		rkey = wqe->wr.ex.invalidate_rkey;
		if (rkey_is_mw(rkey))
			ret = rxe_invalidate_mw(qp, rkey);
		else
			ret = rxe_invalidate_mr(qp, rkey);

		if (unlikely(ret)) {
			wqe->status = IB_WC_LOC_QP_OP_ERR;
			return ret;
		}
		break;
	case IB_WR_REG_MR:
		ret = rxe_reg_fast_mr(qp, wqe);
		if (unlikely(ret)) {
			wqe->status = IB_WC_LOC_QP_OP_ERR;
			return ret;
		}
		break;
	case IB_WR_BIND_MW:
		ret = rxe_bind_mw(qp, wqe);
		if (unlikely(ret)) {
			wqe->status = IB_WC_MW_BIND_ERR;
			return ret;
		}
		break;
	default:
		rxe_dbg_qp(qp, "Unexpected send wqe opcode %d\n", opcode);
		wqe->status = IB_WC_LOC_QP_OP_ERR;
		return -EINVAL;
	}

	wqe->state = wqe_state_done;
	wqe->status = IB_WC_SUCCESS;
	qp->req.wqe_index = queue_next_index(qp->sq.queue, qp->req.wqe_index);

	return 0;
}

int rxe_requester(struct rxe_qp *qp)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_pkt_info pkt;
	struct sk_buff *skb;
	struct rxe_send_wqe *wqe;
	enum rxe_hdr_mask mask;
	u32 payload;
	int mtu;
	int opcode;
	int err;
	int ret;
	struct rxe_queue *q = qp->sq.queue;
	struct rxe_ah *ah;
	struct rxe_av *av;
	unsigned long flags;

	spin_lock_irqsave(&qp->state_lock, flags);
	if (unlikely(!qp->valid)) {
		spin_unlock_irqrestore(&qp->state_lock, flags);
		goto exit;
	}

	if (unlikely(qp_state(qp) == IB_QPS_ERR)) {
		wqe = __req_next_wqe(qp);
		spin_unlock_irqrestore(&qp->state_lock, flags);
		if (wqe) {
			wqe->status = IB_WC_WR_FLUSH_ERR;
			goto err;
		} else {
			goto exit;
		}
	}

	if (unlikely(qp_state(qp) == IB_QPS_RESET)) {
		qp->req.wqe_index = queue_get_consumer(q,
						QUEUE_TYPE_FROM_CLIENT);
		qp->req.opcode = -1;
		qp->req.need_rd_atomic = 0;
		qp->req.wait_psn = 0;
		qp->req.need_retry = 0;
		qp->req.wait_for_rnr_timer = 0;
		spin_unlock_irqrestore(&qp->state_lock, flags);
		goto exit;
	}
	spin_unlock_irqrestore(&qp->state_lock, flags);

	/* we come here if the retransmit timer has fired
	 * or if the rnr timer has fired. If the retransmit
	 * timer fires while we are processing an RNR NAK wait
	 * until the rnr timer has fired before starting the
	 * retry flow
	 */
	if (unlikely(qp->req.need_retry && !qp->req.wait_for_rnr_timer)) {
		req_retry(qp);
		qp->req.need_retry = 0;
	}

	wqe = req_next_wqe(qp);
	if (unlikely(!wqe))
		goto exit;

	if (rxe_wqe_is_fenced(qp, wqe)) {
		qp->req.wait_fence = 1;
		goto exit;
	}

	if (wqe->mask & WR_LOCAL_OP_MASK) {
		err = rxe_do_local_ops(qp, wqe);
		if (unlikely(err))
			goto err;
		else
			goto done;
	}

	if (unlikely(qp_type(qp) == IB_QPT_RC &&
		psn_compare(qp->req.psn, (qp->comp.psn +
				RXE_MAX_UNACKED_PSNS)) > 0)) {
		qp->req.wait_psn = 1;
		goto exit;
	}

	/* Limit the number of inflight SKBs per QP */
	if (unlikely(atomic_read(&qp->skb_out) >
		     RXE_INFLIGHT_SKBS_PER_QP_HIGH)) {
		qp->need_req_skb = 1;
		goto exit;
	}

	opcode = next_opcode(qp, wqe, wqe->wr.opcode);
	if (unlikely(opcode < 0)) {
		wqe->status = IB_WC_LOC_QP_OP_ERR;
		goto err;
	}

	mask = rxe_opcode[opcode].mask;
	if (unlikely(mask & (RXE_READ_OR_ATOMIC_MASK |
			RXE_ATOMIC_WRITE_MASK))) {
		if (check_init_depth(qp, wqe))
			goto exit;
	}

	mtu = get_mtu(qp);
	payload = (mask & (RXE_WRITE_OR_SEND_MASK | RXE_ATOMIC_WRITE_MASK)) ?
			wqe->dma.resid : 0;
	if (payload > mtu) {
		if (qp_type(qp) == IB_QPT_UD) {
			/* C10-93.1.1: If the total sum of all the buffer lengths specified for a
			 * UD message exceeds the MTU of the port as returned by QueryHCA, the CI
			 * shall not emit any packets for this message. Further, the CI shall not
			 * generate an error due to this condition.
			 */

			/* fake a successful UD send */
			wqe->first_psn = qp->req.psn;
			wqe->last_psn = qp->req.psn;
			qp->req.psn = (qp->req.psn + 1) & BTH_PSN_MASK;
			qp->req.opcode = IB_OPCODE_UD_SEND_ONLY;
			qp->req.wqe_index = queue_next_index(qp->sq.queue,
						       qp->req.wqe_index);
			wqe->state = wqe_state_done;
			wqe->status = IB_WC_SUCCESS;
			goto done;
		}
		payload = mtu;
	}

	pkt.rxe = rxe;
	pkt.opcode = opcode;
	pkt.qp = qp;
	pkt.psn = qp->req.psn;
	pkt.mask = rxe_opcode[opcode].mask;
	pkt.wqe = wqe;

	av = rxe_get_av(&pkt, &ah);
	if (unlikely(!av)) {
		rxe_dbg_qp(qp, "Failed no address vector\n");
		wqe->status = IB_WC_LOC_QP_OP_ERR;
		goto err;
	}

	skb = init_req_packet(qp, av, wqe, opcode, payload, &pkt);
	if (unlikely(!skb)) {
		rxe_dbg_qp(qp, "Failed allocating skb\n");
		wqe->status = IB_WC_LOC_QP_OP_ERR;
		if (ah)
			rxe_put(ah);
		goto err;
	}

	err = finish_packet(qp, av, wqe, &pkt, skb, payload);
	if (unlikely(err)) {
		rxe_dbg_qp(qp, "Error during finish packet\n");
		if (err == -EFAULT)
			wqe->status = IB_WC_LOC_PROT_ERR;
		else
			wqe->status = IB_WC_LOC_QP_OP_ERR;
		kfree_skb(skb);
		if (ah)
			rxe_put(ah);
		goto err;
	}

	if (ah)
		rxe_put(ah);

	err = rxe_xmit_packet(qp, &pkt, skb);
	if (err) {
		wqe->status = IB_WC_LOC_QP_OP_ERR;
		goto err;
	}

	update_wqe_state(qp, wqe, &pkt);
	update_wqe_psn(qp, wqe, &pkt, payload);
	update_state(qp, &pkt);

	/* A non-zero return value will cause rxe_do_task to
	 * exit its loop and end the work item. A zero return
	 * will continue looping and return to rxe_requester
	 */
done:
	ret = 0;
	goto out;
err:
	/* update wqe_index for each wqe completion */
	qp->req.wqe_index = queue_next_index(qp->sq.queue, qp->req.wqe_index);
	wqe->state = wqe_state_error;
	rxe_qp_error(qp);
exit:
	ret = -EAGAIN;
out:
	return ret;
}

int rxe_sender(struct rxe_qp *qp)
{
	int req_ret;
	int comp_ret;

	/* process the send queue */
	req_ret = rxe_requester(qp);

	/* process the response queue */
	comp_ret = rxe_completer(qp);

	/* exit the task loop if both requester and completer
	 * are ready
	 */
	return (req_ret && comp_ret) ? -EAGAIN : 0;
}
