/*
 * Copyright (c) 2006, 2007, 2008, 2009 QLogic Corporation. All rights reserved.
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

#include <linux/io.h>

#include "qib.h"

/* cut down ridiculously long IB macro names */
#define OP(x) IB_OPCODE_RC_##x

static void rc_timeout(unsigned long arg);

static u32 restart_sge(struct qib_sge_state *ss, struct qib_swqe *wqe,
		       u32 psn, u32 pmtu)
{
	u32 len;

	len = ((psn - wqe->psn) & QIB_PSN_MASK) * pmtu;
	ss->sge = wqe->sg_list[0];
	ss->sg_list = wqe->sg_list + 1;
	ss->num_sge = wqe->wr.num_sge;
	ss->total_len = wqe->length;
	qib_skip_sge(ss, len, 0);
	return wqe->length - len;
}

static void start_timer(struct qib_qp *qp)
{
	qp->s_flags |= QIB_S_TIMER;
	qp->s_timer.function = rc_timeout;
	/* 4.096 usec. * (1 << qp->timeout) */
	qp->s_timer.expires = jiffies + qp->timeout_jiffies;
	add_timer(&qp->s_timer);
}

/**
 * qib_make_rc_ack - construct a response packet (ACK, NAK, or RDMA read)
 * @dev: the device for this QP
 * @qp: a pointer to the QP
 * @ohdr: a pointer to the IB header being constructed
 * @pmtu: the path MTU
 *
 * Return 1 if constructed; otherwise, return 0.
 * Note that we are in the responder's side of the QP context.
 * Note the QP s_lock must be held.
 */
static int qib_make_rc_ack(struct qib_ibdev *dev, struct qib_qp *qp,
			   struct qib_other_headers *ohdr, u32 pmtu)
{
	struct qib_ack_entry *e;
	u32 hwords;
	u32 len;
	u32 bth0;
	u32 bth2;

	/* Don't send an ACK if we aren't supposed to. */
	if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK))
		goto bail;

	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;

	switch (qp->s_ack_state) {
	case OP(RDMA_READ_RESPONSE_LAST):
	case OP(RDMA_READ_RESPONSE_ONLY):
		e = &qp->s_ack_queue[qp->s_tail_ack_queue];
		if (e->rdma_sge.mr) {
			atomic_dec(&e->rdma_sge.mr->refcount);
			e->rdma_sge.mr = NULL;
		}
		/* FALLTHROUGH */
	case OP(ATOMIC_ACKNOWLEDGE):
		/*
		 * We can increment the tail pointer now that the last
		 * response has been sent instead of only being
		 * constructed.
		 */
		if (++qp->s_tail_ack_queue > QIB_MAX_RDMA_ATOMIC)
			qp->s_tail_ack_queue = 0;
		/* FALLTHROUGH */
	case OP(SEND_ONLY):
	case OP(ACKNOWLEDGE):
		/* Check for no next entry in the queue. */
		if (qp->r_head_ack_queue == qp->s_tail_ack_queue) {
			if (qp->s_flags & QIB_S_ACK_PENDING)
				goto normal;
			goto bail;
		}

		e = &qp->s_ack_queue[qp->s_tail_ack_queue];
		if (e->opcode == OP(RDMA_READ_REQUEST)) {
			/*
			 * If a RDMA read response is being resent and
			 * we haven't seen the duplicate request yet,
			 * then stop sending the remaining responses the
			 * responder has seen until the requester resends it.
			 */
			len = e->rdma_sge.sge_length;
			if (len && !e->rdma_sge.mr) {
				qp->s_tail_ack_queue = qp->r_head_ack_queue;
				goto bail;
			}
			/* Copy SGE state in case we need to resend */
			qp->s_rdma_mr = e->rdma_sge.mr;
			if (qp->s_rdma_mr)
				atomic_inc(&qp->s_rdma_mr->refcount);
			qp->s_ack_rdma_sge.sge = e->rdma_sge;
			qp->s_ack_rdma_sge.num_sge = 1;
			qp->s_cur_sge = &qp->s_ack_rdma_sge;
			if (len > pmtu) {
				len = pmtu;
				qp->s_ack_state = OP(RDMA_READ_RESPONSE_FIRST);
			} else {
				qp->s_ack_state = OP(RDMA_READ_RESPONSE_ONLY);
				e->sent = 1;
			}
			ohdr->u.aeth = qib_compute_aeth(qp);
			hwords++;
			qp->s_ack_rdma_psn = e->psn;
			bth2 = qp->s_ack_rdma_psn++ & QIB_PSN_MASK;
		} else {
			/* COMPARE_SWAP or FETCH_ADD */
			qp->s_cur_sge = NULL;
			len = 0;
			qp->s_ack_state = OP(ATOMIC_ACKNOWLEDGE);
			ohdr->u.at.aeth = qib_compute_aeth(qp);
			ohdr->u.at.atomic_ack_eth[0] =
				cpu_to_be32(e->atomic_data >> 32);
			ohdr->u.at.atomic_ack_eth[1] =
				cpu_to_be32(e->atomic_data);
			hwords += sizeof(ohdr->u.at) / sizeof(u32);
			bth2 = e->psn & QIB_PSN_MASK;
			e->sent = 1;
		}
		bth0 = qp->s_ack_state << 24;
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		qp->s_ack_state = OP(RDMA_READ_RESPONSE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_READ_RESPONSE_MIDDLE):
		qp->s_cur_sge = &qp->s_ack_rdma_sge;
		qp->s_rdma_mr = qp->s_ack_rdma_sge.sge.mr;
		if (qp->s_rdma_mr)
			atomic_inc(&qp->s_rdma_mr->refcount);
		len = qp->s_ack_rdma_sge.sge.sge_length;
		if (len > pmtu)
			len = pmtu;
		else {
			ohdr->u.aeth = qib_compute_aeth(qp);
			hwords++;
			qp->s_ack_state = OP(RDMA_READ_RESPONSE_LAST);
			e = &qp->s_ack_queue[qp->s_tail_ack_queue];
			e->sent = 1;
		}
		bth0 = qp->s_ack_state << 24;
		bth2 = qp->s_ack_rdma_psn++ & QIB_PSN_MASK;
		break;

	default:
normal:
		/*
		 * Send a regular ACK.
		 * Set the s_ack_state so we wait until after sending
		 * the ACK before setting s_ack_state to ACKNOWLEDGE
		 * (see above).
		 */
		qp->s_ack_state = OP(SEND_ONLY);
		qp->s_flags &= ~QIB_S_ACK_PENDING;
		qp->s_cur_sge = NULL;
		if (qp->s_nak_state)
			ohdr->u.aeth =
				cpu_to_be32((qp->r_msn & QIB_MSN_MASK) |
					    (qp->s_nak_state <<
					     QIB_AETH_CREDIT_SHIFT));
		else
			ohdr->u.aeth = qib_compute_aeth(qp);
		hwords++;
		len = 0;
		bth0 = OP(ACKNOWLEDGE) << 24;
		bth2 = qp->s_ack_psn & QIB_PSN_MASK;
	}
	qp->s_rdma_ack_cnt++;
	qp->s_hdrwords = hwords;
	qp->s_cur_size = len;
	qib_make_ruc_header(qp, ohdr, bth0, bth2);
	return 1;

bail:
	qp->s_ack_state = OP(ACKNOWLEDGE);
	qp->s_flags &= ~(QIB_S_RESP_PENDING | QIB_S_ACK_PENDING);
	return 0;
}

/**
 * qib_make_rc_req - construct a request packet (SEND, RDMA r/w, ATOMIC)
 * @qp: a pointer to the QP
 *
 * Return 1 if constructed; otherwise, return 0.
 */
