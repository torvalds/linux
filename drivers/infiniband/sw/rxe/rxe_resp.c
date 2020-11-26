// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/skbuff.h>

#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"

enum resp_states {
	RESPST_NONE,
	RESPST_GET_REQ,
	RESPST_CHK_PSN,
	RESPST_CHK_OP_SEQ,
	RESPST_CHK_OP_VALID,
	RESPST_CHK_RESOURCE,
	RESPST_CHK_LENGTH,
	RESPST_CHK_RKEY,
	RESPST_EXECUTE,
	RESPST_READ_REPLY,
	RESPST_COMPLETE,
	RESPST_ACKNOWLEDGE,
	RESPST_CLEANUP,
	RESPST_DUPLICATE_REQUEST,
	RESPST_ERR_MALFORMED_WQE,
	RESPST_ERR_UNSUPPORTED_OPCODE,
	RESPST_ERR_MISALIGNED_ATOMIC,
	RESPST_ERR_PSN_OUT_OF_SEQ,
	RESPST_ERR_MISSING_OPCODE_FIRST,
	RESPST_ERR_MISSING_OPCODE_LAST_C,
	RESPST_ERR_MISSING_OPCODE_LAST_D1E,
	RESPST_ERR_TOO_MANY_RDMA_ATM_REQ,
	RESPST_ERR_RNR,
	RESPST_ERR_RKEY_VIOLATION,
	RESPST_ERR_LENGTH,
	RESPST_ERR_CQ_OVERFLOW,
	RESPST_ERROR,
	RESPST_RESET,
	RESPST_DONE,
	RESPST_EXIT,
};

static char *resp_state_name[] = {
	[RESPST_NONE]				= "NONE",
	[RESPST_GET_REQ]			= "GET_REQ",
	[RESPST_CHK_PSN]			= "CHK_PSN",
	[RESPST_CHK_OP_SEQ]			= "CHK_OP_SEQ",
	[RESPST_CHK_OP_VALID]			= "CHK_OP_VALID",
	[RESPST_CHK_RESOURCE]			= "CHK_RESOURCE",
	[RESPST_CHK_LENGTH]			= "CHK_LENGTH",
	[RESPST_CHK_RKEY]			= "CHK_RKEY",
	[RESPST_EXECUTE]			= "EXECUTE",
	[RESPST_READ_REPLY]			= "READ_REPLY",
	[RESPST_COMPLETE]			= "COMPLETE",
	[RESPST_ACKNOWLEDGE]			= "ACKNOWLEDGE",
	[RESPST_CLEANUP]			= "CLEANUP",
	[RESPST_DUPLICATE_REQUEST]		= "DUPLICATE_REQUEST",
	[RESPST_ERR_MALFORMED_WQE]		= "ERR_MALFORMED_WQE",
	[RESPST_ERR_UNSUPPORTED_OPCODE]		= "ERR_UNSUPPORTED_OPCODE",
	[RESPST_ERR_MISALIGNED_ATOMIC]		= "ERR_MISALIGNED_ATOMIC",
	[RESPST_ERR_PSN_OUT_OF_SEQ]		= "ERR_PSN_OUT_OF_SEQ",
	[RESPST_ERR_MISSING_OPCODE_FIRST]	= "ERR_MISSING_OPCODE_FIRST",
	[RESPST_ERR_MISSING_OPCODE_LAST_C]	= "ERR_MISSING_OPCODE_LAST_C",
	[RESPST_ERR_MISSING_OPCODE_LAST_D1E]	= "ERR_MISSING_OPCODE_LAST_D1E",
	[RESPST_ERR_TOO_MANY_RDMA_ATM_REQ]	= "ERR_TOO_MANY_RDMA_ATM_REQ",
	[RESPST_ERR_RNR]			= "ERR_RNR",
	[RESPST_ERR_RKEY_VIOLATION]		= "ERR_RKEY_VIOLATION",
	[RESPST_ERR_LENGTH]			= "ERR_LENGTH",
	[RESPST_ERR_CQ_OVERFLOW]		= "ERR_CQ_OVERFLOW",
	[RESPST_ERROR]				= "ERROR",
	[RESPST_RESET]				= "RESET",
	[RESPST_DONE]				= "DONE",
	[RESPST_EXIT]				= "EXIT",
};

/* rxe_recv calls here to add a request packet to the input queue */
void rxe_resp_queue_pkt(struct rxe_qp *qp, struct sk_buff *skb)
{
	int must_sched;
	struct rxe_pkt_info *pkt = SKB_TO_PKT(skb);

	skb_queue_tail(&qp->req_pkts, skb);

	must_sched = (pkt->opcode == IB_OPCODE_RC_RDMA_READ_REQUEST) ||
			(skb_queue_len(&qp->req_pkts) > 1);

	rxe_run_task(&qp->resp.task, must_sched);
}

static inline enum resp_states get_req(struct rxe_qp *qp,
				       struct rxe_pkt_info **pkt_p)
{
	struct sk_buff *skb;

	if (qp->resp.state == QP_STATE_ERROR) {
		while ((skb = skb_dequeue(&qp->req_pkts))) {
			rxe_drop_ref(qp);
			kfree_skb(skb);
		}

		/* go drain recv wr queue */
		return RESPST_CHK_RESOURCE;
	}

	skb = skb_peek(&qp->req_pkts);
	if (!skb)
		return RESPST_EXIT;

	*pkt_p = SKB_TO_PKT(skb);

	return (qp->resp.res) ? RESPST_READ_REPLY : RESPST_CHK_PSN;
}

