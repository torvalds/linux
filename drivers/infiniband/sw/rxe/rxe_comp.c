// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/skbuff.h>

#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"
#include "rxe_task.h"

enum comp_state {
	COMPST_GET_ACK,
	COMPST_GET_WQE,
	COMPST_COMP_WQE,
	COMPST_COMP_ACK,
	COMPST_CHECK_PSN,
	COMPST_CHECK_ACK,
	COMPST_READ,
	COMPST_ATOMIC,
	COMPST_WRITE_SEND,
	COMPST_UPDATE_COMP,
	COMPST_ERROR_RETRY,
	COMPST_RNR_RETRY,
	COMPST_ERROR,
	COMPST_EXIT, /* We have an issue, and we want to rerun the completer */
	COMPST_DONE, /* The completer finished successflly */
};

static char *comp_state_name[] =  {
	[COMPST_GET_ACK]		= "GET ACK",
	[COMPST_GET_WQE]		= "GET WQE",
	[COMPST_COMP_WQE]		= "COMP WQE",
	[COMPST_COMP_ACK]		= "COMP ACK",
	[COMPST_CHECK_PSN]		= "CHECK PSN",
	[COMPST_CHECK_ACK]		= "CHECK ACK",
	[COMPST_READ]			= "READ",
	[COMPST_ATOMIC]			= "ATOMIC",
	[COMPST_WRITE_SEND]		= "WRITE/SEND",
	[COMPST_UPDATE_COMP]		= "UPDATE COMP",
	[COMPST_ERROR_RETRY]		= "ERROR RETRY",
	[COMPST_RNR_RETRY]		= "RNR RETRY",
	[COMPST_ERROR]			= "ERROR",
	[COMPST_EXIT]			= "EXIT",
	[COMPST_DONE]			= "DONE",
};

static unsigned long rnrnak_usec[32] = {
	[IB_RNR_TIMER_655_36] = 655360,
	[IB_RNR_TIMER_000_01] = 10,
	[IB_RNR_TIMER_000_02] = 20,
	[IB_RNR_TIMER_000_03] = 30,
	[IB_RNR_TIMER_000_04] = 40,
	[IB_RNR_TIMER_000_06] = 60,
	[IB_RNR_TIMER_000_08] = 80,
	[IB_RNR_TIMER_000_12] = 120,
	[IB_RNR_TIMER_000_16] = 160,
	[IB_RNR_TIMER_000_24] = 240,
	[IB_RNR_TIMER_000_32] = 320,
	[IB_RNR_TIMER_000_48] = 480,
	[IB_RNR_TIMER_000_64] = 640,
	[IB_RNR_TIMER_000_96] = 960,
	[IB_RNR_TIMER_001_28] = 1280,
	[IB_RNR_TIMER_001_92] = 1920,
	[IB_RNR_TIMER_002_56] = 2560,
	[IB_RNR_TIMER_003_84] = 3840,
	[IB_RNR_TIMER_005_12] = 5120,
	[IB_RNR_TIMER_007_68] = 7680,
	[IB_RNR_TIMER_010_24] = 10240,
	[IB_RNR_TIMER_015_36] = 15360,
	[IB_RNR_TIMER_020_48] = 20480,
	[IB_RNR_TIMER_030_72] = 30720,
	[IB_RNR_TIMER_040_96] = 40960,
	[IB_RNR_TIMER_061_44] = 61410,
	[IB_RNR_TIMER_081_92] = 81920,
	[IB_RNR_TIMER_122_88] = 122880,
	[IB_RNR_TIMER_163_84] = 163840,
	[IB_RNR_TIMER_245_76] = 245760,
	[IB_RNR_TIMER_327_68] = 327680,
	[IB_RNR_TIMER_491_52] = 491520,
};

static inline unsigned long rnrnak_jiffies(u8 timeout)
{
	return max_t(unsigned long,
		usecs_to_jiffies(rnrnak_usec[timeout]), 1);
}