int qib_make_rc_req(struct qib_qp *qp)
{
	struct qib_ibdev *dev = to_idev(qp->ibqp.device);
	struct qib_other_headers *ohdr;
	struct qib_sge_state *ss;
	struct qib_swqe *wqe;
	u32 hwords;
	u32 len;
	u32 bth0;
	u32 bth2;
	u32 pmtu = qp->pmtu;
	char newreq;
	unsigned long flags;
	int ret = 0;
	int delta;

	ohdr = &qp->s_hdr.u.oth;
	if (qp->remote_ah_attr.ah_flags & IB_AH_GRH)
		ohdr = &qp->s_hdr.u.l.oth;

	/*
	 * The lock is needed to synchronize between the sending tasklet,
	 * the receive interrupt handler, and timeout resends.
	 */
	spin_lock_irqsave(&qp->s_lock, flags);

	/* Sending responses has higher priority over sending requests. */
	if ((qp->s_flags & QIB_S_RESP_PENDING) &&
	    qib_make_rc_ack(dev, qp, ohdr, pmtu))
		goto done;

	if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_SEND_OK)) {
		if (!(ib_qib_state_ops[qp->state] & QIB_FLUSH_SEND))
			goto bail;
		/* We are in the error state, flush the work request. */
		if (qp->s_last == qp->s_head)
			goto bail;
		/* If DMAs are in progress, we can't flush immediately. */
		if (atomic_read(&qp->s_dma_busy)) {
			qp->s_flags |= QIB_S_WAIT_DMA;
			goto bail;
		}
		wqe = get_swqe_ptr(qp, qp->s_last);
		qib_send_complete(qp, wqe, qp->s_last != qp->s_acked ?
			IB_WC_SUCCESS : IB_WC_WR_FLUSH_ERR);
		/* will get called again */
		goto done;
	}

	if (qp->s_flags & (QIB_S_WAIT_RNR | QIB_S_WAIT_ACK))
		goto bail;

	if (qib_cmp24(qp->s_psn, qp->s_sending_hpsn) <= 0) {
		if (qib_cmp24(qp->s_sending_psn, qp->s_sending_hpsn) <= 0) {
			qp->s_flags |= QIB_S_WAIT_PSN;
			goto bail;
		}
		qp->s_sending_psn = qp->s_psn;
		qp->s_sending_hpsn = qp->s_psn - 1;
	}

	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;
	bth0 = 0;

	/* Send a request. */
	wqe = get_swqe_ptr(qp, qp->s_cur);
	switch (qp->s_state) {
	default:
		if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_NEXT_SEND_OK))
			goto bail;
		/*
		 * Resend an old request or start a new one.
		 *
		 * We keep track of the current SWQE so that
		 * we don't reset the "furthest progress" state
		 * if we need to back up.
		 */
		newreq = 0;
		if (qp->s_cur == qp->s_tail) {
			/* Check if send work queue is empty. */
			if (qp->s_tail == qp->s_head)
				goto bail;
			/*
			 * If a fence is requested, wait for previous
			 * RDMA read and atomic operations to finish.
			 */
			if ((wqe->wr.send_flags & IB_SEND_FENCE) &&
			    qp->s_num_rd_atomic) {
				qp->s_flags |= QIB_S_WAIT_FENCE;
				goto bail;
			}
			wqe->psn = qp->s_next_psn;
			newreq = 1;
		}
		/*
		 * Note that we have to be careful not to modify the
		 * original work request since we may need to resend
		 * it.
		 */
		len = wqe->length;
		ss = &qp->s_sge;
		bth2 = qp->s_psn & QIB_PSN_MASK;
		switch (wqe->wr.opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			/* If no credit, return. */
			if (!(qp->s_flags & QIB_S_UNLIMITED_CREDIT) &&
			    qib_cmp24(wqe->ssn, qp->s_lsn + 1) > 0) {
				qp->s_flags |= QIB_S_WAIT_SSN_CREDIT;
				goto bail;
			}
			wqe->lpsn = wqe->psn;
			if (len > pmtu) {
				wqe->lpsn += (len - 1) / pmtu;
				qp->s_state = OP(SEND_FIRST);
				len = pmtu;
				break;
			}
			if (wqe->wr.opcode == IB_WR_SEND)
				qp->s_state = OP(SEND_ONLY);
			else {
				qp->s_state = OP(SEND_ONLY_WITH_IMMEDIATE);
				/* Immediate data comes after the BTH */
				ohdr->u.imm_data = wqe->wr.ex.imm_data;
				hwords += 1;
			}
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= IB_BTH_SOLICITED;
			bth2 |= IB_BTH_REQ_ACK;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_RDMA_WRITE:
			if (newreq && !(qp->s_flags & QIB_S_UNLIMITED_CREDIT))
				qp->s_lsn++;
			/* FALLTHROUGH */
		case IB_WR_RDMA_WRITE_WITH_IMM:
			/* If no credit, return. */
			if (!(qp->s_flags & QIB_S_UNLIMITED_CREDIT) &&
			    qib_cmp24(wqe->ssn, qp->s_lsn + 1) > 0) {
				qp->s_flags |= QIB_S_WAIT_SSN_CREDIT;
				goto bail;
			}
			ohdr->u.rc.reth.vaddr =
				cpu_to_be64(wqe->wr.wr.rdma.remote_addr);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->wr.wr.rdma.rkey);
			ohdr->u.rc.reth.length = cpu_to_be32(len);
			hwords += sizeof(struct ib_reth) / sizeof(u32);
			wqe->lpsn = wqe->psn;
			if (len > pmtu) {
				wqe->lpsn += (len - 1) / pmtu;
				qp->s_state = OP(RDMA_WRITE_FIRST);
				len = pmtu;
				break;
			}
			if (wqe->wr.opcode == IB_WR_RDMA_WRITE)
				qp->s_state = OP(RDMA_WRITE_ONLY);
			else {
				qp->s_state =
					OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE);
				/* Immediate data comes after RETH */
				ohdr->u.rc.imm_data = wqe->wr.ex.imm_data;
				hwords += 1;
				if (wqe->wr.send_flags & IB_SEND_SOLICITED)
					bth0 |= IB_BTH_SOLICITED;
			}
			bth2 |= IB_BTH_REQ_ACK;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_RDMA_READ:
			/*
			 * Don't allow more operations to be started
			 * than the QP limits allow.
			 */
			if (newreq) {
				if (qp->s_num_rd_atomic >=
				    qp->s_max_rd_atomic) {
					qp->s_flags |= QIB_S_WAIT_RDMAR;
					goto bail;
				}
				qp->s_num_rd_atomic++;
				if (!(qp->s_flags & QIB_S_UNLIMITED_CREDIT))
					qp->s_lsn++;
				/*
				 * Adjust s_next_psn to count the
				 * expected number of responses.
				 */
				if (len > pmtu)
					qp->s_next_psn += (len - 1) / pmtu;
				wqe->lpsn = qp->s_next_psn++;
			}
			ohdr->u.rc.reth.vaddr =
				cpu_to_be64(wqe->wr.wr.rdma.remote_addr);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->wr.wr.rdma.rkey);
			ohdr->u.rc.reth.length = cpu_to_be32(len);
			qp->s_state = OP(RDMA_READ_REQUEST);
			hwords += sizeof(ohdr->u.rc.reth) / sizeof(u32);
			ss = NULL;
			len = 0;
			bth2 |= IB_BTH_REQ_ACK;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_ATOMIC_CMP_AND_SWP:
		case IB_WR_ATOMIC_FETCH_AND_ADD:
			/*
			 * Don't allow more operations to be started
			 * than the QP limits allow.
			 */
			if (newreq) {
				if (qp->s_num_rd_atomic >=
				    qp->s_max_rd_atomic) {
					qp->s_flags |= QIB_S_WAIT_RDMAR;
					goto bail;
				}
				qp->s_num_rd_atomic++;
				if (!(qp->s_flags & QIB_S_UNLIMITED_CREDIT))
					qp->s_lsn++;
				wqe->lpsn = wqe->psn;
			}
			if (wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP) {
				qp->s_state = OP(COMPARE_SWAP);
				ohdr->u.atomic_eth.swap_data = cpu_to_be64(
					wqe->wr.wr.atomic.swap);
				ohdr->u.atomic_eth.compare_data = cpu_to_be64(
					wqe->wr.wr.atomic.compare_add);
			} else {
				qp->s_state = OP(FETCH_ADD);
				ohdr->u.atomic_eth.swap_data = cpu_to_be64(
					wqe->wr.wr.atomic.compare_add);
				ohdr->u.atomic_eth.compare_data = 0;
			}
			ohdr->u.atomic_eth.vaddr[0] = cpu_to_be32(
				wqe->wr.wr.atomic.remote_addr >> 32);
			ohdr->u.atomic_eth.vaddr[1] = cpu_to_be32(
				wqe->wr.wr.atomic.remote_addr);
			ohdr->u.atomic_eth.rkey = cpu_to_be32(
				wqe->wr.wr.atomic.rkey);
			hwords += sizeof(struct ib_atomic_eth) / sizeof(u32);
			ss = NULL;
			len = 0;
			bth2 |= IB_BTH_REQ_ACK;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		default:
			goto bail;
		}
		qp->s_sge.sge = wqe->sg_list[0];
		qp->s_sge.sg_list = wqe->sg_list + 1;
		qp->s_sge.num_sge = wqe->wr.num_sge;
		qp->s_sge.total_len = wqe->length;
		qp->s_len = wqe->length;
		if (newreq) {
			qp->s_tail++;
			if (qp->s_tail >= qp->s_size)
				qp->s_tail = 0;
		}
		if (wqe->wr.opcode == IB_WR_RDMA_READ)
			qp->s_psn = wqe->lpsn + 1;
		else {
			qp->s_psn++;
			if (qib_cmp24(qp->s_psn, qp->s_next_psn) > 0)
				qp->s_next_psn = qp->s_psn;
		}
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		/*
		 * qp->s_state is normally set to the opcode of the
		 * last packet constructed for new requests and therefore
		 * is never set to RDMA read response.
		 * RDMA_READ_RESPONSE_FIRST is used by the ACK processing
		 * thread to indicate a SEND needs to be restarted from an
		 * earlier PSN without interferring with the sending thread.
		 * See qib_restart_rc().
		 */
		qp->s_len = restart_sge(&qp->s_sge, wqe, qp->s_psn, pmtu);
		/* FALLTHROUGH */
	case OP(SEND_FIRST):
		qp->s_state = OP(SEND_MIDDLE);
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		bth2 = qp->s_psn++ & QIB_PSN_MASK;
		if (qib_cmp24(qp->s_psn, qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		ss = &qp->s_sge;
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			break;
		}
		if (wqe->wr.opcode == IB_WR_SEND)
			qp->s_state = OP(SEND_LAST);
		else {
			qp->s_state = OP(SEND_LAST_WITH_IMMEDIATE);
			/* Immediate data comes after the BTH */
			ohdr->u.imm_data = wqe->wr.ex.imm_data;
			hwords += 1;
		}
		if (wqe->wr.send_flags & IB_SEND_SOLICITED)
			bth0 |= IB_BTH_SOLICITED;
		bth2 |= IB_BTH_REQ_ACK;
		qp->s_cur++;
		if (qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		break;

	case OP(RDMA_READ_RESPONSE_LAST):
		/*
		 * qp->s_state is normally set to the opcode of the
		 * last packet constructed for new requests and therefore
		 * is never set to RDMA read response.
		 * RDMA_READ_RESPONSE_LAST is used by the ACK processing
		 * thread to indicate a RDMA write needs to be restarted from
		 * an earlier PSN without interferring with the sending thread.
		 * See qib_restart_rc().
		 */
		qp->s_len = restart_sge(&qp->s_sge, wqe, qp->s_psn, pmtu);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_FIRST):
		qp->s_state = OP(RDMA_WRITE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		bth2 = qp->s_psn++ & QIB_PSN_MASK;
		if (qib_cmp24(qp->s_psn, qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		ss = &qp->s_sge;
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			break;
		}
		if (wqe->wr.opcode == IB_WR_RDMA_WRITE)
			qp->s_state = OP(RDMA_WRITE_LAST);
		else {
			qp->s_state = OP(RDMA_WRITE_LAST_WITH_IMMEDIATE);
			/* Immediate data comes after the BTH */
			ohdr->u.imm_data = wqe->wr.ex.imm_data;
			hwords += 1;
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= IB_BTH_SOLICITED;
		}
		bth2 |= IB_BTH_REQ_ACK;
		qp->s_cur++;
		if (qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		break;

	case OP(RDMA_READ_RESPONSE_MIDDLE):
		/*
		 * qp->s_state is normally set to the opcode of the
		 * last packet constructed for new requests and therefore
		 * is never set to RDMA read response.
		 * RDMA_READ_RESPONSE_MIDDLE is used by the ACK processing
		 * thread to indicate a RDMA read needs to be restarted from
		 * an earlier PSN without interferring with the sending thread.
		 * See qib_restart_rc().
		 */
		len = ((qp->s_psn - wqe->psn) & QIB_PSN_MASK) * pmtu;
		ohdr->u.rc.reth.vaddr =
			cpu_to_be64(wqe->wr.wr.rdma.remote_addr + len);
		ohdr->u.rc.reth.rkey =
			cpu_to_be32(wqe->wr.wr.rdma.rkey);
		ohdr->u.rc.reth.length = cpu_to_be32(wqe->length - len);
		qp->s_state = OP(RDMA_READ_REQUEST);
		hwords += sizeof(ohdr->u.rc.reth) / sizeof(u32);
		bth2 = (qp->s_psn & QIB_PSN_MASK) | IB_BTH_REQ_ACK;
		qp->s_psn = wqe->lpsn + 1;
		ss = NULL;
		len = 0;
		qp->s_cur++;
		if (qp->s_cur == qp->s_size)
			qp->s_cur = 0;
		break;
	}
	qp->s_sending_hpsn = bth2;
	delta = (((int) bth2 - (int) wqe->psn) << 8) >> 8;
	if (delta && delta % QIB_PSN_CREDIT == 0)
		bth2 |= IB_BTH_REQ_ACK;
	if (qp->s_flags & QIB_S_SEND_ONE) {
		qp->s_flags &= ~QIB_S_SEND_ONE;
		qp->s_flags |= QIB_S_WAIT_ACK;
		bth2 |= IB_BTH_REQ_ACK;
	}
	qp->s_len -= len;
	qp->s_hdrwords = hwords;
	qp->s_cur_sge = ss;
	qp->s_cur_size = len;
	qib_make_ruc_header(qp, ohdr, bth0 | (qp->s_state << 24), bth2);
done:
	ret = 1;
	goto unlock;

bail:
	qp->s_flags &= ~QIB_S_BUSY;
unlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

/**
 * qib_send_rc_ack - Construct an ACK packet and send it
 * @qp: a pointer to the QP
 *
 * This is called from qib_rc_rcv() and qib_kreceive().
 * Note that RDMA reads and atomics are handled in the
 * send side QP state and tasklet.
 */
void qib_send_rc_ack(struct qib_qp *qp)
{
	struct qib_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	u64 pbc;
	u16 lrh0;
	u32 bth0;
	u32 hwords;
	u32 pbufn;
	u32 __iomem *piobuf;
	struct qib_ib_header hdr;
	struct qib_other_headers *ohdr;
	u32 control;
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);

	if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK))
		goto unlock;

	/* Don't send ACK or NAK if a RDMA read or atomic is pending. */
	if ((qp->s_flags & QIB_S_RESP_PENDING) || qp->s_rdma_ack_cnt)
		goto queue_ack;

	/* Construct the header with s_lock held so APM doesn't change it. */
	ohdr = &hdr.u.oth;
	lrh0 = QIB_LRH_BTH;
	/* header size in 32-bit words LRH+BTH+AETH = (8+12+4)/4. */
	hwords = 6;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		hwords += qib_make_grh(ibp, &hdr.u.l.grh,
				       &qp->remote_ah_attr.grh, hwords, 0);
		ohdr = &hdr.u.l.oth;
		lrh0 = QIB_LRH_GRH;
	}
	/* read pkey_index w/o lock (its atomic) */
	bth0 = qib_get_pkey(ibp, qp->s_pkey_index) | (OP(ACKNOWLEDGE) << 24);
	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth0 |= IB_BTH_MIG_REQ;
	if (qp->r_nak_state)
		ohdr->u.aeth = cpu_to_be32((qp->r_msn & QIB_MSN_MASK) |
					    (qp->r_nak_state <<
					     QIB_AETH_CREDIT_SHIFT));
	else
		ohdr->u.aeth = qib_compute_aeth(qp);
	lrh0 |= ibp->sl_to_vl[qp->remote_ah_attr.sl] << 12 |
		qp->remote_ah_attr.sl << 4;
	hdr.lrh[0] = cpu_to_be16(lrh0);
	hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	hdr.lrh[2] = cpu_to_be16(hwords + SIZE_OF_CRC);
	hdr.lrh[3] = cpu_to_be16(ppd->lid | qp->remote_ah_attr.src_path_bits);
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(qp->r_ack_psn & QIB_PSN_MASK);

	spin_unlock_irqrestore(&qp->s_lock, flags);

	/* Don't try to send ACKs if the link isn't ACTIVE */
	if (!(ppd->lflags & QIBL_LINKACTIVE))
		goto done;

	control = dd->f_setpbc_control(ppd, hwords + SIZE_OF_CRC,
				       qp->s_srate, lrh0 >> 12);
	/* length is + 1 for the control dword */
	pbc = ((u64) control << 32) | (hwords + 1);

	piobuf = dd->f_getsendbuf(ppd, pbc, &pbufn);
	if (!piobuf) {
		/*
		 * We are out of PIO buffers at the moment.
		 * Pass responsibility for sending the ACK to the
		 * send tasklet so that when a PIO buffer becomes
		 * available, the ACK is sent ahead of other outgoing
		 * packets.
		 */
		spin_lock_irqsave(&qp->s_lock, flags);
		goto queue_ack;
	}

	/*
	 * Write the pbc.
	 * We have to flush after the PBC for correctness
	 * on some cpus or WC buffer can be written out of order.
	 */
	writeq(pbc, piobuf);

	if (dd->flags & QIB_PIO_FLUSH_WC) {
		u32 *hdrp = (u32 *) &hdr;

		qib_flush_wc();
		qib_pio_copy(piobuf + 2, hdrp, hwords - 1);
		qib_flush_wc();
		__raw_writel(hdrp[hwords - 1], piobuf + hwords + 1);
	} else
		qib_pio_copy(piobuf + 2, (u32 *) &hdr, hwords);

	if (dd->flags & QIB_USE_SPCL_TRIG) {
		u32 spcl_off = (pbufn >= dd->piobcnt2k) ? 2047 : 1023;

		qib_flush_wc();
		__raw_writel(0xaebecede, piobuf + spcl_off);
	}

	qib_flush_wc();
	qib_sendbuf_done(dd, pbufn);

	ibp->n_unicast_xmit++;
	goto done;