static enum resp_states check_psn(struct rxe_qp *qp,
				  struct rxe_pkt_info *pkt)
{
	int diff = psn_compare(pkt->psn, qp->resp.psn);
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);

	switch (qp_type(qp)) {
	case IB_QPT_RC:
		if (diff > 0) {
			if (qp->resp.sent_psn_nak)
				return RESPST_CLEANUP;

			qp->resp.sent_psn_nak = 1;
			rxe_counter_inc(rxe, RXE_CNT_OUT_OF_SEQ_REQ);
			return RESPST_ERR_PSN_OUT_OF_SEQ;

		} else if (diff < 0) {
			rxe_counter_inc(rxe, RXE_CNT_DUP_REQ);
			return RESPST_DUPLICATE_REQUEST;
		}

		if (qp->resp.sent_psn_nak)
			qp->resp.sent_psn_nak = 0;

		break;

	case IB_QPT_UC:
		if (qp->resp.drop_msg || diff != 0) {
			if (pkt->mask & RXE_START_MASK) {
				qp->resp.drop_msg = 0;
				return RESPST_CHK_OP_SEQ;
			}

			qp->resp.drop_msg = 1;
			return RESPST_CLEANUP;
		}
		break;
	default:
		break;
	}

	return RESPST_CHK_OP_SEQ;
}

static enum resp_states check_op_seq(struct rxe_qp *qp,
				     struct rxe_pkt_info *pkt)
{
	switch (qp_type(qp)) {
	case IB_QPT_RC:
		switch (qp->resp.opcode) {
		case IB_OPCODE_RC_SEND_FIRST:
		case IB_OPCODE_RC_SEND_MIDDLE:
			switch (pkt->opcode) {
			case IB_OPCODE_RC_SEND_MIDDLE:
			case IB_OPCODE_RC_SEND_LAST:
			case IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE:
			case IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE:
				return RESPST_CHK_OP_VALID;
			default:
				return RESPST_ERR_MISSING_OPCODE_LAST_C;
			}

		case IB_OPCODE_RC_RDMA_WRITE_FIRST:
		case IB_OPCODE_RC_RDMA_WRITE_MIDDLE:
			switch (pkt->opcode) {
			case IB_OPCODE_RC_RDMA_WRITE_MIDDLE:
			case IB_OPCODE_RC_RDMA_WRITE_LAST:
			case IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE:
				return RESPST_CHK_OP_VALID;
			default:
				return RESPST_ERR_MISSING_OPCODE_LAST_C;
			}

		default:
			switch (pkt->opcode) {
			case IB_OPCODE_RC_SEND_MIDDLE:
			case IB_OPCODE_RC_SEND_LAST:
			case IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE:
			case IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE:
			case IB_OPCODE_RC_RDMA_WRITE_MIDDLE:
			case IB_OPCODE_RC_RDMA_WRITE_LAST:
			case IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE:
				return RESPST_ERR_MISSING_OPCODE_FIRST;
			default:
				return RESPST_CHK_OP_VALID;
			}
		}
		break;

	case IB_QPT_UC:
		switch (qp->resp.opcode) {
		case IB_OPCODE_UC_SEND_FIRST:
		case IB_OPCODE_UC_SEND_MIDDLE:
			switch (pkt->opcode) {
			case IB_OPCODE_UC_SEND_MIDDLE:
			case IB_OPCODE_UC_SEND_LAST:
			case IB_OPCODE_UC_SEND_LAST_WITH_IMMEDIATE:
				return RESPST_CHK_OP_VALID;
			default:
				return RESPST_ERR_MISSING_OPCODE_LAST_D1E;
			}

		case IB_OPCODE_UC_RDMA_WRITE_FIRST:
		case IB_OPCODE_UC_RDMA_WRITE_MIDDLE:
			switch (pkt->opcode) {
			case IB_OPCODE_UC_RDMA_WRITE_MIDDLE:
			case IB_OPCODE_UC_RDMA_WRITE_LAST:
			case IB_OPCODE_UC_RDMA_WRITE_LAST_WITH_IMMEDIATE:
				return RESPST_CHK_OP_VALID;
			default:
				return RESPST_ERR_MISSING_OPCODE_LAST_D1E;
			}

		default:
			switch (pkt->opcode) {
			case IB_OPCODE_UC_SEND_MIDDLE:
			case IB_OPCODE_UC_SEND_LAST:
			case IB_OPCODE_UC_SEND_LAST_WITH_IMMEDIATE:
			case IB_OPCODE_UC_RDMA_WRITE_MIDDLE:
			case IB_OPCODE_UC_RDMA_WRITE_LAST:
			case IB_OPCODE_UC_RDMA_WRITE_LAST_WITH_IMMEDIATE:
				qp->resp.drop_msg = 1;
				return RESPST_CLEANUP;
			default:
				return RESPST_CHK_OP_VALID;
			}
		}
		break;

	default:
		return RESPST_CHK_OP_VALID;
	}
}

static enum resp_states check_op_valid(struct rxe_qp *qp,
				       struct rxe_pkt_info *pkt)
{
	switch (qp_type(qp)) {
	case IB_QPT_RC:
		if (((pkt->mask & RXE_READ_MASK) &&
		     !(qp->attr.qp_access_flags & IB_ACCESS_REMOTE_READ)) ||
		    ((pkt->mask & RXE_WRITE_MASK) &&
		     !(qp->attr.qp_access_flags & IB_ACCESS_REMOTE_WRITE)) ||
		    ((pkt->mask & RXE_ATOMIC_MASK) &&
		     !(qp->attr.qp_access_flags & IB_ACCESS_REMOTE_ATOMIC))) {
			return RESPST_ERR_UNSUPPORTED_OPCODE;
		}

		break;

	case IB_QPT_UC:
		if ((pkt->mask & RXE_WRITE_MASK) &&
		    !(qp->attr.qp_access_flags & IB_ACCESS_REMOTE_WRITE)) {
			qp->resp.drop_msg = 1;
			return RESPST_CLEANUP;
		}

		break;

	case IB_QPT_UD:
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		break;

	default:
		WARN_ON_ONCE(1);
		break;
	}

	return RESPST_CHK_RESOURCE;
}