static enum ib_wc_opcode wr_to_wc_opcode(enum ib_wr_opcode opcode)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:			return IB_WC_RDMA_WRITE;
	case IB_WR_RDMA_WRITE_WITH_IMM:		return IB_WC_RDMA_WRITE;
	case IB_WR_SEND:			return IB_WC_SEND;
	case IB_WR_SEND_WITH_IMM:		return IB_WC_SEND;
	case IB_WR_RDMA_READ:			return IB_WC_RDMA_READ;
	case IB_WR_ATOMIC_CMP_AND_SWP:		return IB_WC_COMP_SWAP;
	case IB_WR_ATOMIC_FETCH_AND_ADD:	return IB_WC_FETCH_ADD;
	case IB_WR_LSO:				return IB_WC_LSO;
	case IB_WR_SEND_WITH_INV:		return IB_WC_SEND;
	case IB_WR_RDMA_READ_WITH_INV:		return IB_WC_RDMA_READ;
	case IB_WR_LOCAL_INV:			return IB_WC_LOCAL_INV;
	case IB_WR_REG_MR:			return IB_WC_REG_MR;
	case IB_WR_BIND_MW:			return IB_WC_BIND_MW;

	default:
		return 0xff;
	}
}

void retransmit_timer(struct timer_list *t)
{
	struct rxe_qp *qp = from_timer(qp, t, retrans_timer);

	pr_debug("%s: fired for qp#%d\n", __func__, qp->elem.index);

	if (qp->valid) {
		qp->comp.timeout = 1;
		rxe_sched_task(&qp->comp.task);
	}
}

void rxe_comp_queue_pkt(struct rxe_qp *qp, struct sk_buff *skb)
{
	int must_sched;

	skb_queue_tail(&qp->resp_pkts, skb);

	must_sched = skb_queue_len(&qp->resp_pkts) > 1;
	if (must_sched != 0)
		rxe_counter_inc(SKB_TO_PKT(skb)->rxe, RXE_CNT_COMPLETER_SCHED);

	if (must_sched)
		rxe_sched_task(&qp->comp.task);
	else
		rxe_run_task(&qp->comp.task);
}

static inline enum comp_state get_wqe(struct rxe_qp *qp,
				      struct rxe_pkt_info *pkt,
				      struct rxe_send_wqe **wqe_p)
{
	struct rxe_send_wqe *wqe;

	/* we come here whether or not we found a response packet to see if
	 * there are any posted WQEs
	 */
	wqe = queue_head(qp->sq.queue, QUEUE_TYPE_FROM_CLIENT);
	*wqe_p = wqe;

	/* no WQE or requester has not started it yet */
	if (!wqe || wqe->state == wqe_state_posted)
		return pkt ? COMPST_DONE : COMPST_EXIT;

	/* WQE does not require an ack */
	if (wqe->state == wqe_state_done)
		return COMPST_COMP_WQE;

	/* WQE caused an error */
	if (wqe->state == wqe_state_error)
		return COMPST_ERROR;

	/* we have a WQE, if we also have an ack check its PSN */
	return pkt ? COMPST_CHECK_PSN : COMPST_EXIT;
}

static inline void reset_retry_counters(struct rxe_qp *qp)
{
	qp->comp.retry_cnt = qp->attr.retry_cnt;
	qp->comp.rnr_retry = qp->attr.rnr_retry;
	qp->comp.started_retry = 0;
}

static inline enum comp_state check_psn(struct rxe_qp *qp,
					struct rxe_pkt_info *pkt,
					struct rxe_send_wqe *wqe)
{
	s32 diff;

	/* check to see if response is past the oldest WQE. if it is, complete
	 * send/write or error read/atomic
	 */
	diff = psn_compare(pkt->psn, wqe->last_psn);
	if (diff > 0) {
		if (wqe->state == wqe_state_pending) {
			if (wqe->mask & WR_ATOMIC_OR_READ_MASK)
				return COMPST_ERROR_RETRY;

			reset_retry_counters(qp);
			return COMPST_COMP_WQE;
		} else {
			return COMPST_DONE;
		}
	}

	/* compare response packet to expected response */
	diff = psn_compare(pkt->psn, qp->comp.psn);
	if (diff < 0) {
		/* response is most likely a retried packet if it matches an
		 * uncompleted WQE go complete it else ignore it
		 */
		if (pkt->psn == wqe->last_psn)
			return COMPST_COMP_ACK;
		else
			return COMPST_DONE;
	} else if ((diff > 0) && (wqe->mask & WR_ATOMIC_OR_READ_MASK)) {
		return COMPST_DONE;
	} else {
		return COMPST_CHECK_ACK;
	}
}