queue_ack:
	if (ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK) {
		ibp->n_rc_qacks++;
		qp->s_flags |= QIB_S_ACK_PENDING | QIB_S_RESP_PENDING;
		qp->s_nak_state = qp->r_nak_state;
		qp->s_ack_psn = qp->r_ack_psn;

		/* Schedule the send tasklet. */
		qib_schedule_send(qp);
	}
unlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
done:
	return;
}

/**
 * reset_psn - reset the QP state to send starting from PSN
 * @qp: the QP
 * @psn: the packet sequence number to restart at
 *
 * This is called from qib_rc_rcv() to process an incoming RC ACK
 * for the given QP.
 * Called at interrupt level with the QP s_lock held.
 */
static void reset_psn(struct qib_qp *qp, u32 psn)
{
	u32 n = qp->s_acked;
	struct qib_swqe *wqe = get_swqe_ptr(qp, n);
	u32 opcode;

	qp->s_cur = n;

	/*
	 * If we are starting the request from the beginning,
	 * let the normal send code handle initialization.
	 */
	if (qib_cmp24(psn, wqe->psn) <= 0) {
		qp->s_state = OP(SEND_LAST);
		goto done;
	}

	/* Find the work request opcode corresponding to the given PSN. */
	opcode = wqe->wr.opcode;
	for (;;) {
		int diff;

		if (++n == qp->s_size)
			n = 0;
		if (n == qp->s_tail)
			break;
		wqe = get_swqe_ptr(qp, n);
		diff = qib_cmp24(psn, wqe->psn);
		if (diff < 0)
			break;
		qp->s_cur = n;
		/*
		 * If we are starting the request from the beginning,
		 * let the normal send code handle initialization.
		 */
		if (diff == 0) {
			qp->s_state = OP(SEND_LAST);
			goto done;
		}
		opcode = wqe->wr.opcode;
	}

	/*
	 * Set the state to restart in the middle of a request.
	 * Don't change the s_sge, s_cur_sge, or s_cur_size.
	 * See qib_make_rc_req().
	 */
	switch (opcode) {
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
		qp->s_state = OP(RDMA_READ_RESPONSE_FIRST);
		break;

	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		qp->s_state = OP(RDMA_READ_RESPONSE_LAST);
		break;

	case IB_WR_RDMA_READ:
		qp->s_state = OP(RDMA_READ_RESPONSE_MIDDLE);
		break;

	default:
		/*
		 * This case shouldn't happen since its only
		 * one PSN per req.
		 */
		qp->s_state = OP(SEND_LAST);
	}
done:
	qp->s_psn = psn;
	/*
	 * Set QIB_S_WAIT_PSN as qib_rc_complete() may start the timer
	 * asynchronously before the send tasklet can get scheduled.
	 * Doing it in qib_make_rc_req() is too late.
	 */
	if ((qib_cmp24(qp->s_psn, qp->s_sending_hpsn) <= 0) &&
	    (qib_cmp24(qp->s_sending_psn, qp->s_sending_hpsn) <= 0))
		qp->s_flags |= QIB_S_WAIT_PSN;
}