static enum resp_states get_srq_wqe(struct rxe_qp *qp)
{
	struct rxe_srq *srq = qp->srq;
	struct rxe_queue *q = srq->rq.queue;
	struct rxe_recv_wqe *wqe;
	struct ib_event ev;

	if (srq->error)
		return RESPST_ERR_RNR;

	spin_lock_bh(&srq->rq.consumer_lock);

	wqe = queue_head(q);
	if (!wqe) {
		spin_unlock_bh(&srq->rq.consumer_lock);
		return RESPST_ERR_RNR;
	}

	/* note kernel and user space recv wqes have same size */
	memcpy(&qp->resp.srq_wqe, wqe, sizeof(qp->resp.srq_wqe));

	qp->resp.wqe = &qp->resp.srq_wqe.wqe;
	advance_consumer(q);

	if (srq->limit && srq->ibsrq.event_handler &&
	    (queue_count(q) < srq->limit)) {
		srq->limit = 0;
		goto event;
	}

	spin_unlock_bh(&srq->rq.consumer_lock);
	return RESPST_CHK_LENGTH;

event:
	spin_unlock_bh(&srq->rq.consumer_lock);
	ev.device = qp->ibqp.device;
	ev.element.srq = qp->ibqp.srq;
	ev.event = IB_EVENT_SRQ_LIMIT_REACHED;
	srq->ibsrq.event_handler(&ev, srq->ibsrq.srq_context);
	return RESPST_CHK_LENGTH;
}

static enum resp_states check_resource(struct rxe_qp *qp,
				       struct rxe_pkt_info *pkt)
{
	struct rxe_srq *srq = qp->srq;

	if (qp->resp.state == QP_STATE_ERROR) {
		if (qp->resp.wqe) {
			qp->resp.status = IB_WC_WR_FLUSH_ERR;
			return RESPST_COMPLETE;
		} else if (!srq) {
			qp->resp.wqe = queue_head(qp->rq.queue);
			if (qp->resp.wqe) {
				qp->resp.status = IB_WC_WR_FLUSH_ERR;
				return RESPST_COMPLETE;
			} else {
				return RESPST_EXIT;
			}
		} else {
			return RESPST_EXIT;
		}
	}

	if (pkt->mask & RXE_READ_OR_ATOMIC) {
		/* it is the requesters job to not send
		 * too many read/atomic ops, we just
		 * recycle the responder resource queue
		 */
		if (likely(qp->attr.max_dest_rd_atomic > 0))
			return RESPST_CHK_LENGTH;
		else
			return RESPST_ERR_TOO_MANY_RDMA_ATM_REQ;
	}

	if (pkt->mask & RXE_RWR_MASK) {
		if (srq)
			return get_srq_wqe(qp);

		qp->resp.wqe = queue_head(qp->rq.queue);
		return (qp->resp.wqe) ? RESPST_CHK_LENGTH : RESPST_ERR_RNR;
	}

	return RESPST_CHK_LENGTH;
}

static enum resp_states check_length(struct rxe_qp *qp,
				     struct rxe_pkt_info *pkt)
{
	switch (qp_type(qp)) {
	case IB_QPT_RC:
		return RESPST_CHK_RKEY;

	case IB_QPT_UC:
		return RESPST_CHK_RKEY;

	default:
		return RESPST_CHK_RKEY;
	}
}

static enum resp_states check_rkey(struct rxe_qp *qp,
				   struct rxe_pkt_info *pkt)
{
	struct rxe_mem *mem = NULL;
	u64 va;
	u32 rkey;
	u32 resid;
	u32 pktlen;
	int mtu = qp->mtu;
	enum resp_states state;
	int access;

	if (pkt->mask & (RXE_READ_MASK | RXE_WRITE_MASK)) {
		if (pkt->mask & RXE_RETH_MASK) {
			qp->resp.va = reth_va(pkt);
			qp->resp.rkey = reth_rkey(pkt);
			qp->resp.resid = reth_len(pkt);
			qp->resp.length = reth_len(pkt);
		}
		access = (pkt->mask & RXE_READ_MASK) ? IB_ACCESS_REMOTE_READ
						     : IB_ACCESS_REMOTE_WRITE;
	} else if (pkt->mask & RXE_ATOMIC_MASK) {
		qp->resp.va = atmeth_va(pkt);
		qp->resp.rkey = atmeth_rkey(pkt);
		qp->resp.resid = sizeof(u64);
		access = IB_ACCESS_REMOTE_ATOMIC;
	} else {
		return RESPST_EXECUTE;
	}

	/* A zero-byte op is not required to set an addr or rkey. */
	if ((pkt->mask & (RXE_READ_MASK | RXE_WRITE_OR_SEND)) &&
	    (pkt->mask & RXE_RETH_MASK) &&
	    reth_len(pkt) == 0) {
		return RESPST_EXECUTE;
	}

	va	= qp->resp.va;
	rkey	= qp->resp.rkey;
	resid	= qp->resp.resid;
	pktlen	= payload_size(pkt);

	mem = lookup_mem(qp->pd, access, rkey, lookup_remote);
	if (!mem) {
		state = RESPST_ERR_RKEY_VIOLATION;
		goto err;
	}

	if (unlikely(mem->state == RXE_MEM_STATE_FREE)) {
		state = RESPST_ERR_RKEY_VIOLATION;
		goto err;
	}

	if (mem_check_range(mem, va, resid)) {
		state = RESPST_ERR_RKEY_VIOLATION;
		goto err;
	}

	if (pkt->mask & RXE_WRITE_MASK)	 {
		if (resid > mtu) {
			if (pktlen != mtu || bth_pad(pkt)) {
				state = RESPST_ERR_LENGTH;
				goto err;
			}
		} else {
			if (pktlen != resid) {
				state = RESPST_ERR_LENGTH;
				goto err;
			}
			if ((bth_pad(pkt) != (0x3 & (-resid)))) {
				/* This case may not be exactly that
				 * but nothing else fits.
				 */
				state = RESPST_ERR_LENGTH;
				goto err;
			}
		}
	}