static inline enum comp_state check_ack(struct rxe_qp *qp,
					struct rxe_pkt_info *pkt,
					struct rxe_send_wqe *wqe)
{
	unsigned int mask = pkt->mask;
	u8 syn;
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);

	/* Check the sequence only */
	switch (qp->comp.opcode) {
	case -1:
		/* Will catch all *_ONLY cases. */
		if (!(mask & RXE_START_MASK))
			return COMPST_ERROR;

		break;

	case IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST:
	case IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE:
		if (pkt->opcode != IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE &&
		    pkt->opcode != IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST) {
			/* read retries of partial data may restart from
			 * read response first or response only.
			 */
			if ((pkt->psn == wqe->first_psn &&
			     pkt->opcode ==
			     IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST) ||
			    (wqe->first_psn == wqe->last_psn &&
			     pkt->opcode ==
			     IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY))
				break;

			return COMPST_ERROR;
		}
		break;
	default:
		WARN_ON_ONCE(1);
	}

	/* Check operation validity. */
	switch (pkt->opcode) {
	case IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST:
	case IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST:
	case IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY:
		syn = aeth_syn(pkt);

		if ((syn & AETH_TYPE_MASK) != AETH_ACK)
			return COMPST_ERROR;

		fallthrough;
		/* (IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE doesn't have an AETH)
		 */
	case IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE:
		if (wqe->wr.opcode != IB_WR_RDMA_READ &&
		    wqe->wr.opcode != IB_WR_RDMA_READ_WITH_INV) {
			wqe->status = IB_WC_FATAL_ERR;
			return COMPST_ERROR;
		}
		reset_retry_counters(qp);
		return COMPST_READ;

	case IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE:
		syn = aeth_syn(pkt);

		if ((syn & AETH_TYPE_MASK) != AETH_ACK)
			return COMPST_ERROR;

		if (wqe->wr.opcode != IB_WR_ATOMIC_CMP_AND_SWP &&
		    wqe->wr.opcode != IB_WR_ATOMIC_FETCH_AND_ADD)
			return COMPST_ERROR;
		reset_retry_counters(qp);
		return COMPST_ATOMIC;

	case IB_OPCODE_RC_ACKNOWLEDGE:
		syn = aeth_syn(pkt);
		switch (syn & AETH_TYPE_MASK) {
		case AETH_ACK:
			reset_retry_counters(qp);
			return COMPST_WRITE_SEND;

		case AETH_RNR_NAK:
			rxe_counter_inc(rxe, RXE_CNT_RCV_RNR);
			return COMPST_RNR_RETRY;

		case AETH_NAK:
			switch (syn) {
			case AETH_NAK_PSN_SEQ_ERROR:
				/* a nak implicitly acks all packets with psns
				 * before
				 */
				if (psn_compare(pkt->psn, qp->comp.psn) > 0) {
					rxe_counter_inc(rxe,
							RXE_CNT_RCV_SEQ_ERR);
					qp->comp.psn = pkt->psn;
					if (qp->req.wait_psn) {
						qp->req.wait_psn = 0;
						rxe_run_task(&qp->req.task);
					}
				}
				return COMPST_ERROR_RETRY;

			case AETH_NAK_INVALID_REQ:
				wqe->status = IB_WC_REM_INV_REQ_ERR;
				return COMPST_ERROR;

			case AETH_NAK_REM_ACC_ERR:
				wqe->status = IB_WC_REM_ACCESS_ERR;
				return COMPST_ERROR;

			case AETH_NAK_REM_OP_ERR:
				wqe->status = IB_WC_REM_OP_ERR;
				return COMPST_ERROR;

			default:
				pr_warn("unexpected nak %x\n", syn);
				wqe->status = IB_WC_REM_OP_ERR;
				return COMPST_ERROR;
			}

		default:
			return COMPST_ERROR;
		}
		break;

	default:
		pr_warn("unexpected opcode\n");
	}

	return COMPST_ERROR;
}

static inline enum comp_state do_read(struct rxe_qp *qp,
				      struct rxe_pkt_info *pkt,
				      struct rxe_send_wqe *wqe)
{
	int ret;

	ret = copy_data(qp->pd, IB_ACCESS_LOCAL_WRITE,
			&wqe->dma, payload_addr(pkt),
			payload_size(pkt), RXE_TO_MR_OBJ);
	if (ret) {
		wqe->status = IB_WC_LOC_PROT_ERR;
		return COMPST_ERROR;
	}

	if (wqe->dma.resid == 0 && (pkt->mask & RXE_END_MASK))
		return COMPST_COMP_ACK;

	return COMPST_UPDATE_COMP;
}