/*
 * Back up requester to resend the last un-ACKed request.
 * The QP r_lock and s_lock should be held and interrupts disabled.
 */
static void qib_restart_rc(struct qib_qp *qp, u32 psn, int wait)
{
	struct qib_swqe *wqe = get_swqe_ptr(qp, qp->s_acked);
	struct qib_ibport *ibp;

	if (qp->s_retry == 0) {
		if (qp->s_mig_state == IB_MIG_ARMED) {
			qib_migrate_qp(qp);
			qp->s_retry = qp->s_retry_cnt;
		} else if (qp->s_last == qp->s_acked) {
			qib_send_complete(qp, wqe, IB_WC_RETRY_EXC_ERR);
			qib_error_qp(qp, IB_WC_WR_FLUSH_ERR);
			return;
		} else /* XXX need to handle delayed completion */
			return;
	} else
		qp->s_retry--;

	ibp = to_iport(qp->ibqp.device, qp->port_num);
	if (wqe->wr.opcode == IB_WR_RDMA_READ)
		ibp->n_rc_resends++;
	else
		ibp->n_rc_resends += (qp->s_psn - psn) & QIB_PSN_MASK;

	qp->s_flags &= ~(QIB_S_WAIT_FENCE | QIB_S_WAIT_RDMAR |
			 QIB_S_WAIT_SSN_CREDIT | QIB_S_WAIT_PSN |
			 QIB_S_WAIT_ACK);
	if (wait)
		qp->s_flags |= QIB_S_SEND_ONE;
	reset_psn(qp, psn);
}

/*
 * This is called from s_timer for missing responses.
 */
static void rc_timeout(unsigned long arg)
{
	struct qib_qp *qp = (struct qib_qp *)arg;
	struct qib_ibport *ibp;
	unsigned long flags;

	spin_lock_irqsave(&qp->r_lock, flags);
	spin_lock(&qp->s_lock);
	if (qp->s_flags & QIB_S_TIMER) {
		ibp = to_iport(qp->ibqp.device, qp->port_num);
		ibp->n_rc_timeouts++;
		qp->s_flags &= ~QIB_S_TIMER;
		del_timer(&qp->s_timer);
		qib_restart_rc(qp, qp->s_last_psn + 1, 1);
		qib_schedule_send(qp);
	}
	spin_unlock(&qp->s_lock);
	spin_unlock_irqrestore(&qp->r_lock, flags);
}

/*
 * This is called from s_timer for RNR timeouts.
 */
void qib_rc_rnr_retry(unsigned long arg)
{
	struct qib_qp *qp = (struct qib_qp *)arg;
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (qp->s_flags & QIB_S_WAIT_RNR) {
		qp->s_flags &= ~QIB_S_WAIT_RNR;
		del_timer(&qp->s_timer);
		qib_schedule_send(qp);
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
}

/*
 * Set qp->s_sending_psn to the next PSN after the given one.
 * This would be psn+1 except when RDMA reads are present.
 */
static void reset_sending_psn(struct qib_qp *qp, u32 psn)
{
	struct qib_swqe *wqe;
	u32 n = qp->s_last;

	/* Find the work request corresponding to the given PSN. */
	for (;;) {
		wqe = get_swqe_ptr(qp, n);
		if (qib_cmp24(psn, wqe->lpsn) <= 0) {
			if (wqe->wr.opcode == IB_WR_RDMA_READ)
				qp->s_sending_psn = wqe->lpsn + 1;
			else
				qp->s_sending_psn = psn + 1;
			break;
		}
		if (++n == qp->s_size)
			n = 0;
		if (n == qp->s_tail)
			break;
	}
}

/*
 * This should be called with the QP s_lock held and interrupts disabled.
 */
void qib_rc_send_complete(struct qib_qp *qp, struct qib_ib_header *hdr)
{
	struct qib_other_headers *ohdr;
	struct qib_swqe *wqe;
	struct ib_wc wc;
	unsigned i;
	u32 opcode;
	u32 psn;

	if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_OR_FLUSH_SEND))
		return;

	/* Find out where the BTH is */
	if ((be16_to_cpu(hdr->lrh[0]) & 3) == QIB_LRH_BTH)
		ohdr = &hdr->u.oth;
	else
		ohdr = &hdr->u.l.oth;

	opcode = be32_to_cpu(ohdr->bth[0]) >> 24;
	if (opcode >= OP(RDMA_READ_RESPONSE_FIRST) &&
	    opcode <= OP(ATOMIC_ACKNOWLEDGE)) {
		WARN_ON(!qp->s_rdma_ack_cnt);
		qp->s_rdma_ack_cnt--;
		return;
	}

	psn = be32_to_cpu(ohdr->bth[2]);
	reset_sending_psn(qp, psn);

	/*
	 * Start timer after a packet requesting an ACK has been sent and
	 * there are still requests that haven't been acked.
	 */
	if ((psn & IB_BTH_REQ_ACK) && qp->s_acked != qp->s_tail &&
	    !(qp->s_flags & (QIB_S_TIMER | QIB_S_WAIT_RNR | QIB_S_WAIT_PSN)) &&
	    (ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK))
		start_timer(qp);

	while (qp->s_last != qp->s_acked) {
		wqe = get_swqe_ptr(qp, qp->s_last);
		if (qib_cmp24(wqe->lpsn, qp->s_sending_psn) >= 0 &&
		    qib_cmp24(qp->s_sending_psn, qp->s_sending_hpsn) <= 0)
			break;
		for (i = 0; i < wqe->wr.num_sge; i++) {
			struct qib_sge *sge = &wqe->sg_list[i];

			atomic_dec(&sge->mr->refcount);
		}
		/* Post a send completion queue entry if requested. */
		if (!(qp->s_flags & QIB_S_SIGNAL_REQ_WR) ||
		    (wqe->wr.send_flags & IB_SEND_SIGNALED)) {
			memset(&wc, 0, sizeof wc);
			wc.wr_id = wqe->wr.wr_id;
			wc.status = IB_WC_SUCCESS;
			wc.opcode = ib_qib_wc_opcode[wqe->wr.opcode];
			wc.byte_len = wqe->length;
			wc.qp = &qp->ibqp;
			qib_cq_enter(to_icq(qp->ibqp.send_cq), &wc, 0);
		}
		if (++qp->s_last >= qp->s_size)
			qp->s_last = 0;
	}
	/*
	 * If we were waiting for sends to complete before resending,
	 * and they are now complete, restart sending.
	 */
	if (qp->s_flags & QIB_S_WAIT_PSN &&
	    qib_cmp24(qp->s_sending_psn, qp->s_sending_hpsn) > 0) {
		qp->s_flags &= ~QIB_S_WAIT_PSN;
		qp->s_sending_psn = qp->s_psn;
		qp->s_sending_hpsn = qp->s_psn - 1;
		qib_schedule_send(qp);
	}
}

static inline void update_last_psn(struct qib_qp *qp, u32 psn)
{
	qp->s_last_psn = psn;
}

/*
 * Generate a SWQE completion.
 * This is similar to qib_send_complete but has to check to be sure
 * that the SGEs are not being referenced if the SWQE is being resent.
 */
static struct qib_swqe *do_rc_completion(struct qib_qp *qp,
					 struct qib_swqe *wqe,
					 struct qib_ibport *ibp)
{
	struct ib_wc wc;
	unsigned i;

	/*
	 * Don't decrement refcount and don't generate a
	 * completion if the SWQE is being resent until the send
	 * is finished.
	 */
	if (qib_cmp24(wqe->lpsn, qp->s_sending_psn) < 0 ||
	    qib_cmp24(qp->s_sending_psn, qp->s_sending_hpsn) > 0) {
		for (i = 0; i < wqe->wr.num_sge; i++) {
			struct qib_sge *sge = &wqe->sg_list[i];

			atomic_dec(&sge->mr->refcount);
		}
		/* Post a send completion queue entry if requested. */
		if (!(qp->s_flags & QIB_S_SIGNAL_REQ_WR) ||
		    (wqe->wr.send_flags & IB_SEND_SIGNALED)) {
			memset(&wc, 0, sizeof wc);
			wc.wr_id = wqe->wr.wr_id;
			wc.status = IB_WC_SUCCESS;
			wc.opcode = ib_qib_wc_opcode[wqe->wr.opcode];
			wc.byte_len = wqe->length;
			wc.qp = &qp->ibqp;
			qib_cq_enter(to_icq(qp->ibqp.send_cq), &wc, 0);
		}
		if (++qp->s_last >= qp->s_size)
			qp->s_last = 0;
	} else
		ibp->n_rc_delayed_comp++;

	qp->s_retry = qp->s_retry_cnt;
	update_last_psn(qp, wqe->lpsn);