	WARN_ON_ONCE(qp->resp.mr);

	qp->resp.mr = mem;
	return RESPST_EXECUTE;

err:
	if (mem)
		rxe_drop_ref(mem);
	return state;
}

static enum resp_states send_data_in(struct rxe_qp *qp, void *data_addr,
				     int data_len)
{
	int err;

	err = copy_data(qp->pd, IB_ACCESS_LOCAL_WRITE, &qp->resp.wqe->dma,
			data_addr, data_len, to_mem_obj, NULL);
	if (unlikely(err))
		return (err == -ENOSPC) ? RESPST_ERR_LENGTH
					: RESPST_ERR_MALFORMED_WQE;

	return RESPST_NONE;
}

static enum resp_states write_data_in(struct rxe_qp *qp,
				      struct rxe_pkt_info *pkt)
{
	enum resp_states rc = RESPST_NONE;
	int	err;
	int data_len = payload_size(pkt);

	err = rxe_mem_copy(qp->resp.mr, qp->resp.va, payload_addr(pkt),
			   data_len, to_mem_obj, NULL);
	if (err) {
		rc = RESPST_ERR_RKEY_VIOLATION;
		goto out;
	}

	qp->resp.va += data_len;
	qp->resp.resid -= data_len;

out:
	return rc;
}

/* Guarantee atomicity of atomic operations at the machine level. */
static DEFINE_SPINLOCK(atomic_ops_lock);

static enum resp_states process_atomic(struct rxe_qp *qp,
				       struct rxe_pkt_info *pkt)
{
	u64 iova = atmeth_va(pkt);
	u64 *vaddr;
	enum resp_states ret;
	struct rxe_mem *mr = qp->resp.mr;

	if (mr->state != RXE_MEM_STATE_VALID) {
		ret = RESPST_ERR_RKEY_VIOLATION;
		goto out;
	}

	vaddr = iova_to_vaddr(mr, iova, sizeof(u64));

	/* check vaddr is 8 bytes aligned. */
	if (!vaddr || (uintptr_t)vaddr & 7) {
		ret = RESPST_ERR_MISALIGNED_ATOMIC;
		goto out;
	}

	spin_lock_bh(&atomic_ops_lock);

	qp->resp.atomic_orig = *vaddr;

	if (pkt->opcode == IB_OPCODE_RC_COMPARE_SWAP ||
	    pkt->opcode == IB_OPCODE_RD_COMPARE_SWAP) {
		if (*vaddr == atmeth_comp(pkt))
			*vaddr = atmeth_swap_add(pkt);
	} else {
		*vaddr += atmeth_swap_add(pkt);
	}

	spin_unlock_bh(&atomic_ops_lock);

	ret = RESPST_NONE;
out:
	return ret;
}

static struct sk_buff *prepare_ack_packet(struct rxe_qp *qp,
					  struct rxe_pkt_info *pkt,
					  struct rxe_pkt_info *ack,
					  int opcode,
					  int payload,
					  u32 psn,
					  u8 syndrome,
					  u32 *crcp)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct sk_buff *skb;
	u32 crc = 0;
	u32 *p;
	int paylen;
	int pad;
	int err;

	/*
	 * allocate packet
	 */
	pad = (-payload) & 0x3;
	paylen = rxe_opcode[opcode].length + payload + pad + RXE_ICRC_SIZE;

	skb = rxe_init_packet(rxe, &qp->pri_av, paylen, ack);
	if (!skb)
		return NULL;

	ack->qp = qp;
	ack->opcode = opcode;
	ack->mask = rxe_opcode[opcode].mask;
	ack->offset = pkt->offset;
	ack->paylen = paylen;

	/* fill in bth using the request packet headers */
	memcpy(ack->hdr, pkt->hdr, pkt->offset + RXE_BTH_BYTES);

	bth_set_opcode(ack, opcode);
	bth_set_qpn(ack, qp->attr.dest_qp_num);
	bth_set_pad(ack, pad);
	bth_set_se(ack, 0);
	bth_set_psn(ack, psn);
	bth_set_ack(ack, 0);
	ack->psn = psn;

	if (ack->mask & RXE_AETH_MASK) {
		aeth_set_syn(ack, syndrome);
		aeth_set_msn(ack, qp->resp.msn);
	}

	if (ack->mask & RXE_ATMACK_MASK)
		atmack_set_orig(ack, qp->resp.atomic_orig);

	err = rxe_prepare(ack, skb, &crc);
	if (err) {
		kfree_skb(skb);
		return NULL;
	}

	if (crcp) {
		/* CRC computation will be continued by the caller */
		*crcp = crc;
	} else {
		p = payload_addr(ack) + payload + bth_pad(ack);
		*p = ~crc;
	}

	return skb;
}

/* RDMA read response. If res is not NULL, then we have a current RDMA request
 * being processed or replayed.
 */
static enum resp_states read_reply(struct rxe_qp *qp,
				   struct rxe_pkt_info *req_pkt)
{
	struct rxe_pkt_info ack_pkt;
	struct sk_buff *skb;
	int mtu = qp->mtu;
	enum resp_states state;
	int payload;
	int opcode;
	int err;
	struct resp_res *res = qp->resp.res;
	u32 icrc;
	u32 *p;

	if (!res) {
		/* This is the first time we process that request. Get a
		 * resource
		 */
		res = &qp->resp.resources[qp->resp.res_head];

		free_rd_atomic_resource(qp, res);
		rxe_advance_resp_resource(qp);

		res->type		= RXE_READ_MASK;
		res->replay		= 0;

		res->read.va		= qp->resp.va;
		res->read.va_org	= qp->resp.va;

		res->first_psn		= req_pkt->psn;

		if (reth_len(req_pkt)) {
			res->last_psn	= (req_pkt->psn +
					   (reth_len(req_pkt) + mtu - 1) /
					   mtu - 1) & BTH_PSN_MASK;
		} else {
			res->last_psn	= res->first_psn;
		}
		res->cur_psn		= req_pkt->psn;

		res->read.resid		= qp->resp.resid;
		res->read.length	= qp->resp.resid;
		res->read.rkey		= qp->resp.rkey;

		/* note res inherits the reference to mr from qp */
		res->read.mr		= qp->resp.mr;
		qp->resp.mr		= NULL;

		qp->resp.res		= res;
		res->state		= rdatm_res_state_new;
	}