static inline enum comp_state do_atomic(struct rxe_qp *qp,
					struct rxe_pkt_info *pkt,
					struct rxe_send_wqe *wqe)
{
	int ret;

	u64 atomic_orig = atmack_orig(pkt);

	ret = copy_data(qp->pd, IB_ACCESS_LOCAL_WRITE,
			&wqe->dma, &atomic_orig,
			sizeof(u64), RXE_TO_MR_OBJ);
	if (ret) {
		wqe->status = IB_WC_LOC_PROT_ERR;
		return COMPST_ERROR;
	}

	return COMPST_COMP_ACK;
}

static void make_send_cqe(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
			  struct rxe_cqe *cqe)
{
	struct ib_wc *wc = &cqe->ibwc;
	struct ib_uverbs_wc *uwc = &cqe->uibwc;

	memset(cqe, 0, sizeof(*cqe));

	if (!qp->is_user) {
		wc->wr_id = wqe->wr.wr_id;
		wc->status = wqe->status;
		wc->qp = &qp->ibqp;
	} else {
		uwc->wr_id = wqe->wr.wr_id;
		uwc->status = wqe->status;
		uwc->qp_num = qp->ibqp.qp_num;
	}

	if (wqe->status == IB_WC_SUCCESS) {
		if (!qp->is_user) {
			wc->opcode = wr_to_wc_opcode(wqe->wr.opcode);
			if (wqe->wr.opcode == IB_WR_RDMA_WRITE_WITH_IMM ||
			    wqe->wr.opcode == IB_WR_SEND_WITH_IMM)
				wc->wc_flags = IB_WC_WITH_IMM;
			wc->byte_len = wqe->dma.length;
		} else {
			uwc->opcode = wr_to_wc_opcode(wqe->wr.opcode);
			if (wqe->wr.opcode == IB_WR_RDMA_WRITE_WITH_IMM ||
			    wqe->wr.opcode == IB_WR_SEND_WITH_IMM)
				uwc->wc_flags = IB_WC_WITH_IMM;
			uwc->byte_len = wqe->dma.length;
		}
	}
}

/*
 * IBA Spec. Section 10.7.3.1 SIGNALED COMPLETIONS
 * ---------8<---------8<-------------
 * ...Note that if a completion error occurs, a Work Completion
 * will always be generated, even if the signaling
 * indicator requests an Unsignaled Completion.
 * ---------8<---------8<-------------
 */
static void do_complete(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_cqe cqe;
	bool post;

	/* do we need to post a completion */
	post = ((qp->sq_sig_type == IB_SIGNAL_ALL_WR) ||
			(wqe->wr.send_flags & IB_SEND_SIGNALED) ||
			wqe->status != IB_WC_SUCCESS);

	if (post)
		make_send_cqe(qp, wqe, &cqe);

	queue_advance_consumer(qp->sq.queue, QUEUE_TYPE_FROM_CLIENT);

	if (post)
		rxe_cq_post(qp->scq, &cqe, 0);

	if (wqe->wr.opcode == IB_WR_SEND ||
	    wqe->wr.opcode == IB_WR_SEND_WITH_IMM ||
	    wqe->wr.opcode == IB_WR_SEND_WITH_INV)
		rxe_counter_inc(rxe, RXE_CNT_RDMA_SEND);

	/*
	 * we completed something so let req run again
	 * if it is trying to fence
	 */
	if (qp->req.wait_fence) {
		qp->req.wait_fence = 0;
		rxe_run_task(&qp->req.task);
	}
}

static inline enum comp_state complete_ack(struct rxe_qp *qp,
					   struct rxe_pkt_info *pkt,
					   struct rxe_send_wqe *wqe)
{
	if (wqe->has_rd_atomic) {
		wqe->has_rd_atomic = 0;
		atomic_inc(&qp->req.rd_atomic);
		if (qp->req.need_rd_atomic) {
			qp->comp.timeout_retry = 0;
			qp->req.need_rd_atomic = 0;
			rxe_run_task(&qp->req.task);
		}
	}

	if (unlikely(qp->req.state == QP_STATE_DRAIN)) {
		/* state_lock used by requester & completer */
		spin_lock_bh(&qp->state_lock);
		if ((qp->req.state == QP_STATE_DRAIN) &&
		    (qp->comp.psn == qp->req.psn)) {
			qp->req.state = QP_STATE_DRAINED;
			spin_unlock_bh(&qp->state_lock);

			if (qp->ibqp.event_handler) {
				struct ib_event ev;

				ev.device = qp->ibqp.device;
				ev.element.qp = &qp->ibqp;
				ev.event = IB_EVENT_SQ_DRAINED;
				qp->ibqp.event_handler(&ev,
					qp->ibqp.qp_context);
			}
		} else {
			spin_unlock_bh(&qp->state_lock);
		}
	}