	/*
	 * If we are completing a request which is in the process of
	 * being resent, we can stop resending it since we know the
	 * responder has already seen it.
	 */
	if (qp->s_acked == qp->s_cur) {
		if (++qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		qp->s_acked = qp->s_cur;
		wqe = get_swqe_ptr(qp, qp->s_cur);
		if (qp->s_acked != qp->s_tail) {
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = wqe->psn;
		}
	} else {
		if (++qp->s_acked >= qp->s_size)
			qp->s_acked = 0;
		if (qp->state == IB_QPS_SQD && qp->s_acked == qp->s_cur)
			qp->s_draining = 0;
		wqe = get_swqe_ptr(qp, qp->s_acked);
	}
	return wqe;
}

/**
 * do_rc_ack - process an incoming RC ACK
 * @qp: the QP the ACK came in on
 * @psn: the packet sequence number of the ACK
 * @opcode: the opcode of the request that resulted in the ACK
 *
 * This is called from qib_rc_rcv_resp() to process an incoming RC ACK
 * for the given QP.
 * Called at interrupt level with the QP s_lock held.
 * Returns 1 if OK, 0 if current operation should be aborted (NAK).
 */
static int do_rc_ack(struct qib_qp *qp, u32 aeth, u32 psn, int opcode,
		     u64 val, struct qib_ctxtdata *rcd)
{
	struct qib_ibport *ibp;
	enum ib_wc_status status;
	struct qib_swqe *wqe;
	int ret = 0;
	u32 ack_psn;
	int diff;

	/* Remove QP from retry timer */
	if (qp->s_flags & (QIB_S_TIMER | QIB_S_WAIT_RNR)) {
		qp->s_flags &= ~(QIB_S_TIMER | QIB_S_WAIT_RNR);
		del_timer(&qp->s_timer);
	}

	/*
	 * Note that NAKs implicitly ACK outstanding SEND and RDMA write
	 * requests and implicitly NAK RDMA read and atomic requests issued
	 * before the NAK'ed request.  The MSN won't include the NAK'ed
	 * request but will include an ACK'ed request(s).
	 */
	ack_psn = psn;
	if (aeth >> 29)
		ack_psn--;
	wqe = get_swqe_ptr(qp, qp->s_acked);
	ibp = to_iport(qp->ibqp.device, qp->port_num);

	/*
	 * The MSN might be for a later WQE than the PSN indicates so
	 * only complete WQEs that the PSN finishes.
	 */
	while ((diff = qib_cmp24(ack_psn, wqe->lpsn)) >= 0) {
		/*
		 * RDMA_READ_RESPONSE_ONLY is a special case since
		 * we want to generate completion events for everything
		 * before the RDMA read, copy the data, then generate
		 * the completion for the read.
		 */
		if (wqe->wr.opcode == IB_WR_RDMA_READ &&
		    opcode == OP(RDMA_READ_RESPONSE_ONLY) &&
		    diff == 0) {
			ret = 1;
			goto bail;
		}
		/*
		 * If this request is a RDMA read or atomic, and the ACK is
		 * for a later operation, this ACK NAKs the RDMA read or
		 * atomic.  In other words, only a RDMA_READ_LAST or ONLY
		 * can ACK a RDMA read and likewise for atomic ops.  Note
		 * that the NAK case can only happen if relaxed ordering is
		 * used and requests are sent after an RDMA read or atomic
		 * is sent but before the response is received.
		 */
		if ((wqe->wr.opcode == IB_WR_RDMA_READ &&
		     (opcode != OP(RDMA_READ_RESPONSE_LAST) || diff != 0)) ||
		    ((wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		      wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) &&
		     (opcode != OP(ATOMIC_ACKNOWLEDGE) || diff != 0))) {
			/* Retry this request. */
			if (!(qp->r_flags & QIB_R_RDMAR_SEQ)) {
				qp->r_flags |= QIB_R_RDMAR_SEQ;
				qib_restart_rc(qp, qp->s_last_psn + 1, 0);
				if (list_empty(&qp->rspwait)) {
					qp->r_flags |= QIB_R_RSP_SEND;
					atomic_inc(&qp->refcount);
					list_add_tail(&qp->rspwait,
						      &rcd->qp_wait_list);
				}
			}
			/*
			 * No need to process the ACK/NAK since we are
			 * restarting an earlier request.
			 */
			goto bail;
		}
		if (wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		    wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) {
			u64 *vaddr = wqe->sg_list[0].vaddr;
			*vaddr = val;
		}
		if (qp->s_num_rd_atomic &&
		    (wqe->wr.opcode == IB_WR_RDMA_READ ||
		     wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		     wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD)) {
			qp->s_num_rd_atomic--;
			/* Restart sending task if fence is complete */
			if ((qp->s_flags & QIB_S_WAIT_FENCE) &&
			    !qp->s_num_rd_atomic) {
				qp->s_flags &= ~(QIB_S_WAIT_FENCE |
						 QIB_S_WAIT_ACK);
				qib_schedule_send(qp);
			} else if (qp->s_flags & QIB_S_WAIT_RDMAR) {
				qp->s_flags &= ~(QIB_S_WAIT_RDMAR |
						 QIB_S_WAIT_ACK);
				qib_schedule_send(qp);
			}
		}
		wqe = do_rc_completion(qp, wqe, ibp);
		if (qp->s_acked == qp->s_tail)
			break;
	}

	switch (aeth >> 29) {
	case 0:         /* ACK */
		ibp->n_rc_acks++;
		if (qp->s_acked != qp->s_tail) {
			/*
			 * We are expecting more ACKs so
			 * reset the retransmit timer.
			 */
			start_timer(qp);
			/*
			 * We can stop resending the earlier packets and
			 * continue with the next packet the receiver wants.
			 */
			if (qib_cmp24(qp->s_psn, psn) <= 0)
				reset_psn(qp, psn + 1);
		} else if (qib_cmp24(qp->s_psn, psn) <= 0) {
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = psn + 1;
		}
		if (qp->s_flags & QIB_S_WAIT_ACK) {
			qp->s_flags &= ~QIB_S_WAIT_ACK;
			qib_schedule_send(qp);
		}
		qib_get_credit(qp, aeth);
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		qp->s_retry = qp->s_retry_cnt;
		update_last_psn(qp, psn);
		ret = 1;
		goto bail;

	case 1:         /* RNR NAK */
		ibp->n_rnr_naks++;
		if (qp->s_acked == qp->s_tail)
			goto bail;
		if (qp->s_flags & QIB_S_WAIT_RNR)
			goto bail;
		if (qp->s_rnr_retry == 0) {
			status = IB_WC_RNR_RETRY_EXC_ERR;
			goto class_b;
		}
		if (qp->s_rnr_retry_cnt < 7)
			qp->s_rnr_retry--;

		/* The last valid PSN is the previous PSN. */
		update_last_psn(qp, psn - 1);

		ibp->n_rc_resends += (qp->s_psn - psn) & QIB_PSN_MASK;

		reset_psn(qp, psn);

		qp->s_flags &= ~(QIB_S_WAIT_SSN_CREDIT | QIB_S_WAIT_ACK);
		qp->s_flags |= QIB_S_WAIT_RNR;
		qp->s_timer.function = qib_rc_rnr_retry;
		qp->s_timer.expires = jiffies + usecs_to_jiffies(
			ib_qib_rnr_table[(aeth >> QIB_AETH_CREDIT_SHIFT) &
					   QIB_AETH_CREDIT_MASK]);
		add_timer(&qp->s_timer);
		goto bail;

	case 3:         /* NAK */
		if (qp->s_acked == qp->s_tail)
			goto bail;
		/* The last valid PSN is the previous PSN. */
		update_last_psn(qp, psn - 1);
		switch ((aeth >> QIB_AETH_CREDIT_SHIFT) &
			QIB_AETH_CREDIT_MASK) {
		case 0: /* PSN sequence error */
			ibp->n_seq_naks++;
			/*
			 * Back up to the responder's expected PSN.
			 * Note that we might get a NAK in the middle of an
			 * RDMA READ response which terminates the RDMA
			 * READ.
			 */
			qib_restart_rc(qp, psn, 0);
			qib_schedule_send(qp);
			break;

		case 1: /* Invalid Request */
			status = IB_WC_REM_INV_REQ_ERR;
			ibp->n_other_naks++;
			goto class_b;

		case 2: /* Remote Access Error */
			status = IB_WC_REM_ACCESS_ERR;
			ibp->n_other_naks++;
			goto class_b;

		case 3: /* Remote Operation Error */
			status = IB_WC_REM_OP_ERR;
			ibp->n_other_naks++;
class_b:
			if (qp->s_last == qp->s_acked) {
				qib_send_complete(qp, wqe, status);
				qib_error_qp(qp, IB_WC_WR_FLUSH_ERR);
			}
			break;

		default:
			/* Ignore other reserved NAK error codes */
			goto reserved;
		}
		qp->s_retry = qp->s_retry_cnt;
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		goto bail;

	default:                /* 2: reserved */
reserved:
		/* Ignore reserved NAK codes. */
		goto bail;
	}

bail:
	return ret;
}

/*
 * We have seen an out of sequence RDMA read middle or last packet.
 * This ACKs SENDs and RDMA writes up to the first RDMA read or atomic SWQE.
 */
static void rdma_seq_err(struct qib_qp *qp, struct qib_ibport *ibp, u32 psn,
			 struct qib_ctxtdata *rcd)
{
	struct qib_swqe *wqe;

	/* Remove QP from retry timer */
	if (qp->s_flags & (QIB_S_TIMER | QIB_S_WAIT_RNR)) {
		qp->s_flags &= ~(QIB_S_TIMER | QIB_S_WAIT_RNR);
		del_timer(&qp->s_timer);
	}

	wqe = get_swqe_ptr(qp, qp->s_acked);

	while (qib_cmp24(psn, wqe->lpsn) > 0) {
		if (wqe->wr.opcode == IB_WR_RDMA_READ ||
		    wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		    wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD)
			break;
		wqe = do_rc_completion(qp, wqe, ibp);
	}

	ibp->n_rdma_seq++;
	qp->r_flags |= QIB_R_RDMAR_SEQ;
	qib_restart_rc(qp, qp->s_last_psn + 1, 0);
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= QIB_R_RSP_SEND;
		atomic_inc(&qp->refcount);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
}

/**
 * qib_rc_rcv_resp - process an incoming RC response packet
 * @ibp: the port this packet came in on
 * @ohdr: the other headers for this packet
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP for this packet
 * @opcode: the opcode for this packet
 * @psn: the packet sequence number for this packet
 * @hdrsize: the header length
 * @pmtu: the path MTU
 *
 * This is called from qib_rc_rcv() to process an incoming RC response
 * packet for the given QP.
 * Called at interrupt level.
 */
static void qib_rc_rcv_resp(struct qib_ibport *ibp,
			    struct qib_other_headers *ohdr,
			    void *data, u32 tlen,
			    struct qib_qp *qp,
			    u32 opcode,
			    u32 psn, u32 hdrsize, u32 pmtu,
			    struct qib_ctxtdata *rcd)
{
	struct qib_swqe *wqe;
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	enum ib_wc_status status;
	unsigned long flags;
	int diff;
	u32 pad;
	u32 aeth;
	u64 val;