	if (res->state == rdatm_res_state_new) {
		if (res->read.resid <= mtu)
			opcode = IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY;
		else
			opcode = IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST;
	} else {
		if (res->read.resid > mtu)
			opcode = IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE;
		else
			opcode = IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST;
	}

	res->state = rdatm_res_state_next;

	payload = min_t(int, res->read.resid, mtu);

	skb = prepare_ack_packet(qp, req_pkt, &ack_pkt, opcode, payload,
				 res->cur_psn, AETH_ACK_UNLIMITED, &icrc);
	if (!skb)
		return RESPST_ERR_RNR;

	err = rxe_mem_copy(res->read.mr, res->read.va, payload_addr(&ack_pkt),
			   payload, from_mem_obj, &icrc);
	if (err)
		pr_err("Failed copying memory\n");

	if (bth_pad(&ack_pkt)) {
		struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
		u8 *pad = payload_addr(&ack_pkt) + payload;

		memset(pad, 0, bth_pad(&ack_pkt));
		icrc = rxe_crc32(rxe, icrc, pad, bth_pad(&ack_pkt));
	}
	p = payload_addr(&ack_pkt) + payload + bth_pad(&ack_pkt);
	*p = ~icrc;

	err = rxe_xmit_packet(qp, &ack_pkt, skb);
	if (err) {
		pr_err("Failed sending RDMA reply.\n");
		return RESPST_ERR_RNR;
	}

	res->read.va += payload;
	res->read.resid -= payload;
	res->cur_psn = (res->cur_psn + 1) & BTH_PSN_MASK;

	if (res->read.resid > 0) {
		state = RESPST_DONE;
	} else {
		qp->resp.res = NULL;
		if (!res->replay)
			qp->resp.opcode = -1;
		if (psn_compare(res->cur_psn, qp->resp.psn) >= 0)
			qp->resp.psn = res->cur_psn;
		state = RESPST_CLEANUP;
	}

	return state;
}

static void build_rdma_network_hdr(union rdma_network_hdr *hdr,
				   struct rxe_pkt_info *pkt)
{
	struct sk_buff *skb = PKT_TO_SKB(pkt);

	memset(hdr, 0, sizeof(*hdr));
	if (skb->protocol == htons(ETH_P_IP))
		memcpy(&hdr->roce4grh, ip_hdr(skb), sizeof(hdr->roce4grh));
	else if (skb->protocol == htons(ETH_P_IPV6))
		memcpy(&hdr->ibgrh, ipv6_hdr(skb), sizeof(hdr->ibgrh));
}

/* Executes a new request. A retried request never reach that function (send
 * and writes are discarded, and reads and atomics are retried elsewhere.
 */
static enum resp_states execute(struct rxe_qp *qp, struct rxe_pkt_info *pkt)
{
	enum resp_states err;

	if (pkt->mask & RXE_SEND_MASK) {
		if (qp_type(qp) == IB_QPT_UD ||
		    qp_type(qp) == IB_QPT_SMI ||
		    qp_type(qp) == IB_QPT_GSI) {
			union rdma_network_hdr hdr;

			build_rdma_network_hdr(&hdr, pkt);

			err = send_data_in(qp, &hdr, sizeof(hdr));
			if (err)
				return err;
		}
		err = send_data_in(qp, payload_addr(pkt), payload_size(pkt));
		if (err)
			return err;
	} else if (pkt->mask & RXE_WRITE_MASK) {
		err = write_data_in(qp, pkt);
		if (err)
			return err;
	} else if (pkt->mask & RXE_READ_MASK) {
		/* For RDMA Read we can increment the msn now. See C9-148. */
		qp->resp.msn++;
		return RESPST_READ_REPLY;
	} else if (pkt->mask & RXE_ATOMIC_MASK) {
		err = process_atomic(qp, pkt);
		if (err)
			return err;
	} else {
		/* Unreachable */
		WARN_ON_ONCE(1);
	}

	/* next expected psn, read handles this separately */
	qp->resp.psn = (pkt->psn + 1) & BTH_PSN_MASK;
	qp->resp.ack_psn = qp->resp.psn;

	qp->resp.opcode = pkt->opcode;
	qp->resp.status = IB_WC_SUCCESS;

	if (pkt->mask & RXE_COMP_MASK) {
		/* We successfully processed this new request. */
		qp->resp.msn++;
		return RESPST_COMPLETE;
	} else if (qp_type(qp) == IB_QPT_RC)
		return RESPST_ACKNOWLEDGE;
	else
		return RESPST_CLEANUP;
}