	do_complete(qp, wqe);

	if (psn_compare(pkt->psn, qp->comp.psn) >= 0)
		return COMPST_UPDATE_COMP;
	else
		return COMPST_DONE;
}

static inline enum comp_state complete_wqe(struct rxe_qp *qp,
					   struct rxe_pkt_info *pkt,
					   struct rxe_send_wqe *wqe)
{
	if (pkt && wqe->state == wqe_state_pending) {
		if (psn_compare(wqe->last_psn, qp->comp.psn) >= 0) {
			qp->comp.psn = (wqe->last_psn + 1) & BTH_PSN_MASK;
			qp->comp.opcode = -1;
		}

		if (qp->req.wait_psn) {
			qp->req.wait_psn = 0;
			rxe_sched_task(&qp->req.task);
		}
	}

	do_complete(qp, wqe);

	return COMPST_GET_WQE;
}

static void rxe_drain_resp_pkts(struct rxe_qp *qp, bool notify)
{
	struct sk_buff *skb;
	struct rxe_send_wqe *wqe;
	struct rxe_queue *q = qp->sq.queue;

	while ((skb = skb_dequeue(&qp->resp_pkts))) {
		rxe_put(qp);
		kfree_skb(skb);
		ib_device_put(qp->ibqp.device);
	}

	while ((wqe = queue_head(q, q->type))) {
		if (notify) {
			wqe->status = IB_WC_WR_FLUSH_ERR;
			do_complete(qp, wqe);
		} else {
			queue_advance_consumer(q, q->type);
		}
	}
}

static void free_pkt(struct rxe_pkt_info *pkt)
{
	struct sk_buff *skb = PKT_TO_SKB(pkt);
	struct rxe_qp *qp = pkt->qp;
	struct ib_device *dev = qp->ibqp.device;

	kfree_skb(skb);
	rxe_put(qp);
	ib_device_put(dev);
}