	if (opcode != OP(RDMA_READ_RESPONSE_MIDDLE)) {
		/*
		 * If ACK'd PSN on SDMA busy list try to make progress to
		 * reclaim SDMA credits.
		 */
		if ((qib_cmp24(psn, qp->s_sending_psn) >= 0) &&
		    (qib_cmp24(qp->s_sending_psn, qp->s_sending_hpsn) <= 0)) {

			/*
			 * If send tasklet not running attempt to progress
			 * SDMA queue.
			 */
			if (!(qp->s_flags & QIB_S_BUSY)) {
				/* Acquire SDMA Lock */
				spin_lock_irqsave(&ppd->sdma_lock, flags);
				/* Invoke sdma make progress */
				qib_sdma_make_progress(ppd);
				/* Release SDMA Lock */
				spin_unlock_irqrestore(&ppd->sdma_lock, flags);
			}
		}
	}

	spin_lock_irqsave(&qp->s_lock, flags);
	if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK))
		goto ack_done;

	/* Ignore invalid responses. */
	if (qib_cmp24(psn, qp->s_next_psn) >= 0)
		goto ack_done;

	/* Ignore duplicate responses. */
	diff = qib_cmp24(psn, qp->s_last_psn);
	if (unlikely(diff <= 0)) {
		/* Update credits for "ghost" ACKs */
		if (diff == 0 && opcode == OP(ACKNOWLEDGE)) {
			aeth = be32_to_cpu(ohdr->u.aeth);
			if ((aeth >> 29) == 0)
				qib_get_credit(qp, aeth);
		}
		goto ack_done;
	}

	/*
	 * Skip everything other than the PSN we expect, if we are waiting
	 * for a reply to a restarted RDMA read or atomic op.
	 */
	if (qp->r_flags & QIB_R_RDMAR_SEQ) {
		if (qib_cmp24(psn, qp->s_last_psn + 1) != 0)
			goto ack_done;
		qp->r_flags &= ~QIB_R_RDMAR_SEQ;
	}

	if (unlikely(qp->s_acked == qp->s_tail))
		goto ack_done;
	wqe = get_swqe_ptr(qp, qp->s_acked);
	status = IB_WC_SUCCESS;

	switch (opcode) {
	case OP(ACKNOWLEDGE):
	case OP(ATOMIC_ACKNOWLEDGE):
	case OP(RDMA_READ_RESPONSE_FIRST):
		aeth = be32_to_cpu(ohdr->u.aeth);
		if (opcode == OP(ATOMIC_ACKNOWLEDGE)) {
			__be32 *p = ohdr->u.at.atomic_ack_eth;

			val = ((u64) be32_to_cpu(p[0]) << 32) |
				be32_to_cpu(p[1]);
		} else
			val = 0;
		if (!do_rc_ack(qp, aeth, psn, opcode, val, rcd) ||
		    opcode != OP(RDMA_READ_RESPONSE_FIRST))
			goto ack_done;
		hdrsize += 4;
		wqe = get_swqe_ptr(qp, qp->s_acked);
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
		/*
		 * If this is a response to a resent RDMA read, we
		 * have to be careful to copy the data to the right
		 * location.
		 */
		qp->s_rdma_read_len = restart_sge(&qp->s_rdma_read_sge,
						  wqe, psn, pmtu);
		goto read_middle;

	case OP(RDMA_READ_RESPONSE_MIDDLE):
		/* no AETH, no ACK */
		if (unlikely(qib_cmp24(psn, qp->s_last_psn + 1)))
			goto ack_seq_err;
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
read_middle:
		if (unlikely(tlen != (hdrsize + pmtu + 4)))
			goto ack_len_err;
		if (unlikely(pmtu >= qp->s_rdma_read_len))
			goto ack_len_err;

		/*
		 * We got a response so update the timeout.
		 * 4.096 usec. * (1 << qp->timeout)
		 */
		qp->s_flags |= QIB_S_TIMER;
		mod_timer(&qp->s_timer, jiffies + qp->timeout_jiffies);
		if (qp->s_flags & QIB_S_WAIT_ACK) {
			qp->s_flags &= ~QIB_S_WAIT_ACK;
			qib_schedule_send(qp);
		}

		if (opcode == OP(RDMA_READ_RESPONSE_MIDDLE))
			qp->s_retry = qp->s_retry_cnt;

		/*
		 * Update the RDMA receive state but do the copy w/o
		 * holding the locks and blocking interrupts.
		 */
		qp->s_rdma_read_len -= pmtu;
		update_last_psn(qp, psn);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		qib_copy_sge(&qp->s_rdma_read_sge, data, pmtu, 0);
		goto bail;

	case OP(RDMA_READ_RESPONSE_ONLY):
		aeth = be32_to_cpu(ohdr->u.aeth);
		if (!do_rc_ack(qp, aeth, psn, opcode, 0, rcd))
			goto ack_done;
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/*
		 * Check that the data size is >= 0 && <= pmtu.
		 * Remember to account for the AETH header (4) and
		 * ICRC (4).
		 */
		if (unlikely(tlen < (hdrsize + pad + 8)))
			goto ack_len_err;
		/*
		 * If this is a response to a resent RDMA read, we
		 * have to be careful to copy the data to the right
		 * location.
		 */
		wqe = get_swqe_ptr(qp, qp->s_acked);
		qp->s_rdma_read_len = restart_sge(&qp->s_rdma_read_sge,
						  wqe, psn, pmtu);
		goto read_last;

	case OP(RDMA_READ_RESPONSE_LAST):
		/* ACKs READ req. */
		if (unlikely(qib_cmp24(psn, qp->s_last_psn + 1)))
			goto ack_seq_err;
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/*
		 * Check that the data size is >= 1 && <= pmtu.
		 * Remember to account for the AETH header (4) and
		 * ICRC (4).
		 */
		if (unlikely(tlen <= (hdrsize + pad + 8)))
			goto ack_len_err;
read_last:
		tlen -= hdrsize + pad + 8;
		if (unlikely(tlen != qp->s_rdma_read_len))
			goto ack_len_err;
		aeth = be32_to_cpu(ohdr->u.aeth);
		qib_copy_sge(&qp->s_rdma_read_sge, data, tlen, 0);
		WARN_ON(qp->s_rdma_read_sge.num_sge);
		(void) do_rc_ack(qp, aeth, psn,
				 OP(RDMA_READ_RESPONSE_LAST), 0, rcd);
		goto ack_done;
	}

ack_op_err:
	status = IB_WC_LOC_QP_OP_ERR;
	goto ack_err;

ack_seq_err:
	rdma_seq_err(qp, ibp, psn, rcd);
	goto ack_done;

ack_len_err:
	status = IB_WC_LOC_LEN_ERR;
ack_err:
	if (qp->s_last == qp->s_acked) {
		qib_send_complete(qp, wqe, status);
		qib_error_qp(qp, IB_WC_WR_FLUSH_ERR);
	}
ack_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
bail:
	return;
}

/**
 * qib_rc_rcv_error - process an incoming duplicate or error RC packet
 * @ohdr: the other headers for this packet
 * @data: the packet data
 * @qp: the QP for this packet
 * @opcode: the opcode for this packet
 * @psn: the packet sequence number for this packet
 * @diff: the difference between the PSN and the expected PSN
 *
 * This is called from qib_rc_rcv() to process an unexpected
 * incoming RC packet for the given QP.
 * Called at interrupt level.
 * Return 1 if no more processing is needed; otherwise return 0 to
 * schedule a response to be sent.
 */
static int qib_rc_rcv_error(struct qib_other_headers *ohdr,
			    void *data,
			    struct qib_qp *qp,
			    u32 opcode,
			    u32 psn,
			    int diff,
			    struct qib_ctxtdata *rcd)
{
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct qib_ack_entry *e;
	unsigned long flags;
	u8 i, prev;
	int old_req;

	if (diff > 0) {
		/*
		 * Packet sequence error.
		 * A NAK will ACK earlier sends and RDMA writes.
		 * Don't queue the NAK if we already sent one.
		 */
		if (!qp->r_nak_state) {
			ibp->n_rc_seqnak++;
			qp->r_nak_state = IB_NAK_PSN_ERROR;
			/* Use the expected PSN. */
			qp->r_ack_psn = qp->r_psn;
			/*
			 * Wait to send the sequence NAK until all packets
			 * in the receive queue have been processed.
			 * Otherwise, we end up propagating congestion.
			 */
			if (list_empty(&qp->rspwait)) {
				qp->r_flags |= QIB_R_RSP_NAK;
				atomic_inc(&qp->refcount);
				list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
			}
		}
		goto done;
	}

	/*
	 * Handle a duplicate request.  Don't re-execute SEND, RDMA
	 * write or atomic op.  Don't NAK errors, just silently drop
	 * the duplicate request.  Note that r_sge, r_len, and
	 * r_rcv_len may be in use so don't modify them.
	 *
	 * We are supposed to ACK the earliest duplicate PSN but we
	 * can coalesce an outstanding duplicate ACK.  We have to
	 * send the earliest so that RDMA reads can be restarted at
	 * the requester's expected PSN.
	 *
	 * First, find where this duplicate PSN falls within the
	 * ACKs previously sent.
	 * old_req is true if there is an older response that is scheduled
	 * to be sent before sending this one.
	 */
	e = NULL;
	old_req = 1;
	ibp->n_rc_dupreq++;

	spin_lock_irqsave(&qp->s_lock, flags);