static enum resp_states do_complete(struct rxe_qp *qp,
				    struct rxe_pkt_info *pkt)
{
	struct rxe_cqe cqe;
	struct ib_wc *wc = &cqe.ibwc;
	struct ib_uverbs_wc *uwc = &cqe.uibwc;
	struct rxe_recv_wqe *wqe = qp->resp.wqe;
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);

	if (unlikely(!wqe))
		return RESPST_CLEANUP;

	memset(&cqe, 0, sizeof(cqe));

	if (qp->rcq->is_user) {
		uwc->status             = qp->resp.status;
		uwc->qp_num             = qp->ibqp.qp_num;
		uwc->wr_id              = wqe->wr_id;
	} else {
		wc->status              = qp->resp.status;
		wc->qp                  = &qp->ibqp;
		wc->wr_id               = wqe->wr_id;
	}

	if (wc->status == IB_WC_SUCCESS) {
		rxe_counter_inc(rxe, RXE_CNT_RDMA_RECV);
		wc->opcode = (pkt->mask & RXE_IMMDT_MASK &&
				pkt->mask & RXE_WRITE_MASK) ?
					IB_WC_RECV_RDMA_WITH_IMM : IB_WC_RECV;
		wc->vendor_err = 0;
		wc->byte_len = (pkt->mask & RXE_IMMDT_MASK &&
				pkt->mask & RXE_WRITE_MASK) ?
					qp->resp.length : wqe->dma.length - wqe->dma.resid;

		/* fields after byte_len are different between kernel and user
		 * space
		 */
		if (qp->rcq->is_user) {
			uwc->wc_flags = IB_WC_GRH;

			if (pkt->mask & RXE_IMMDT_MASK) {
				uwc->wc_flags |= IB_WC_WITH_IMM;
				uwc->ex.imm_data = immdt_imm(pkt);
			}

			if (pkt->mask & RXE_IETH_MASK) {
				uwc->wc_flags |= IB_WC_WITH_INVALIDATE;
				uwc->ex.invalidate_rkey = ieth_rkey(pkt);
			}

			uwc->qp_num		= qp->ibqp.qp_num;

			if (pkt->mask & RXE_DETH_MASK)
				uwc->src_qp = deth_sqp(pkt);

			uwc->port_num		= qp->attr.port_num;
		} else {
			struct sk_buff *skb = PKT_TO_SKB(pkt);

			wc->wc_flags = IB_WC_GRH | IB_WC_WITH_NETWORK_HDR_TYPE;
			if (skb->protocol == htons(ETH_P_IP))
				wc->network_hdr_type = RDMA_NETWORK_IPV4;
			else
				wc->network_hdr_type = RDMA_NETWORK_IPV6;

			if (is_vlan_dev(skb->dev)) {
				wc->wc_flags |= IB_WC_WITH_VLAN;
				wc->vlan_id = vlan_dev_vlan_id(skb->dev);
			}

			if (pkt->mask & RXE_IMMDT_MASK) {
				wc->wc_flags |= IB_WC_WITH_IMM;
				wc->ex.imm_data = immdt_imm(pkt);
			}

			if (pkt->mask & RXE_IETH_MASK) {
				struct rxe_mem *rmr;

				wc->wc_flags |= IB_WC_WITH_INVALIDATE;
				wc->ex.invalidate_rkey = ieth_rkey(pkt);

				rmr = rxe_pool_get_index(&rxe->mr_pool,
							 wc->ex.invalidate_rkey >> 8);
				if (unlikely(!rmr)) {
					pr_err("Bad rkey %#x invalidation\n",
					       wc->ex.invalidate_rkey);
					return RESPST_ERROR;
				}
				rmr->state = RXE_MEM_STATE_FREE;
				rxe_drop_ref(rmr);
			}

			wc->qp			= &qp->ibqp;

			if (pkt->mask & RXE_DETH_MASK)
				wc->src_qp = deth_sqp(pkt);

			wc->port_num		= qp->attr.port_num;
		}
	}

	/* have copy for srq and reference for !srq */
	if (!qp->srq)
		advance_consumer(qp->rq.queue);

	qp->resp.wqe = NULL;

	if (rxe_cq_post(qp->rcq, &cqe, pkt ? bth_se(pkt) : 1))
		return RESPST_ERR_CQ_OVERFLOW;

	if (qp->resp.state == QP_STATE_ERROR)
		return RESPST_CHK_RESOURCE;

	if (!pkt)
		return RESPST_DONE;
	else if (qp_type(qp) == IB_QPT_RC)
		return RESPST_ACKNOWLEDGE;
	else
		return RESPST_CLEANUP;
}

static int send_ack(struct rxe_qp *qp, struct rxe_pkt_info *pkt,
		    u8 syndrome, u32 psn)
{
	int err = 0;
	struct rxe_pkt_info ack_pkt;
	struct sk_buff *skb;

	skb = prepare_ack_packet(qp, pkt, &ack_pkt, IB_OPCODE_RC_ACKNOWLEDGE,
				 0, psn, syndrome, NULL);
	if (!skb) {
		err = -ENOMEM;
		goto err1;
	}

	err = rxe_xmit_packet(qp, &ack_pkt, skb);
	if (err)
		pr_err_ratelimited("Failed sending ack\n");

err1:
	return err;
}

static int send_atomic_ack(struct rxe_qp *qp, struct rxe_pkt_info *pkt,
			   u8 syndrome)
{
	int rc = 0;
	struct rxe_pkt_info ack_pkt;
	struct sk_buff *skb;
	struct resp_res *res;

	skb = prepare_ack_packet(qp, pkt, &ack_pkt,
				 IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE, 0, pkt->psn,
				 syndrome, NULL);
	if (!skb) {
		rc = -ENOMEM;
		goto out;
	}

	rxe_add_ref(qp);

	res = &qp->resp.resources[qp->resp.res_head];
	free_rd_atomic_resource(qp, res);
	rxe_advance_resp_resource(qp);

	memcpy(SKB_TO_PKT(skb), &ack_pkt, sizeof(ack_pkt));
	memset((unsigned char *)SKB_TO_PKT(skb) + sizeof(ack_pkt), 0,
	       sizeof(skb->cb) - sizeof(ack_pkt));

	skb_get(skb);
	res->type = RXE_ATOMIC_MASK;
	res->atomic.skb = skb;
	res->first_psn = ack_pkt.psn;
	res->last_psn  = ack_pkt.psn;
	res->cur_psn   = ack_pkt.psn;

	rc = rxe_xmit_packet(qp, &ack_pkt, skb);
	if (rc) {
		pr_err_ratelimited("Failed sending ack\n");
		rxe_drop_ref(qp);
	}
out:
	return rc;
}