int rxe_completer(void *arg)
{
	struct rxe_qp *qp = (struct rxe_qp *)arg;
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_send_wqe *wqe = NULL;
	struct sk_buff *skb = NULL;
	struct rxe_pkt_info *pkt = NULL;
	enum comp_state state;
	int ret;

	if (!rxe_get(qp))
		return -EAGAIN;

	if (!qp->valid || qp->comp.state == QP_STATE_ERROR ||
	    qp->comp.state == QP_STATE_RESET) {
		rxe_drain_resp_pkts(qp, qp->valid &&
				    qp->comp.state == QP_STATE_ERROR);
		goto exit;
	}

	if (qp->comp.timeout) {
		qp->comp.timeout_retry = 1;
		qp->comp.timeout = 0;
	} else {
		qp->comp.timeout_retry = 0;
	}

	if (qp->req.need_retry)
		goto exit;

	state = COMPST_GET_ACK;

	while (1) {
		pr_debug("qp#%d state = %s\n", qp_num(qp),
			 comp_state_name[state]);
		switch (state) {
		case COMPST_GET_ACK:
			skb = skb_dequeue(&qp->resp_pkts);
			if (skb) {
				pkt = SKB_TO_PKT(skb);
				qp->comp.timeout_retry = 0;
			}
			state = COMPST_GET_WQE;
			break;

		case COMPST_GET_WQE:
			state = get_wqe(qp, pkt, &wqe);
			break;

		case COMPST_CHECK_PSN:
			state = check_psn(qp, pkt, wqe);
			break;

		case COMPST_CHECK_ACK:
			state = check_ack(qp, pkt, wqe);
			break;

		case COMPST_READ:
			state = do_read(qp, pkt, wqe);
			break;

		case COMPST_ATOMIC:
			state = do_atomic(qp, pkt, wqe);
			break;

		case COMPST_WRITE_SEND:
			if (wqe->state == wqe_state_pending &&
			    wqe->last_psn == pkt->psn)
				state = COMPST_COMP_ACK;
			else
				state = COMPST_UPDATE_COMP;
			break;

		case COMPST_COMP_ACK:
			state = complete_ack(qp, pkt, wqe);
			break;

		case COMPST_COMP_WQE:
			state = complete_wqe(qp, pkt, wqe);
			break;

		case COMPST_UPDATE_COMP:
			if (pkt->mask & RXE_END_MASK)
				qp->comp.opcode = -1;
			else
				qp->comp.opcode = pkt->opcode;

			if (psn_compare(pkt->psn, qp->comp.psn) >= 0)
				qp->comp.psn = (pkt->psn + 1) & BTH_PSN_MASK;

			if (qp->req.wait_psn) {
				qp->req.wait_psn = 0;
				rxe_sched_task(&qp->req.task);
			}

			state = COMPST_DONE;
			break;

		case COMPST_DONE:
			goto done;

		case COMPST_EXIT:
			if (qp->comp.timeout_retry && wqe) {
				state = COMPST_ERROR_RETRY;
				break;
			}

			/* re reset the timeout counter if
			 * (1) QP is type RC
			 * (2) the QP is alive
			 * (3) there is a packet sent by the requester that
			 *     might be acked (we still might get spurious
			 *     timeouts but try to keep them as few as possible)
			 * (4) the timeout parameter is set
			 */
			if ((qp_type(qp) == IB_QPT_RC) &&
			    (qp->req.state == QP_STATE_READY) &&
			    (psn_compare(qp->req.psn, qp->comp.psn) > 0) &&
			    qp->qp_timeout_jiffies)
				mod_timer(&qp->retrans_timer,
					  jiffies + qp->qp_timeout_jiffies);
			goto exit;

		case COMPST_ERROR_RETRY:
			/* we come here if the retry timer fired and we did
			 * not receive a response packet. try to retry the send
			 * queue if that makes sense and the limits have not
			 * been exceeded. remember that some timeouts are
			 * spurious since we do not reset the timer but kick
			 * it down the road or let it expire
			 */

			/* there is nothing to retry in this case */
			if (!wqe || (wqe->state == wqe_state_posted))
				goto exit;

			/* if we've started a retry, don't start another
			 * retry sequence, unless this is a timeout.
			 */
			if (qp->comp.started_retry &&
			    !qp->comp.timeout_retry)
				goto done;

			if (qp->comp.retry_cnt > 0) {
				if (qp->comp.retry_cnt != 7)
					qp->comp.retry_cnt--;

				/* no point in retrying if we have already
				 * seen the last ack that the requester could
				 * have caused
				 */
				if (psn_compare(qp->req.psn,
						qp->comp.psn) > 0) {
					/* tell the requester to retry the
					 * send queue next time around
					 */
					rxe_counter_inc(rxe,
							RXE_CNT_COMP_RETRY);
					qp->req.need_retry = 1;
					qp->comp.started_retry = 1;
					rxe_run_task(&qp->req.task);
				}
				goto done;

			} else {
				rxe_counter_inc(rxe, RXE_CNT_RETRY_EXCEEDED);
				wqe->status = IB_WC_RETRY_EXC_ERR;
				state = COMPST_ERROR;
			}
			break;

		case COMPST_RNR_RETRY:
			/* we come here if we received an RNR NAK */
			if (qp->comp.rnr_retry > 0) {
				if (qp->comp.rnr_retry != 7)
					qp->comp.rnr_retry--;

				/* don't start a retry flow until the
				 * rnr timer has fired
				 */
				qp->req.wait_for_rnr_timer = 1;
				pr_debug("qp#%d set rnr nak timer\n",
					 qp_num(qp));
				mod_timer(&qp->rnr_nak_timer,
					  jiffies + rnrnak_jiffies(aeth_syn(pkt)
						& ~AETH_TYPE_MASK));
				goto exit;
			} else {
				rxe_counter_inc(rxe,
						RXE_CNT_RNR_RETRY_EXCEEDED);
				wqe->status = IB_WC_RNR_RETRY_EXC_ERR;
				state = COMPST_ERROR;
			}
			break;

		case COMPST_ERROR:
			WARN_ON_ONCE(wqe->status == IB_WC_SUCCESS);
			do_complete(qp, wqe);
			rxe_qp_error(qp);
			goto exit;
		}
	}

	/* A non-zero return value will cause rxe_do_task to
	 * exit its loop and end the tasklet. A zero return
	 * will continue looping and return to rxe_completer
	 */
done:
	ret = 0;
	goto out;
exit:
	ret = -EAGAIN;
out:
	if (pkt)
		free_pkt(pkt);
	rxe_put(qp);

	return ret;
}