	for (i = qp->r_head_ack_queue; ; i = prev) {
		if (i == qp->s_tail_ack_queue)
			old_req = 0;
		if (i)
			prev = i - 1;
		else
			prev = QIB_MAX_RDMA_ATOMIC;
		if (prev == qp->r_head_ack_queue) {
			e = NULL;
			break;
		}
		e = &qp->s_ack_queue[prev];
		if (!e->opcode) {
			e = NULL;
			break;
		}
		if (qib_cmp24(psn, e->psn) >= 0) {
			if (prev == qp->s_tail_ack_queue &&
			    qib_cmp24(psn, e->lpsn) <= 0)
				old_req = 0;
			break;
		}
	}
	switch (opcode) {
	case OP(RDMA_READ_REQUEST): {
		struct ib_reth *reth;
		u32 offset;
		u32 len;

		/*
		 * If we didn't find the RDMA read request in the ack queue,
		 * we can ignore this request.
		 */
		if (!e || e->opcode != OP(RDMA_READ_REQUEST))
			goto unlock_done;
		/* RETH comes after BTH */
		reth = &ohdr->u.rc.reth;
		/*
		 * Address range must be a subset of the original
		 * request and start on pmtu boundaries.
		 * We reuse the old ack_queue slot since the requester
		 * should not back up and request an earlier PSN for the
		 * same request.
		 */
		offset = ((psn - e->psn) & QIB_PSN_MASK) *
			qp->pmtu;
		len = be32_to_cpu(reth->length);
		if (unlikely(offset + len != e->rdma_sge.sge_length))
			goto unlock_done;
		if (e->rdma_sge.mr) {
			atomic_dec(&e->rdma_sge.mr->refcount);
			e->rdma_sge.mr = NULL;
		}
		if (len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			ok = qib_rkey_ok(qp, &e->rdma_sge, len, vaddr, rkey,
					 IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok))
				goto unlock_done;
		} else {
			e->rdma_sge.vaddr = NULL;
			e->rdma_sge.length = 0;
			e->rdma_sge.sge_length = 0;
		}
		e->psn = psn;
		if (old_req)
			goto unlock_done;
		qp->s_tail_ack_queue = prev;
		break;
	}

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD): {
		/*
		 * If we didn't find the atomic request in the ack queue
		 * or the send tasklet is already backed up to send an
		 * earlier entry, we can ignore this request.
		 */
		if (!e || e->opcode != (u8) opcode || old_req)
			goto unlock_done;
		qp->s_tail_ack_queue = prev;
		break;
	}

	default:
		/*
		 * Ignore this operation if it doesn't request an ACK
		 * or an earlier RDMA read or atomic is going to be resent.
		 */
		if (!(psn & IB_BTH_REQ_ACK) || old_req)
			goto unlock_done;
		/*
		 * Resend the most recent ACK if this request is
		 * after all the previous RDMA reads and atomics.
		 */
		if (i == qp->r_head_ack_queue) {
			spin_unlock_irqrestore(&qp->s_lock, flags);
			qp->r_nak_state = 0;
			qp->r_ack_psn = qp->r_psn - 1;
			goto send_ack;
		}
		/*
		 * Try to send a simple ACK to work around a Mellanox bug
		 * which doesn't accept a RDMA read response or atomic
		 * response as an ACK for earlier SENDs or RDMA writes.
		 */
		if (!(qp->s_flags & QIB_S_RESP_PENDING)) {
			spin_unlock_irqrestore(&qp->s_lock, flags);
			qp->r_nak_state = 0;
			qp->r_ack_psn = qp->s_ack_queue[i].psn - 1;
			goto send_ack;
		}
		/*
		 * Resend the RDMA read or atomic op which
		 * ACKs this duplicate request.
		 */
		qp->s_tail_ack_queue = i;
		break;
	}
	qp->s_ack_state = OP(ACKNOWLEDGE);
	qp->s_flags |= QIB_S_RESP_PENDING;
	qp->r_nak_state = 0;
	qib_schedule_send(qp);

unlock_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
done:
	return 1;

send_ack:
	return 0;
}

void qib_rc_error(struct qib_qp *qp, enum ib_wc_status err)
{
	unsigned long flags;
	int lastwqe;

	spin_lock_irqsave(&qp->s_lock, flags);
	lastwqe = qib_error_qp(qp, err);
	spin_unlock_irqrestore(&qp->s_lock, flags);

	if (lastwqe) {
		struct ib_event ev;

		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
}

static inline void qib_update_ack_queue(struct qib_qp *qp, unsigned n)
{
	unsigned next;

	next = n + 1;
	if (next > QIB_MAX_RDMA_ATOMIC)
		next = 0;
	qp->s_tail_ack_queue = next;
	qp->s_ack_state = OP(ACKNOWLEDGE);
}

/**
 * qib_rc_rcv - process an incoming RC packet
 * @rcd: the context pointer
 * @hdr: the header of this packet
 * @has_grh: true if the header has a GRH
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP for this packet
 *
 * This is called from qib_qp_rcv() to process an incoming RC packet
 * for the given QP.
 * Called at interrupt level.
 */
void qib_rc_rcv(struct qib_ctxtdata *rcd, struct qib_ib_header *hdr,
		int has_grh, void *data, u32 tlen, struct qib_qp *qp)
{
	struct qib_ibport *ibp = &rcd->ppd->ibport_data;
	struct qib_other_headers *ohdr;
	u32 opcode;
	u32 hdrsize;
	u32 psn;
	u32 pad;
	struct ib_wc wc;
	u32 pmtu = qp->pmtu;
	int diff;
	struct ib_reth *reth;
	unsigned long flags;
	int ret;

	/* Check for GRH */
	if (!has_grh) {
		ohdr = &hdr->u.oth;
		hdrsize = 8 + 12;       /* LRH + BTH */
	} else {
		ohdr = &hdr->u.l.oth;
		hdrsize = 8 + 40 + 12;  /* LRH + GRH + BTH */
	}

	opcode = be32_to_cpu(ohdr->bth[0]);
	if (qib_ruc_check_hdr(ibp, hdr, has_grh, qp, opcode))
		return;

	psn = be32_to_cpu(ohdr->bth[2]);
	opcode >>= 24;

	/*
	 * Process responses (ACKs) before anything else.  Note that the
	 * packet sequence number will be for something in the send work
	 * queue rather than the expected receive packet sequence number.
	 * In other words, this QP is the requester.
	 */
	if (opcode >= OP(RDMA_READ_RESPONSE_FIRST) &&
	    opcode <= OP(ATOMIC_ACKNOWLEDGE)) {
		qib_rc_rcv_resp(ibp, ohdr, data, tlen, qp, opcode, psn,
				hdrsize, pmtu, rcd);
		return;
	}

	/* Compute 24 bits worth of difference. */
	diff = qib_cmp24(psn, qp->r_psn);
	if (unlikely(diff)) {
		if (qib_rc_rcv_error(ohdr, data, qp, opcode, psn, diff, rcd))
			return;
		goto send_ack;
	}

	/* Check for opcode sequence errors. */
	switch (qp->r_state) {
	case OP(SEND_FIRST):
	case OP(SEND_MIDDLE):
		if (opcode == OP(SEND_MIDDLE) ||
		    opcode == OP(SEND_LAST) ||
		    opcode == OP(SEND_LAST_WITH_IMMEDIATE))
			break;
		goto nack_inv;

	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_MIDDLE):
		if (opcode == OP(RDMA_WRITE_MIDDLE) ||
		    opcode == OP(RDMA_WRITE_LAST) ||
		    opcode == OP(RDMA_WRITE_LAST_WITH_IMMEDIATE))
			break;
		goto nack_inv;

	default:
		if (opcode == OP(SEND_MIDDLE) ||
		    opcode == OP(SEND_LAST) ||
		    opcode == OP(SEND_LAST_WITH_IMMEDIATE) ||
		    opcode == OP(RDMA_WRITE_MIDDLE) ||
		    opcode == OP(RDMA_WRITE_LAST) ||
		    opcode == OP(RDMA_WRITE_LAST_WITH_IMMEDIATE))
			goto nack_inv;
		/*
		 * Note that it is up to the requester to not send a new
		 * RDMA read or atomic operation before receiving an ACK
		 * for the previous operation.
		 */
		break;
	}

	if (qp->state == IB_QPS_RTR && !(qp->r_flags & QIB_R_COMM_EST)) {
		qp->r_flags |= QIB_R_COMM_EST;
		if (qp->ibqp.event_handler) {
			struct ib_event ev;

			ev.device = qp->ibqp.device;
			ev.element.qp = &qp->ibqp;
			ev.event = IB_EVENT_COMM_EST;
			qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
		}
	}

	/* OK, process the packet. */
	switch (opcode) {
	case OP(SEND_FIRST):
		ret = qib_get_rwqe(qp, 0);
		if (ret < 0)
			goto nack_op_err;
		if (!ret)
			goto rnr_nak;
		qp->r_rcv_len = 0;
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
	case OP(RDMA_WRITE_MIDDLE):
send_middle:
		/* Check for invalid length PMTU or posted rwqe len. */
		if (unlikely(tlen != (hdrsize + pmtu + 4)))
			goto nack_inv;
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len))
			goto nack_inv;
		qib_copy_sge(&qp->r_sge, data, pmtu, 1);
		break;

	case OP(RDMA_WRITE_LAST_WITH_IMMEDIATE):
		/* consume RWQE */
		ret = qib_get_rwqe(qp, 1);
		if (ret < 0)
			goto nack_op_err;
		if (!ret)
			goto rnr_nak;
		goto send_last_imm;

	case OP(SEND_ONLY):
	case OP(SEND_ONLY_WITH_IMMEDIATE):
		ret = qib_get_rwqe(qp, 0);
		if (ret < 0)
			goto nack_op_err;
		if (!ret)
			goto rnr_nak;
		qp->r_rcv_len = 0;
		if (opcode == OP(SEND_ONLY))
			goto no_immediate_data;
		/* FALLTHROUGH for SEND_ONLY_WITH_IMMEDIATE */
	case OP(SEND_LAST_WITH_IMMEDIATE):