static enum resp_states acknowledge(struct rxe_qp *qp,
				    struct rxe_pkt_info *pkt)
{
	if (qp_type(qp) != IB_QPT_RC)
		return RESPST_CLEANUP;

	if (qp->resp.aeth_syndrome != AETH_ACK_UNLIMITED)
		send_ack(qp, pkt, qp->resp.aeth_syndrome, pkt->psn);
	else if (pkt->mask & RXE_ATOMIC_MASK)
		send_atomic_ack(qp, pkt, AETH_ACK_UNLIMITED);
	else if (bth_ack(pkt))
		send_ack(qp, pkt, AETH_ACK_UNLIMITED, pkt->psn);

	return RESPST_CLEANUP;
}

static enum resp_states cleanup(struct rxe_qp *qp,
				struct rxe_pkt_info *pkt)
{
	struct sk_buff *skb;

	if (pkt) {
		skb = skb_dequeue(&qp->req_pkts);
		rxe_drop_ref(qp);
		kfree_skb(skb);
	}

	if (qp->resp.mr) {
		rxe_drop_ref(qp->resp.mr);
		qp->resp.mr = NULL;
	}

	return RESPST_DONE;
}

static struct resp_res *find_resource(struct rxe_qp *qp, u32 psn)
{
	int i;

	for (i = 0; i < qp->attr.max_dest_rd_atomic; i++) {
		struct resp_res *res = &qp->resp.resources[i];

		if (res->type == 0)
			continue;

		if (psn_compare(psn, res->first_psn) >= 0 &&
		    psn_compare(psn, res->last_psn) <= 0) {
			return res;
		}
	}

	return NULL;
}

static enum resp_states duplicate_request(struct rxe_qp *qp,
					  struct rxe_pkt_info *pkt)
{
	enum resp_states rc;
	u32 prev_psn = (qp->resp.ack_psn - 1) & BTH_PSN_MASK;

	if (pkt->mask & RXE_SEND_MASK ||
	    pkt->mask & RXE_WRITE_MASK) {
		/* SEND. Ack again and cleanup. C9-105. */
		if (bth_ack(pkt))
			send_ack(qp, pkt, AETH_ACK_UNLIMITED, prev_psn);
		rc = RESPST_CLEANUP;
		goto out;
	} else if (pkt->mask & RXE_READ_MASK) {
		struct resp_res *res;

		res = find_resource(qp, pkt->psn);
		if (!res) {
			/* Resource not found. Class D error.  Drop the
			 * request.
			 */
			rc = RESPST_CLEANUP;
			goto out;
		} else {
			/* Ensure this new request is the same as the previous
			 * one or a subset of it.
			 */
			u64 iova = reth_va(pkt);
			u32 resid = reth_len(pkt);

			if (iova < res->read.va_org ||
			    resid > res->read.length ||
			    (iova + resid) > (res->read.va_org +
					      res->read.length)) {
				rc = RESPST_CLEANUP;
				goto out;
			}

			if (reth_rkey(pkt) != res->read.rkey) {
				rc = RESPST_CLEANUP;
				goto out;
			}

			res->cur_psn = pkt->psn;
			res->state = (pkt->psn == res->first_psn) ?
					rdatm_res_state_new :
					rdatm_res_state_replay;
			res->replay = 1;

			/* Reset the resource, except length. */
			res->read.va_org = iova;
			res->read.va = iova;
			res->read.resid = resid;

			/* Replay the RDMA read reply. */
			qp->resp.res = res;
			rc = RESPST_READ_REPLY;
			goto out;
		}
	} else {
		struct resp_res *res;

		/* Find the operation in our list of responder resources. */
		res = find_resource(qp, pkt->psn);
		if (res) {
			skb_get(res->atomic.skb);
			/* Resend the result. */
			rc = rxe_xmit_packet(qp, pkt, res->atomic.skb);
			if (rc) {
				pr_err("Failed resending result. This flow is not handled - skb ignored\n");
				rc = RESPST_CLEANUP;
				goto out;
			}
		}

		/* Resource not found. Class D error. Drop the request. */
		rc = RESPST_CLEANUP;
		goto out;
	}
out:
	return rc;
}

/* Process a class A or C. Both are treated the same in this implementation. */
static void do_class_ac_error(struct rxe_qp *qp, u8 syndrome,
			      enum ib_wc_status status)
{
	qp->resp.aeth_syndrome	= syndrome;
	qp->resp.status		= status;

	/* indicate that we should go through the ERROR state */
	qp->resp.goto_error	= 1;
}

static enum resp_states do_class_d1e_error(struct rxe_qp *qp)
{
	/* UC */
	if (qp->srq) {
		/* Class E */
		qp->resp.drop_msg = 1;
		if (qp->resp.wqe) {
			qp->resp.status = IB_WC_REM_INV_REQ_ERR;
			return RESPST_COMPLETE;
		} else {
			return RESPST_CLEANUP;
		}
	} else {
		/* Class D1. This packet may be the start of a
		 * new message and could be valid. The previous
		 * message is invalid and ignored. reset the
		 * recv wr to its original state
		 */
		if (qp->resp.wqe) {
			qp->resp.wqe->dma.resid = qp->resp.wqe->dma.length;
			qp->resp.wqe->dma.cur_sge = 0;
			qp->resp.wqe->dma.sge_offset = 0;
			qp->resp.opcode = -1;
		}

		if (qp->resp.mr) {
			rxe_drop_ref(qp->resp.mr);
			qp->resp.mr = NULL;
		}

		return RESPST_CLEANUP;
	}
}

static void rxe_drain_req_pkts(struct rxe_qp *qp, bool notify)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&qp->req_pkts))) {
		rxe_drop_ref(qp);
		kfree_skb(skb);
	}

	if (notify)
		return;

	while (!qp->srq && qp->rq.queue && queue_head(qp->rq.queue))
		advance_consumer(qp->rq.queue);
}