send_last_imm:
		wc.ex.imm_data = ohdr->u.imm_data;
		hdrsize += 4;
		wc.wc_flags = IB_WC_WITH_IMM;
		goto send_last;
	case OP(SEND_LAST):
	case OP(RDMA_WRITE_LAST):
no_immediate_data:
		wc.wc_flags = 0;
		wc.ex.imm_data = 0;
send_last:
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* XXX LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4)))
			goto nack_inv;
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		wc.byte_len = tlen + qp->r_rcv_len;
		if (unlikely(wc.byte_len > qp->r_len))
			goto nack_inv;
		qib_copy_sge(&qp->r_sge, data, tlen, 1);
		while (qp->r_sge.num_sge) {
			atomic_dec(&qp->r_sge.sge.mr->refcount);
			if (--qp->r_sge.num_sge)
				qp->r_sge.sge = *qp->r_sge.sg_list++;
		}
		qp->r_msn++;
		if (!test_and_clear_bit(QIB_R_WRID_VALID, &qp->r_aflags))
			break;
		wc.wr_id = qp->r_wr_id;
		wc.status = IB_WC_SUCCESS;
		if (opcode == OP(RDMA_WRITE_LAST_WITH_IMMEDIATE) ||
		    opcode == OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE))
			wc.opcode = IB_WC_RECV_RDMA_WITH_IMM;
		else
			wc.opcode = IB_WC_RECV;
		wc.qp = &qp->ibqp;
		wc.src_qp = qp->remote_qpn;
		wc.slid = qp->remote_ah_attr.dlid;
		wc.sl = qp->remote_ah_attr.sl;
		/* zero fields that are N/A */
		wc.vendor_err = 0;
		wc.pkey_index = 0;
		wc.dlid_path_bits = 0;
		wc.port_num = 0;
		/* Signal completion event if the solicited bit is set. */
		qib_cq_enter(to_icq(qp->ibqp.recv_cq), &wc,
			     (ohdr->bth[0] &
			      cpu_to_be32(IB_BTH_SOLICITED)) != 0);
		break;

	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_ONLY):
	case OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE):
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
			goto nack_inv;
		/* consume RWQE */
		reth = &ohdr->u.rc.reth;
		hdrsize += sizeof(*reth);
		qp->r_len = be32_to_cpu(reth->length);
		qp->r_rcv_len = 0;
		qp->r_sge.sg_list = NULL;
		if (qp->r_len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/* Check rkey & NAK */
			ok = qib_rkey_ok(qp, &qp->r_sge.sge, qp->r_len, vaddr,
					 rkey, IB_ACCESS_REMOTE_WRITE);
			if (unlikely(!ok))
				goto nack_acc;
			qp->r_sge.num_sge = 1;
		} else {
			qp->r_sge.num_sge = 0;
			qp->r_sge.sge.mr = NULL;
			qp->r_sge.sge.vaddr = NULL;
			qp->r_sge.sge.length = 0;
			qp->r_sge.sge.sge_length = 0;
		}
		if (opcode == OP(RDMA_WRITE_FIRST))
			goto send_middle;
		else if (opcode == OP(RDMA_WRITE_ONLY))
			goto no_immediate_data;
		ret = qib_get_rwqe(qp, 1);
		if (ret < 0)
			goto nack_op_err;
		if (!ret)
			goto rnr_nak;
		wc.ex.imm_data = ohdr->u.rc.imm_data;
		hdrsize += 4;
		wc.wc_flags = IB_WC_WITH_IMM;
		goto send_last;

	case OP(RDMA_READ_REQUEST): {
		struct qib_ack_entry *e;
		u32 len;
		u8 next;

		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_READ)))
			goto nack_inv;
		next = qp->r_head_ack_queue + 1;
		/* s_ack_queue is size QIB_MAX_RDMA_ATOMIC+1 so use > not >= */
		if (next > QIB_MAX_RDMA_ATOMIC)
			next = 0;
		spin_lock_irqsave(&qp->s_lock, flags);
		if (unlikely(next == qp->s_tail_ack_queue)) {
			if (!qp->s_ack_queue[next].sent)
				goto nack_inv_unlck;
			qib_update_ack_queue(qp, next);
		}
		e = &qp->s_ack_queue[qp->r_head_ack_queue];
		if (e->opcode == OP(RDMA_READ_REQUEST) && e->rdma_sge.mr) {
			atomic_dec(&e->rdma_sge.mr->refcount);
			e->rdma_sge.mr = NULL;
		}
		reth = &ohdr->u.rc.reth;
		len = be32_to_cpu(reth->length);
		if (len) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/* Check rkey & NAK */
			ok = qib_rkey_ok(qp, &e->rdma_sge, len, vaddr,
					 rkey, IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok))
				goto nack_acc_unlck;
			/*
			 * Update the next expected PSN.  We add 1 later
			 * below, so only add the remainder here.
			 */
			if (len > pmtu)
				qp->r_psn += (len - 1) / pmtu;
		} else {
			e->rdma_sge.mr = NULL;
			e->rdma_sge.vaddr = NULL;
			e->rdma_sge.length = 0;
			e->rdma_sge.sge_length = 0;
		}
		e->opcode = opcode;
		e->sent = 0;
		e->psn = psn;
		e->lpsn = qp->r_psn;
		/*
		 * We need to increment the MSN here instead of when we
		 * finish sending the result since a duplicate request would
		 * increment it more than once.
		 */
		qp->r_msn++;
		qp->r_psn++;
		qp->r_state = opcode;
		qp->r_nak_state = 0;
		qp->r_head_ack_queue = next;

		/* Schedule the send tasklet. */
		qp->s_flags |= QIB_S_RESP_PENDING;
		qib_schedule_send(qp);

		goto sunlock;
	}

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD): {
		struct ib_atomic_eth *ateth;
		struct qib_ack_entry *e;
		u64 vaddr;
		atomic64_t *maddr;
		u64 sdata;
		u32 rkey;
		u8 next;

		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_inv;
		next = qp->r_head_ack_queue + 1;
		if (next > QIB_MAX_RDMA_ATOMIC)
			next = 0;
		spin_lock_irqsave(&qp->s_lock, flags);
		if (unlikely(next == qp->s_tail_ack_queue)) {
			if (!qp->s_ack_queue[next].sent)
				goto nack_inv_unlck;
			qib_update_ack_queue(qp, next);
		}
		e = &qp->s_ack_queue[qp->r_head_ack_queue];
		if (e->opcode == OP(RDMA_READ_REQUEST) && e->rdma_sge.mr) {
			atomic_dec(&e->rdma_sge.mr->refcount);
			e->rdma_sge.mr = NULL;
		}
		ateth = &ohdr->u.atomic_eth;
		vaddr = ((u64) be32_to_cpu(ateth->vaddr[0]) << 32) |
			be32_to_cpu(ateth->vaddr[1]);
		if (unlikely(vaddr & (sizeof(u64) - 1)))
			goto nack_inv_unlck;
		rkey = be32_to_cpu(ateth->rkey);
		/* Check rkey & NAK */
		if (unlikely(!qib_rkey_ok(qp, &qp->r_sge.sge, sizeof(u64),
					  vaddr, rkey,
					  IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_acc_unlck;
		/* Perform atomic OP and save result. */
		maddr = (atomic64_t *) qp->r_sge.sge.vaddr;
		sdata = be64_to_cpu(ateth->swap_data);
		e->atomic_data = (opcode == OP(FETCH_ADD)) ?
			(u64) atomic64_add_return(sdata, maddr) - sdata :
			(u64) cmpxchg((u64 *) qp->r_sge.sge.vaddr,
				      be64_to_cpu(ateth->compare_data),
				      sdata);
		atomic_dec(&qp->r_sge.sge.mr->refcount);
		qp->r_sge.num_sge = 0;
		e->opcode = opcode;
		e->sent = 0;
		e->psn = psn;
		e->lpsn = psn;
		qp->r_msn++;
		qp->r_psn++;
		qp->r_state = opcode;
		qp->r_nak_state = 0;
		qp->r_head_ack_queue = next;

		/* Schedule the send tasklet. */
		qp->s_flags |= QIB_S_RESP_PENDING;
		qib_schedule_send(qp);

		goto sunlock;
	}

	default:
		/* NAK unknown opcodes. */
		goto nack_inv;
	}
	qp->r_psn++;
	qp->r_state = opcode;
	qp->r_ack_psn = psn;
	qp->r_nak_state = 0;
	/* Send an ACK if requested or required. */
	if (psn & (1 << 31))
		goto send_ack;
	return;

rnr_nak:
	qp->r_nak_state = IB_RNR_NAK | qp->r_min_rnr_timer;
	qp->r_ack_psn = qp->r_psn;
	/* Queue RNR NAK for later */
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= QIB_R_RSP_NAK;
		atomic_inc(&qp->refcount);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
	return;

nack_op_err:
	qib_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
	qp->r_nak_state = IB_NAK_REMOTE_OPERATIONAL_ERROR;
	qp->r_ack_psn = qp->r_psn;
	/* Queue NAK for later */
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= QIB_R_RSP_NAK;
		atomic_inc(&qp->refcount);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
	return;

nack_inv_unlck:
	spin_unlock_irqrestore(&qp->s_lock, flags);
nack_inv:
	qib_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
	qp->r_nak_state = IB_NAK_INVALID_REQUEST;
	qp->r_ack_psn = qp->r_psn;
	/* Queue NAK for later */
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= QIB_R_RSP_NAK;
		atomic_inc(&qp->refcount);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
	return;

nack_acc_unlck:
	spin_unlock_irqrestore(&qp->s_lock, flags);
nack_acc:
	qib_rc_error(qp, IB_WC_LOC_PROT_ERR);
	qp->r_nak_state = IB_NAK_REMOTE_ACCESS_ERROR;
	qp->r_ack_psn = qp->r_psn;
send_ack:
	qib_send_rc_ack(qp);
	return;

sunlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
}