int rxe_responder(void *arg)
{
	struct rxe_qp *qp = (struct rxe_qp *)arg;
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	enum resp_states state;
	struct rxe_pkt_info *pkt = NULL;
	int ret = 0;

	rxe_add_ref(qp);

	qp->resp.aeth_syndrome = AETH_ACK_UNLIMITED;

	if (!qp->valid) {
		ret = -EINVAL;
		goto done;
	}

	switch (qp->resp.state) {
	case QP_STATE_RESET:
		state = RESPST_RESET;
		break;

	default:
		state = RESPST_GET_REQ;
		break;
	}

	while (1) {
		pr_debug("qp#%d state = %s\n", qp_num(qp),
			 resp_state_name[state]);
		switch (state) {
		case RESPST_GET_REQ:
			state = get_req(qp, &pkt);
			break;
		case RESPST_CHK_PSN:
			state = check_psn(qp, pkt);
			break;
		case RESPST_CHK_OP_SEQ:
			state = check_op_seq(qp, pkt);
			break;
		case RESPST_CHK_OP_VALID:
			state = check_op_valid(qp, pkt);
			break;
		case RESPST_CHK_RESOURCE:
			state = check_resource(qp, pkt);
			break;
		case RESPST_CHK_LENGTH:
			state = check_length(qp, pkt);
			break;
		case RESPST_CHK_RKEY:
			state = check_rkey(qp, pkt);
			break;
		case RESPST_EXECUTE:
			state = execute(qp, pkt);
			break;
		case RESPST_COMPLETE:
			state = do_complete(qp, pkt);
			break;
		case RESPST_READ_REPLY:
			state = read_reply(qp, pkt);
			break;
		case RESPST_ACKNOWLEDGE:
			state = acknowledge(qp, pkt);
			break;
		case RESPST_CLEANUP:
			state = cleanup(qp, pkt);
			break;
		case RESPST_DUPLICATE_REQUEST:
			state = duplicate_request(qp, pkt);
			break;
		case RESPST_ERR_PSN_OUT_OF_SEQ:
			/* RC only - Class B. Drop packet. */
			send_ack(qp, pkt, AETH_NAK_PSN_SEQ_ERROR, qp->resp.psn);
			state = RESPST_CLEANUP;
			break;

		case RESPST_ERR_TOO_MANY_RDMA_ATM_REQ:
		case RESPST_ERR_MISSING_OPCODE_FIRST:
		case RESPST_ERR_MISSING_OPCODE_LAST_C:
		case RESPST_ERR_UNSUPPORTED_OPCODE:
		case RESPST_ERR_MISALIGNED_ATOMIC:
			/* RC Only - Class C. */
			do_class_ac_error(qp, AETH_NAK_INVALID_REQ,
					  IB_WC_REM_INV_REQ_ERR);
			state = RESPST_COMPLETE;
			break;

		case RESPST_ERR_MISSING_OPCODE_LAST_D1E:
			state = do_class_d1e_error(qp);
			break;
		case RESPST_ERR_RNR:
			if (qp_type(qp) == IB_QPT_RC) {
				rxe_counter_inc(rxe, RXE_CNT_SND_RNR);
				/* RC - class B */
				send_ack(qp, pkt, AETH_RNR_NAK |
					 (~AETH_TYPE_MASK &
					 qp->attr.min_rnr_timer),
					 pkt->psn);
			} else {
				/* UD/UC - class D */
				qp->resp.drop_msg = 1;
			}
			state = RESPST_CLEANUP;
			break;

		case RESPST_ERR_RKEY_VIOLATION:
			if (qp_type(qp) == IB_QPT_RC) {
				/* Class C */
				do_class_ac_error(qp, AETH_NAK_REM_ACC_ERR,
						  IB_WC_REM_ACCESS_ERR);
				state = RESPST_COMPLETE;
			} else {
				qp->resp.drop_msg = 1;
				if (qp->srq) {
					/* UC/SRQ Class D */
					qp->resp.status = IB_WC_REM_ACCESS_ERR;
					state = RESPST_COMPLETE;
				} else {
					/* UC/non-SRQ Class E. */
					state = RESPST_CLEANUP;
				}
			}
			break;

		case RESPST_ERR_LENGTH:
			if (qp_type(qp) == IB_QPT_RC) {
				/* Class C */
				do_class_ac_error(qp, AETH_NAK_INVALID_REQ,
						  IB_WC_REM_INV_REQ_ERR);
				state = RESPST_COMPLETE;
			} else if (qp->srq) {
				/* UC/UD - class E */
				qp->resp.status = IB_WC_REM_INV_REQ_ERR;
				state = RESPST_COMPLETE;
			} else {
				/* UC/UD - class D */
				qp->resp.drop_msg = 1;
				state = RESPST_CLEANUP;
			}
			break;

		case RESPST_ERR_MALFORMED_WQE:
			/* All, Class A. */
			do_class_ac_error(qp, AETH_NAK_REM_OP_ERR,
					  IB_WC_LOC_QP_OP_ERR);
			state = RESPST_COMPLETE;
			break;

		case RESPST_ERR_CQ_OVERFLOW:
			/* All - Class G */
			state = RESPST_ERROR;
			break;

		case RESPST_DONE:
			if (qp->resp.goto_error) {
				state = RESPST_ERROR;
				break;
			}

			goto done;

		case RESPST_EXIT:
			if (qp->resp.goto_error) {
				state = RESPST_ERROR;
				break;
			}

			goto exit;

		case RESPST_RESET:
			rxe_drain_req_pkts(qp, false);
			qp->resp.wqe = NULL;
			goto exit;

		case RESPST_ERROR:
			qp->resp.goto_error = 0;
			pr_warn("qp#%d moved to error state\n", qp_num(qp));
			rxe_qp_error(qp);
			goto exit;

		default:
			WARN_ON_ONCE(1);
		}
	}

exit:
	ret = -EAGAIN;
done:
	rxe_drop_ref(qp);
	return ret;
}
