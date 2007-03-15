/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
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
#include "ipath_kernel.h"

/* cut down ridiculously long IB macro names */
#define OP(x) IB_OPCODE_RC_##x

static u32 restart_sge(struct ipath_sge_state *ss, struct ipath_swqe *wqe,
		       u32 psn, u32 pmtu)
{
	u32 len;

	len = ((psn - wqe->psn) & IPATH_PSN_MASK) * pmtu;
	ss->sge = wqe->sg_list[0];
	ss->sg_list = wqe->sg_list + 1;
	ss->num_sge = wqe->wr.num_sge;
	ipath_skip_sge(ss, len);
	return wqe->length - len;
}

/**
 * ipath_init_restart- initialize the qp->s_sge after a restart
 * @qp: the QP who's SGE we're restarting
 * @wqe: the work queue to initialize the QP's SGE from
 *
 * The QP s_lock should be held and interrupts disabled.
 */
static void ipath_init_restart(struct ipath_qp *qp, struct ipath_swqe *wqe)
{
	struct ipath_ibdev *dev;

	qp->s_len = restart_sge(&qp->s_sge, wqe, qp->s_psn,
				ib_mtu_enum_to_int(qp->path_mtu));
	dev = to_idev(qp->ibqp.device);
	spin_lock(&dev->pending_lock);
	if (list_empty(&qp->timerwait))
		list_add_tail(&qp->timerwait,
			      &dev->pending[dev->pending_index]);
	spin_unlock(&dev->pending_lock);
}

/**
 * ipath_make_rc_ack - construct a response packet (ACK, NAK, or RDMA read)
 * @qp: a pointer to the QP
 * @ohdr: a pointer to the IB header being constructed
 * @pmtu: the path MTU
 *
 * Return 1 if constructed; otherwise, return 0.
 * Note that we are in the responder's side of the QP context.
 * Note the QP s_lock must be held.
 */
static int ipath_make_rc_ack(struct ipath_qp *qp,
			     struct ipath_other_headers *ohdr,
			     u32 pmtu, u32 *bth0p, u32 *bth2p)
{
	struct ipath_ack_entry *e;
	u32 hwords;
	u32 len;
	u32 bth0;
	u32 bth2;

	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;

	switch (qp->s_ack_state) {
	case OP(RDMA_READ_RESPONSE_LAST):
	case OP(RDMA_READ_RESPONSE_ONLY):
	case OP(ATOMIC_ACKNOWLEDGE):
		qp->s_ack_state = OP(ACKNOWLEDGE);
		/* FALLTHROUGH */
	case OP(ACKNOWLEDGE):
		/* Check for no next entry in the queue. */
		if (qp->r_head_ack_queue == qp->s_tail_ack_queue) {
			if (qp->s_flags & IPATH_S_ACK_PENDING)
				goto normal;
			goto bail;
		}

		e = &qp->s_ack_queue[qp->s_tail_ack_queue];
		if (e->opcode == OP(RDMA_READ_REQUEST)) {
			/* Copy SGE state in case we need to resend */
			qp->s_ack_rdma_sge = e->rdma_sge;
			qp->s_cur_sge = &qp->s_ack_rdma_sge;
			len = e->rdma_sge.sge.sge_length;
			if (len > pmtu) {
				len = pmtu;
				qp->s_ack_state = OP(RDMA_READ_RESPONSE_FIRST);
			} else {
				qp->s_ack_state = OP(RDMA_READ_RESPONSE_ONLY);
				if (++qp->s_tail_ack_queue >
				    IPATH_MAX_RDMA_ATOMIC)
					qp->s_tail_ack_queue = 0;
			}
			ohdr->u.aeth = ipath_compute_aeth(qp);
			hwords++;
			qp->s_ack_rdma_psn = e->psn;
			bth2 = qp->s_ack_rdma_psn++ & IPATH_PSN_MASK;
		} else {
			/* COMPARE_SWAP or FETCH_ADD */
			qp->s_cur_sge = NULL;
			len = 0;
			qp->s_ack_state = OP(ATOMIC_ACKNOWLEDGE);
			ohdr->u.at.aeth = ipath_compute_aeth(qp);
			ohdr->u.at.atomic_ack_eth[0] =
				cpu_to_be32(e->atomic_data >> 32);
			ohdr->u.at.atomic_ack_eth[1] =
				cpu_to_be32(e->atomic_data);
			hwords += sizeof(ohdr->u.at) / sizeof(u32);
			bth2 = e->psn;
			if (++qp->s_tail_ack_queue > IPATH_MAX_RDMA_ATOMIC)
				qp->s_tail_ack_queue = 0;
		}
		bth0 = qp->s_ack_state << 24;
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		qp->s_ack_state = OP(RDMA_READ_RESPONSE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_READ_RESPONSE_MIDDLE):
		len = qp->s_ack_rdma_sge.sge.sge_length;
		if (len > pmtu)
			len = pmtu;
		else {
			ohdr->u.aeth = ipath_compute_aeth(qp);
			hwords++;
			qp->s_ack_state = OP(RDMA_READ_RESPONSE_LAST);
			if (++qp->s_tail_ack_queue > IPATH_MAX_RDMA_ATOMIC)
				qp->s_tail_ack_queue = 0;
		}
		bth0 = qp->s_ack_state << 24;
		bth2 = qp->s_ack_rdma_psn++ & IPATH_PSN_MASK;
		break;

	default:
	normal:
		/*
		 * Send a regular ACK.
		 * Set the s_ack_state so we wait until after sending
		 * the ACK before setting s_ack_state to ACKNOWLEDGE
		 * (see above).
		 */
		qp->s_ack_state = OP(ATOMIC_ACKNOWLEDGE);
		qp->s_flags &= ~IPATH_S_ACK_PENDING;
		qp->s_cur_sge = NULL;
		if (qp->s_nak_state)
			ohdr->u.aeth =
				cpu_to_be32((qp->r_msn & IPATH_MSN_MASK) |
					    (qp->s_nak_state <<
					     IPATH_AETH_CREDIT_SHIFT));
		else
			ohdr->u.aeth = ipath_compute_aeth(qp);
		hwords++;
		len = 0;
		bth0 = OP(ACKNOWLEDGE) << 24;
		bth2 = qp->s_ack_psn & IPATH_PSN_MASK;
	}
	qp->s_hdrwords = hwords;
	qp->s_cur_size = len;
	*bth0p = bth0;
	*bth2p = bth2;
	return 1;

bail:
	return 0;
}

/**
 * ipath_make_rc_req - construct a request packet (SEND, RDMA r/w, ATOMIC)
 * @qp: a pointer to the QP
 * @ohdr: a pointer to the IB header being constructed
 * @pmtu: the path MTU
 * @bth0p: pointer to the BTH opcode word
 * @bth2p: pointer to the BTH PSN word
 *
 * Return 1 if constructed; otherwise, return 0.
 * Note the QP s_lock must be held and interrupts disabled.
 */
int ipath_make_rc_req(struct ipath_qp *qp,
		      struct ipath_other_headers *ohdr,
		      u32 pmtu, u32 *bth0p, u32 *bth2p)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	struct ipath_sge_state *ss;
	struct ipath_swqe *wqe;
	u32 hwords;
	u32 len;
	u32 bth0;
	u32 bth2;
	char newreq;

	/* Sending responses has higher priority over sending requests. */
	if ((qp->r_head_ack_queue != qp->s_tail_ack_queue ||
	     (qp->s_flags & IPATH_S_ACK_PENDING) ||
	     qp->s_ack_state != IB_OPCODE_RC_ACKNOWLEDGE) &&
	    ipath_make_rc_ack(qp, ohdr, pmtu, bth0p, bth2p))
		goto done;

	if (!(ib_ipath_state_ops[qp->state] & IPATH_PROCESS_SEND_OK) ||
	    qp->s_rnr_timeout)
		goto bail;

	/* Limit the number of packets sent without an ACK. */
	if (ipath_cmp24(qp->s_psn, qp->s_last_psn + IPATH_PSN_CREDIT) > 0) {
		qp->s_wait_credit = 1;
		dev->n_rc_stalls++;
		spin_lock(&dev->pending_lock);
		if (list_empty(&qp->timerwait))
			list_add_tail(&qp->timerwait,
				      &dev->pending[dev->pending_index]);
		spin_unlock(&dev->pending_lock);
		goto bail;
	}

	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;
	bth0 = 0;

	/* Send a request. */
	wqe = get_swqe_ptr(qp, qp->s_cur);
	switch (qp->s_state) {
	default:
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
				qp->s_flags |= IPATH_S_FENCE_PENDING;
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
		bth2 = 0;
		switch (wqe->wr.opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			/* If no credit, return. */
			if (qp->s_lsn != (u32) -1 &&
			    ipath_cmp24(wqe->ssn, qp->s_lsn + 1) > 0)
				goto bail;
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
				ohdr->u.imm_data = wqe->wr.imm_data;
				hwords += 1;
			}
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= 1 << 23;
			bth2 = 1 << 31;	/* Request ACK. */
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_RDMA_WRITE:
			if (newreq && qp->s_lsn != (u32) -1)
				qp->s_lsn++;
			/* FALLTHROUGH */
		case IB_WR_RDMA_WRITE_WITH_IMM:
			/* If no credit, return. */
			if (qp->s_lsn != (u32) -1 &&
			    ipath_cmp24(wqe->ssn, qp->s_lsn + 1) > 0)
				goto bail;
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
				ohdr->u.rc.imm_data = wqe->wr.imm_data;
				hwords += 1;
				if (wqe->wr.send_flags & IB_SEND_SOLICITED)
					bth0 |= 1 << 23;
			}
			bth2 = 1 << 31;	/* Request ACK. */
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
					qp->s_flags |= IPATH_S_RDMAR_PENDING;
					goto bail;
				}
				qp->s_num_rd_atomic++;
				if (qp->s_lsn != (u32) -1)
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
					qp->s_flags |= IPATH_S_RDMAR_PENDING;
					goto bail;
				}
				qp->s_num_rd_atomic++;
				if (qp->s_lsn != (u32) -1)
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
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		default:
			goto bail;
		}
		qp->s_sge.sge = wqe->sg_list[0];
		qp->s_sge.sg_list = wqe->sg_list + 1;
		qp->s_sge.num_sge = wqe->wr.num_sge;
		qp->s_len = wqe->length;
		if (newreq) {
			qp->s_tail++;
			if (qp->s_tail >= qp->s_size)
				qp->s_tail = 0;
		}
		bth2 |= qp->s_psn & IPATH_PSN_MASK;
		if (wqe->wr.opcode == IB_WR_RDMA_READ)
			qp->s_psn = wqe->lpsn + 1;
		else {
			qp->s_psn++;
			if (ipath_cmp24(qp->s_psn, qp->s_next_psn) > 0)
				qp->s_next_psn = qp->s_psn;
		}
		/*
		 * Put the QP on the pending list so lost ACKs will cause
		 * a retry.  More than one request can be pending so the
		 * QP may already be on the dev->pending list.
		 */
		spin_lock(&dev->pending_lock);
		if (list_empty(&qp->timerwait))
			list_add_tail(&qp->timerwait,
				      &dev->pending[dev->pending_index]);
		spin_unlock(&dev->pending_lock);
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		/*
		 * This case can only happen if a send is restarted.
		 * See ipath_restart_rc().
		 */
		ipath_init_restart(qp, wqe);
		/* FALLTHROUGH */
	case OP(SEND_FIRST):
		qp->s_state = OP(SEND_MIDDLE);
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		bth2 = qp->s_psn++ & IPATH_PSN_MASK;
		if (ipath_cmp24(qp->s_psn, qp->s_next_psn) > 0)
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
			ohdr->u.imm_data = wqe->wr.imm_data;
			hwords += 1;
		}
		if (wqe->wr.send_flags & IB_SEND_SOLICITED)
			bth0 |= 1 << 23;
		bth2 |= 1 << 31;	/* Request ACK. */
		qp->s_cur++;
		if (qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		break;

	case OP(RDMA_READ_RESPONSE_LAST):
		/*
		 * This case can only happen if a RDMA write is restarted.
		 * See ipath_restart_rc().
		 */
		ipath_init_restart(qp, wqe);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_FIRST):
		qp->s_state = OP(RDMA_WRITE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		bth2 = qp->s_psn++ & IPATH_PSN_MASK;
		if (ipath_cmp24(qp->s_psn, qp->s_next_psn) > 0)
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
			ohdr->u.imm_data = wqe->wr.imm_data;
			hwords += 1;
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= 1 << 23;
		}
		bth2 |= 1 << 31;	/* Request ACK. */
		qp->s_cur++;
		if (qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		break;

	case OP(RDMA_READ_RESPONSE_MIDDLE):
		/*
		 * This case can only happen if a RDMA read is restarted.
		 * See ipath_restart_rc().
		 */
		ipath_init_restart(qp, wqe);
		len = ((qp->s_psn - wqe->psn) & IPATH_PSN_MASK) * pmtu;
		ohdr->u.rc.reth.vaddr =
			cpu_to_be64(wqe->wr.wr.rdma.remote_addr + len);
		ohdr->u.rc.reth.rkey =
			cpu_to_be32(wqe->wr.wr.rdma.rkey);
		ohdr->u.rc.reth.length = cpu_to_be32(qp->s_len);
		qp->s_state = OP(RDMA_READ_REQUEST);
		hwords += sizeof(ohdr->u.rc.reth) / sizeof(u32);
		bth2 = qp->s_psn++ & IPATH_PSN_MASK;
		if (ipath_cmp24(qp->s_psn, qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		ss = NULL;
		len = 0;
		qp->s_cur++;
		if (qp->s_cur == qp->s_size)
			qp->s_cur = 0;
		break;
	}
	if (ipath_cmp24(qp->s_psn, qp->s_last_psn + IPATH_PSN_CREDIT - 1) >= 0)
		bth2 |= 1 << 31;	/* Request ACK. */
	qp->s_len -= len;
	qp->s_hdrwords = hwords;
	qp->s_cur_sge = ss;
	qp->s_cur_size = len;
	*bth0p = bth0 | (qp->s_state << 24);
	*bth2p = bth2;
done:
	return 1;

bail:
	return 0;
}

/**
 * send_rc_ack - Construct an ACK packet and send it
 * @qp: a pointer to the QP
 *
 * This is called from ipath_rc_rcv() and only uses the receive
 * side QP state.
 * Note that RDMA reads and atomics are handled in the
 * send side QP state and tasklet.
 */
static void send_rc_ack(struct ipath_qp *qp)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	u16 lrh0;
	u32 bth0;
	u32 hwords;
	struct ipath_ib_header hdr;
	struct ipath_other_headers *ohdr;

	/* Don't send ACK or NAK if a RDMA read or atomic is pending. */
	if (qp->r_head_ack_queue != qp->s_tail_ack_queue)
		goto queue_ack;

	/* Construct the header. */
	ohdr = &hdr.u.oth;
	lrh0 = IPATH_LRH_BTH;
	/* header size in 32-bit words LRH+BTH+AETH = (8+12+4)/4. */
	hwords = 6;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		hwords += ipath_make_grh(dev, &hdr.u.l.grh,
					 &qp->remote_ah_attr.grh,
					 hwords, 0);
		ohdr = &hdr.u.l.oth;
		lrh0 = IPATH_LRH_GRH;
	}
	/* read pkey_index w/o lock (its atomic) */
	bth0 = ipath_get_pkey(dev->dd, qp->s_pkey_index) |
		OP(ACKNOWLEDGE) << 24;
	if (qp->r_nak_state)
		ohdr->u.aeth = cpu_to_be32((qp->r_msn & IPATH_MSN_MASK) |
					    (qp->r_nak_state <<
					     IPATH_AETH_CREDIT_SHIFT));
	else
		ohdr->u.aeth = ipath_compute_aeth(qp);
	lrh0 |= qp->remote_ah_attr.sl << 4;
	hdr.lrh[0] = cpu_to_be16(lrh0);
	hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	hdr.lrh[2] = cpu_to_be16(hwords + SIZE_OF_CRC);
	hdr.lrh[3] = cpu_to_be16(dev->dd->ipath_lid);
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(qp->r_ack_psn & IPATH_PSN_MASK);

	/*
	 * If we can send the ACK, clear the ACK state.
	 */
	if (ipath_verbs_send(dev->dd, hwords, (u32 *) &hdr, 0, NULL) == 0) {
		dev->n_unicast_xmit++;
		goto done;
	}

	/*
	 * We are out of PIO buffers at the moment.
	 * Pass responsibility for sending the ACK to the
	 * send tasklet so that when a PIO buffer becomes
	 * available, the ACK is sent ahead of other outgoing
	 * packets.
	 */
	dev->n_rc_qacks++;

queue_ack:
	spin_lock_irq(&qp->s_lock);
	qp->s_flags |= IPATH_S_ACK_PENDING;
	qp->s_nak_state = qp->r_nak_state;
	qp->s_ack_psn = qp->r_ack_psn;
	spin_unlock_irq(&qp->s_lock);

	/* Call ipath_do_rc_send() in another thread. */
	tasklet_hi_schedule(&qp->s_task);

done:
	return;
}

/**
 * reset_psn - reset the QP state to send starting from PSN
 * @qp: the QP
 * @psn: the packet sequence number to restart at
 *
 * This is called from ipath_rc_rcv() to process an incoming RC ACK
 * for the given QP.
 * Called at interrupt level with the QP s_lock held.
 */
static void reset_psn(struct ipath_qp *qp, u32 psn)
{
	u32 n = qp->s_last;
	struct ipath_swqe *wqe = get_swqe_ptr(qp, n);
	u32 opcode;

	qp->s_cur = n;

	/*
	 * If we are starting the request from the beginning,
	 * let the normal send code handle initialization.
	 */
	if (ipath_cmp24(psn, wqe->psn) <= 0) {
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
		diff = ipath_cmp24(psn, wqe->psn);
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
	 * See ipath_do_rc_send().
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
}

/**
 * ipath_restart_rc - back up requester to resend the last un-ACKed request
 * @qp: the QP to restart
 * @psn: packet sequence number for the request
 * @wc: the work completion request
 *
 * The QP s_lock should be held and interrupts disabled.
 */
void ipath_restart_rc(struct ipath_qp *qp, u32 psn, struct ib_wc *wc)
{
	struct ipath_swqe *wqe = get_swqe_ptr(qp, qp->s_last);
	struct ipath_ibdev *dev;

	if (qp->s_retry == 0) {
		wc->wr_id = wqe->wr.wr_id;
		wc->status = IB_WC_RETRY_EXC_ERR;
		wc->opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		wc->vendor_err = 0;
		wc->byte_len = 0;
		wc->qp = &qp->ibqp;
		wc->src_qp = qp->remote_qpn;
		wc->pkey_index = 0;
		wc->slid = qp->remote_ah_attr.dlid;
		wc->sl = qp->remote_ah_attr.sl;
		wc->dlid_path_bits = 0;
		wc->port_num = 0;
		ipath_sqerror_qp(qp, wc);
		goto bail;
	}
	qp->s_retry--;

	/*
	 * Remove the QP from the timeout queue.
	 * Note: it may already have been removed by ipath_ib_timer().
	 */
	dev = to_idev(qp->ibqp.device);
	spin_lock(&dev->pending_lock);
	if (!list_empty(&qp->timerwait))
		list_del_init(&qp->timerwait);
	spin_unlock(&dev->pending_lock);

	if (wqe->wr.opcode == IB_WR_RDMA_READ)
		dev->n_rc_resends++;
	else
		dev->n_rc_resends += (qp->s_psn - psn) & IPATH_PSN_MASK;

	reset_psn(qp, psn);
	tasklet_hi_schedule(&qp->s_task);

bail:
	return;
}

static inline void update_last_psn(struct ipath_qp *qp, u32 psn)
{
	if (qp->s_wait_credit) {
		qp->s_wait_credit = 0;
		tasklet_hi_schedule(&qp->s_task);
	}
	qp->s_last_psn = psn;
}

/**
 * do_rc_ack - process an incoming RC ACK
 * @qp: the QP the ACK came in on
 * @psn: the packet sequence number of the ACK
 * @opcode: the opcode of the request that resulted in the ACK
 *
 * This is called from ipath_rc_rcv_resp() to process an incoming RC ACK
 * for the given QP.
 * Called at interrupt level with the QP s_lock held and interrupts disabled.
 * Returns 1 if OK, 0 if current operation should be aborted (NAK).
 */
static int do_rc_ack(struct ipath_qp *qp, u32 aeth, u32 psn, int opcode)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	struct ib_wc wc;
	struct ipath_swqe *wqe;
	int ret = 0;
	u32 ack_psn;

	/*
	 * Remove the QP from the timeout queue (or RNR timeout queue).
	 * If ipath_ib_timer() has already removed it,
	 * it's OK since we hold the QP s_lock and ipath_restart_rc()
	 * just won't find anything to restart if we ACK everything.
	 */
	spin_lock(&dev->pending_lock);
	if (!list_empty(&qp->timerwait))
		list_del_init(&qp->timerwait);
	spin_unlock(&dev->pending_lock);

	/*
	 * Note that NAKs implicitly ACK outstanding SEND and RDMA write
	 * requests and implicitly NAK RDMA read and atomic requests issued
	 * before the NAK'ed request.  The MSN won't include the NAK'ed
	 * request but will include an ACK'ed request(s).
	 */
	ack_psn = psn;
	if (aeth >> 29)
		ack_psn--;
	wqe = get_swqe_ptr(qp, qp->s_last);

	/*
	 * The MSN might be for a later WQE than the PSN indicates so
	 * only complete WQEs that the PSN finishes.
	 */
	while (ipath_cmp24(ack_psn, wqe->lpsn) >= 0) {
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
		     (opcode != OP(RDMA_READ_RESPONSE_LAST) ||
		      ipath_cmp24(ack_psn, wqe->lpsn) != 0)) ||
		    ((wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		      wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) &&
		     (opcode != OP(ATOMIC_ACKNOWLEDGE) ||
		      ipath_cmp24(wqe->psn, psn) != 0))) {
			/*
			 * The last valid PSN seen is the previous
			 * request's.
			 */
			update_last_psn(qp, wqe->psn - 1);
			/* Retry this request. */
			ipath_restart_rc(qp, wqe->psn, &wc);
			/*
			 * No need to process the ACK/NAK since we are
			 * restarting an earlier request.
			 */
			goto bail;
		}
		if (qp->s_num_rd_atomic &&
		    (wqe->wr.opcode == IB_WR_RDMA_READ ||
		     wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		     wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD)) {
			qp->s_num_rd_atomic--;
			/* Restart sending task if fence is complete */
			if ((qp->s_flags & IPATH_S_FENCE_PENDING) &&
			    !qp->s_num_rd_atomic) {
				qp->s_flags &= ~IPATH_S_FENCE_PENDING;
				tasklet_hi_schedule(&qp->s_task);
			} else if (qp->s_flags & IPATH_S_RDMAR_PENDING) {
				qp->s_flags &= ~IPATH_S_RDMAR_PENDING;
				tasklet_hi_schedule(&qp->s_task);
			}
		}
		/* Post a send completion queue entry if requested. */
		if (!(qp->s_flags & IPATH_S_SIGNAL_REQ_WR) ||
		    (wqe->wr.send_flags & IB_SEND_SIGNALED)) {
			wc.wr_id = wqe->wr.wr_id;
			wc.status = IB_WC_SUCCESS;
			wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
			wc.vendor_err = 0;
			wc.byte_len = wqe->length;
			wc.imm_data = 0;
			wc.qp = &qp->ibqp;
			wc.src_qp = qp->remote_qpn;
			wc.wc_flags = 0;
			wc.pkey_index = 0;
			wc.slid = qp->remote_ah_attr.dlid;
			wc.sl = qp->remote_ah_attr.sl;
			wc.dlid_path_bits = 0;
			wc.port_num = 0;
			ipath_cq_enter(to_icq(qp->ibqp.send_cq), &wc, 0);
		}
		qp->s_retry = qp->s_retry_cnt;
		/*
		 * If we are completing a request which is in the process of
		 * being resent, we can stop resending it since we know the
		 * responder has already seen it.
		 */
		if (qp->s_last == qp->s_cur) {
			if (++qp->s_cur >= qp->s_size)
				qp->s_cur = 0;
			qp->s_last = qp->s_cur;
			if (qp->s_last == qp->s_tail)
				break;
			wqe = get_swqe_ptr(qp, qp->s_cur);
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = wqe->psn;
		} else {
			if (++qp->s_last >= qp->s_size)
				qp->s_last = 0;
			if (qp->s_last == qp->s_tail)
				break;
			wqe = get_swqe_ptr(qp, qp->s_last);
		}
	}

	switch (aeth >> 29) {
	case 0:		/* ACK */
		dev->n_rc_acks++;
		/* If this is a partial ACK, reset the retransmit timer. */
		if (qp->s_last != qp->s_tail) {
			spin_lock(&dev->pending_lock);
			list_add_tail(&qp->timerwait,
				      &dev->pending[dev->pending_index]);
			spin_unlock(&dev->pending_lock);
			/*
			 * If we get a partial ACK for a resent operation,
			 * we can stop resending the earlier packets and
			 * continue with the next packet the receiver wants.
			 */
			if (ipath_cmp24(qp->s_psn, psn) <= 0) {
				reset_psn(qp, psn + 1);
				tasklet_hi_schedule(&qp->s_task);
			}
		} else if (ipath_cmp24(qp->s_psn, psn) <= 0) {
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = psn + 1;
		}
		ipath_get_credit(qp, aeth);
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		qp->s_retry = qp->s_retry_cnt;
		update_last_psn(qp, psn);
		ret = 1;
		goto bail;

	case 1:		/* RNR NAK */
		dev->n_rnr_naks++;
		if (qp->s_last == qp->s_tail)
			goto bail;
		if (qp->s_rnr_retry == 0) {
			wc.status = IB_WC_RNR_RETRY_EXC_ERR;
			goto class_b;
		}
		if (qp->s_rnr_retry_cnt < 7)
			qp->s_rnr_retry--;

		/* The last valid PSN is the previous PSN. */
		update_last_psn(qp, psn - 1);

		if (wqe->wr.opcode == IB_WR_RDMA_READ)
			dev->n_rc_resends++;
		else
			dev->n_rc_resends +=
				(qp->s_psn - psn) & IPATH_PSN_MASK;

		reset_psn(qp, psn);

		qp->s_rnr_timeout =
			ib_ipath_rnr_table[(aeth >> IPATH_AETH_CREDIT_SHIFT) &
					   IPATH_AETH_CREDIT_MASK];
		ipath_insert_rnr_queue(qp);
		goto bail;

	case 3:		/* NAK */
		if (qp->s_last == qp->s_tail)
			goto bail;
		/* The last valid PSN is the previous PSN. */
		update_last_psn(qp, psn - 1);
		switch ((aeth >> IPATH_AETH_CREDIT_SHIFT) &
			IPATH_AETH_CREDIT_MASK) {
		case 0:	/* PSN sequence error */
			dev->n_seq_naks++;
			/*
			 * Back up to the responder's expected PSN.
			 * Note that we might get a NAK in the middle of an
			 * RDMA READ response which terminates the RDMA
			 * READ.
			 */
			ipath_restart_rc(qp, psn, &wc);
			break;

		case 1:	/* Invalid Request */
			wc.status = IB_WC_REM_INV_REQ_ERR;
			dev->n_other_naks++;
			goto class_b;

		case 2:	/* Remote Access Error */
			wc.status = IB_WC_REM_ACCESS_ERR;
			dev->n_other_naks++;
			goto class_b;

		case 3:	/* Remote Operation Error */
			wc.status = IB_WC_REM_OP_ERR;
			dev->n_other_naks++;
		class_b:
			wc.wr_id = wqe->wr.wr_id;
			wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
			wc.vendor_err = 0;
			wc.byte_len = 0;
			wc.qp = &qp->ibqp;
			wc.src_qp = qp->remote_qpn;
			wc.pkey_index = 0;
			wc.slid = qp->remote_ah_attr.dlid;
			wc.sl = qp->remote_ah_attr.sl;
			wc.dlid_path_bits = 0;
			wc.port_num = 0;
			ipath_sqerror_qp(qp, &wc);
			break;

		default:
			/* Ignore other reserved NAK error codes */
			goto reserved;
		}
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		goto bail;

	default:		/* 2: reserved */
	reserved:
		/* Ignore reserved NAK codes. */
		goto bail;
	}

bail:
	return ret;
}

/**
 * ipath_rc_rcv_resp - process an incoming RC response packet
 * @dev: the device this packet came in on
 * @ohdr: the other headers for this packet
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP for this packet
 * @opcode: the opcode for this packet
 * @psn: the packet sequence number for this packet
 * @hdrsize: the header length
 * @pmtu: the path MTU
 * @header_in_data: true if part of the header data is in the data buffer
 *
 * This is called from ipath_rc_rcv() to process an incoming RC response
 * packet for the given QP.
 * Called at interrupt level.
 */
static inline void ipath_rc_rcv_resp(struct ipath_ibdev *dev,
				     struct ipath_other_headers *ohdr,
				     void *data, u32 tlen,
				     struct ipath_qp *qp,
				     u32 opcode,
				     u32 psn, u32 hdrsize, u32 pmtu,
				     int header_in_data)
{
	struct ipath_swqe *wqe;
	unsigned long flags;
	struct ib_wc wc;
	int diff;
	u32 pad;
	u32 aeth;

	spin_lock_irqsave(&qp->s_lock, flags);

	/* Ignore invalid responses. */
	if (ipath_cmp24(psn, qp->s_next_psn) >= 0)
		goto ack_done;

	/* Ignore duplicate responses. */
	diff = ipath_cmp24(psn, qp->s_last_psn);
	if (unlikely(diff <= 0)) {
		/* Update credits for "ghost" ACKs */
		if (diff == 0 && opcode == OP(ACKNOWLEDGE)) {
			if (!header_in_data)
				aeth = be32_to_cpu(ohdr->u.aeth);
			else {
				aeth = be32_to_cpu(((__be32 *) data)[0]);
				data += sizeof(__be32);
			}
			if ((aeth >> 29) == 0)
				ipath_get_credit(qp, aeth);
		}
		goto ack_done;
	}

	if (unlikely(qp->s_last == qp->s_tail))
		goto ack_done;
	wqe = get_swqe_ptr(qp, qp->s_last);

	switch (opcode) {
	case OP(ACKNOWLEDGE):
	case OP(ATOMIC_ACKNOWLEDGE):
	case OP(RDMA_READ_RESPONSE_FIRST):
		if (!header_in_data)
			aeth = be32_to_cpu(ohdr->u.aeth);
		else {
			aeth = be32_to_cpu(((__be32 *) data)[0]);
			data += sizeof(__be32);
		}
		if (opcode == OP(ATOMIC_ACKNOWLEDGE)) {
			u64 val;

			if (!header_in_data) {
				__be32 *p = ohdr->u.at.atomic_ack_eth;

				val = ((u64) be32_to_cpu(p[0]) << 32) |
					be32_to_cpu(p[1]);
			} else
				val = be64_to_cpu(((__be64 *) data)[0]);
			*(u64 *) wqe->sg_list[0].vaddr = val;
		}
		if (!do_rc_ack(qp, aeth, psn, opcode) ||
		    opcode != OP(RDMA_READ_RESPONSE_FIRST))
			goto ack_done;
		hdrsize += 4;
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
		if (unlikely(ipath_cmp24(psn, qp->s_last_psn + 1))) {
			dev->n_rdma_seq++;
			ipath_restart_rc(qp, qp->s_last_psn + 1, &wc);
			goto ack_done;
		}
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
	read_middle:
		if (unlikely(tlen != (hdrsize + pmtu + 4)))
			goto ack_len_err;
		if (unlikely(pmtu >= qp->s_rdma_read_len))
			goto ack_len_err;

		/* We got a response so update the timeout. */
		spin_lock(&dev->pending_lock);
		if (qp->s_rnr_timeout == 0 && !list_empty(&qp->timerwait))
			list_move_tail(&qp->timerwait,
				       &dev->pending[dev->pending_index]);
		spin_unlock(&dev->pending_lock);
		/*
		 * Update the RDMA receive state but do the copy w/o
		 * holding the locks and blocking interrupts.
		 */
		qp->s_rdma_read_len -= pmtu;
		update_last_psn(qp, psn);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		ipath_copy_sge(&qp->s_rdma_read_sge, data, pmtu);
		goto bail;

	case OP(RDMA_READ_RESPONSE_ONLY):
		if (unlikely(ipath_cmp24(psn, qp->s_last_psn + 1))) {
			dev->n_rdma_seq++;
			ipath_restart_rc(qp, qp->s_last_psn + 1, &wc);
			goto ack_done;
		}
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
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
		qp->s_rdma_read_len = restart_sge(&qp->s_rdma_read_sge,
						  wqe, psn, pmtu);
		goto read_last;

	case OP(RDMA_READ_RESPONSE_LAST):
		/* ACKs READ req. */
		if (unlikely(ipath_cmp24(psn, qp->s_last_psn + 1))) {
			dev->n_rdma_seq++;
			ipath_restart_rc(qp, qp->s_last_psn + 1, &wc);
			goto ack_done;
		}
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
		if (!header_in_data)
			aeth = be32_to_cpu(ohdr->u.aeth);
		else {
			aeth = be32_to_cpu(((__be32 *) data)[0]);
			data += sizeof(__be32);
		}
		ipath_copy_sge(&qp->s_rdma_read_sge, data, tlen);
		(void) do_rc_ack(qp, aeth, psn, OP(RDMA_READ_RESPONSE_LAST));
		goto ack_done;
	}

ack_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	goto bail;

ack_op_err:
	wc.status = IB_WC_LOC_QP_OP_ERR;
	goto ack_err;

ack_len_err:
	wc.status = IB_WC_LOC_LEN_ERR;
ack_err:
	wc.wr_id = wqe->wr.wr_id;
	wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
	wc.vendor_err = 0;
	wc.byte_len = 0;
	wc.imm_data = 0;
	wc.qp = &qp->ibqp;
	wc.src_qp = qp->remote_qpn;
	wc.wc_flags = 0;
	wc.pkey_index = 0;
	wc.slid = qp->remote_ah_attr.dlid;
	wc.sl = qp->remote_ah_attr.sl;
	wc.dlid_path_bits = 0;
	wc.port_num = 0;
	ipath_sqerror_qp(qp, &wc);
bail:
	return;
}

/**
 * ipath_rc_rcv_error - process an incoming duplicate or error RC packet
 * @dev: the device this packet came in on
 * @ohdr: the other headers for this packet
 * @data: the packet data
 * @qp: the QP for this packet
 * @opcode: the opcode for this packet
 * @psn: the packet sequence number for this packet
 * @diff: the difference between the PSN and the expected PSN
 * @header_in_data: true if part of the header data is in the data buffer
 *
 * This is called from ipath_rc_rcv() to process an unexpected
 * incoming RC packet for the given QP.
 * Called at interrupt level.
 * Return 1 if no more processing is needed; otherwise return 0 to
 * schedule a response to be sent.
 */
static inline int ipath_rc_rcv_error(struct ipath_ibdev *dev,
				     struct ipath_other_headers *ohdr,
				     void *data,
				     struct ipath_qp *qp,
				     u32 opcode,
				     u32 psn,
				     int diff,
				     int header_in_data)
{
	struct ipath_ack_entry *e;
	u8 i, prev;
	int old_req;

	if (diff > 0) {
		/*
		 * Packet sequence error.
		 * A NAK will ACK earlier sends and RDMA writes.
		 * Don't queue the NAK if we already sent one.
		 */
		if (!qp->r_nak_state) {
			qp->r_nak_state = IB_NAK_PSN_ERROR;
			/* Use the expected PSN. */
			qp->r_ack_psn = qp->r_psn;
			goto send_ack;
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
	 */
	psn &= IPATH_PSN_MASK;
	e = NULL;
	old_req = 1;
	spin_lock_irq(&qp->s_lock);
	for (i = qp->r_head_ack_queue; ; i = prev) {
		if (i == qp->s_tail_ack_queue)
			old_req = 0;
		if (i)
			prev = i - 1;
		else
			prev = IPATH_MAX_RDMA_ATOMIC;
		if (prev == qp->r_head_ack_queue) {
			e = NULL;
			break;
		}
		e = &qp->s_ack_queue[prev];
		if (!e->opcode) {
			e = NULL;
			break;
		}
		if (ipath_cmp24(psn, e->psn) >= 0)
			break;
	}
	switch (opcode) {
	case OP(RDMA_READ_REQUEST): {
		struct ib_reth *reth;
		u32 offset;
		u32 len;

		/*
		 * If we didn't find the RDMA read request in the ack queue,
		 * or the send tasklet is already backed up to send an
		 * earlier entry, we can ignore this request.
		 */
		if (!e || e->opcode != OP(RDMA_READ_REQUEST) || old_req)
			goto unlock_done;
		/* RETH comes after BTH */
		if (!header_in_data)
			reth = &ohdr->u.rc.reth;
		else {
			reth = (struct ib_reth *)data;
			data += sizeof(*reth);
		}
		/*
		 * Address range must be a subset of the original
		 * request and start on pmtu boundaries.
		 * We reuse the old ack_queue slot since the requester
		 * should not back up and request an earlier PSN for the
		 * same request.
		 */
		offset = ((psn - e->psn) & IPATH_PSN_MASK) *
			ib_mtu_enum_to_int(qp->path_mtu);
		len = be32_to_cpu(reth->length);
		if (unlikely(offset + len > e->rdma_sge.sge.sge_length))
			goto unlock_done;
		if (len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			ok = ipath_rkey_ok(qp, &e->rdma_sge,
					   len, vaddr, rkey,
					   IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok))
				goto unlock_done;
		} else {
			e->rdma_sge.sg_list = NULL;
			e->rdma_sge.num_sge = 0;
			e->rdma_sge.sge.mr = NULL;
			e->rdma_sge.sge.vaddr = NULL;
			e->rdma_sge.sge.length = 0;
			e->rdma_sge.sge.sge_length = 0;
		}
		e->psn = psn;
		qp->s_ack_state = OP(ACKNOWLEDGE);
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
		qp->s_ack_state = OP(ACKNOWLEDGE);
		qp->s_tail_ack_queue = prev;
		break;
	}

	default:
		if (old_req)
			goto unlock_done;
		/*
		 * Resend the most recent ACK if this request is
		 * after all the previous RDMA reads and atomics.
		 */
		if (i == qp->r_head_ack_queue) {
			spin_unlock_irq(&qp->s_lock);
			qp->r_nak_state = 0;
			qp->r_ack_psn = qp->r_psn - 1;
			goto send_ack;
		}
		/*
		 * Resend the RDMA read or atomic op which
		 * ACKs this duplicate request.
		 */
		qp->s_ack_state = OP(ACKNOWLEDGE);
		qp->s_tail_ack_queue = i;
		break;
	}
	qp->r_nak_state = 0;
	spin_unlock_irq(&qp->s_lock);
	tasklet_hi_schedule(&qp->s_task);

unlock_done:
	spin_unlock_irq(&qp->s_lock);
done:
	return 1;

send_ack:
	return 0;
}

static void ipath_rc_error(struct ipath_qp *qp, enum ib_wc_status err)
{
	spin_lock_irq(&qp->s_lock);
	qp->state = IB_QPS_ERR;
	ipath_error_qp(qp, err);
	spin_unlock_irq(&qp->s_lock);
}

/**
 * ipath_rc_rcv - process an incoming RC packet
 * @dev: the device this packet came in on
 * @hdr: the header of this packet
 * @has_grh: true if the header has a GRH
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP for this packet
 *
 * This is called from ipath_qp_rcv() to process an incoming RC packet
 * for the given QP.
 * Called at interrupt level.
 */
void ipath_rc_rcv(struct ipath_ibdev *dev, struct ipath_ib_header *hdr,
		  int has_grh, void *data, u32 tlen, struct ipath_qp *qp)
{
	struct ipath_other_headers *ohdr;
	u32 opcode;
	u32 hdrsize;
	u32 psn;
	u32 pad;
	struct ib_wc wc;
	u32 pmtu = ib_mtu_enum_to_int(qp->path_mtu);
	int diff;
	struct ib_reth *reth;
	int header_in_data;

	/* Validate the SLID. See Ch. 9.6.1.5 */
	if (unlikely(be16_to_cpu(hdr->lrh[3]) != qp->remote_ah_attr.dlid))
		goto done;

	/* Check for GRH */
	if (!has_grh) {
		ohdr = &hdr->u.oth;
		hdrsize = 8 + 12;	/* LRH + BTH */
		psn = be32_to_cpu(ohdr->bth[2]);
		header_in_data = 0;
	} else {
		ohdr = &hdr->u.l.oth;
		hdrsize = 8 + 40 + 12;	/* LRH + GRH + BTH */
		/*
		 * The header with GRH is 60 bytes and the core driver sets
		 * the eager header buffer size to 56 bytes so the last 4
		 * bytes of the BTH header (PSN) is in the data buffer.
		 */
		header_in_data = dev->dd->ipath_rcvhdrentsize == 16;
		if (header_in_data) {
			psn = be32_to_cpu(((__be32 *) data)[0]);
			data += sizeof(__be32);
		} else
			psn = be32_to_cpu(ohdr->bth[2]);
	}

	/*
	 * Process responses (ACKs) before anything else.  Note that the
	 * packet sequence number will be for something in the send work
	 * queue rather than the expected receive packet sequence number.
	 * In other words, this QP is the requester.
	 */
	opcode = be32_to_cpu(ohdr->bth[0]) >> 24;
	if (opcode >= OP(RDMA_READ_RESPONSE_FIRST) &&
	    opcode <= OP(ATOMIC_ACKNOWLEDGE)) {
		ipath_rc_rcv_resp(dev, ohdr, data, tlen, qp, opcode, psn,
				  hdrsize, pmtu, header_in_data);
		goto done;
	}

	/* Compute 24 bits worth of difference. */
	diff = ipath_cmp24(psn, qp->r_psn);
	if (unlikely(diff)) {
		if (ipath_rc_rcv_error(dev, ohdr, data, qp, opcode,
				       psn, diff, header_in_data))
			goto done;
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
	nack_inv:
		ipath_rc_error(qp, IB_WC_REM_INV_REQ_ERR);
		qp->r_nak_state = IB_NAK_INVALID_REQUEST;
		qp->r_ack_psn = qp->r_psn;
		goto send_ack;

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

	wc.imm_data = 0;
	wc.wc_flags = 0;

	/* OK, process the packet. */
	switch (opcode) {
	case OP(SEND_FIRST):
		if (!ipath_get_rwqe(qp, 0)) {
		rnr_nak:
			/*
			 * A RNR NAK will ACK earlier sends and RDMA writes.
			 * Don't queue the NAK if a RDMA read or atomic
			 * is pending though.
			 */
			if (qp->r_nak_state)
				goto done;
			qp->r_nak_state = IB_RNR_NAK | qp->r_min_rnr_timer;
			qp->r_ack_psn = qp->r_psn;
			goto send_ack;
		}
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
		ipath_copy_sge(&qp->r_sge, data, pmtu);
		break;

	case OP(RDMA_WRITE_LAST_WITH_IMMEDIATE):
		/* consume RWQE */
		if (!ipath_get_rwqe(qp, 1))
			goto rnr_nak;
		goto send_last_imm;

	case OP(SEND_ONLY):
	case OP(SEND_ONLY_WITH_IMMEDIATE):
		if (!ipath_get_rwqe(qp, 0))
			goto rnr_nak;
		qp->r_rcv_len = 0;
		if (opcode == OP(SEND_ONLY))
			goto send_last;
		/* FALLTHROUGH */
	case OP(SEND_LAST_WITH_IMMEDIATE):
	send_last_imm:
		if (header_in_data) {
			wc.imm_data = *(__be32 *) data;
			data += sizeof(__be32);
		} else {
			/* Immediate data comes after BTH */
			wc.imm_data = ohdr->u.imm_data;
		}
		hdrsize += 4;
		wc.wc_flags = IB_WC_WITH_IMM;
		/* FALLTHROUGH */
	case OP(SEND_LAST):
	case OP(RDMA_WRITE_LAST):
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
		ipath_copy_sge(&qp->r_sge, data, tlen);
		qp->r_msn++;
		if (!qp->r_wrid_valid)
			break;
		qp->r_wrid_valid = 0;
		wc.wr_id = qp->r_wr_id;
		wc.status = IB_WC_SUCCESS;
		wc.opcode = IB_WC_RECV;
		wc.vendor_err = 0;
		wc.qp = &qp->ibqp;
		wc.src_qp = qp->remote_qpn;
		wc.pkey_index = 0;
		wc.slid = qp->remote_ah_attr.dlid;
		wc.sl = qp->remote_ah_attr.sl;
		wc.dlid_path_bits = 0;
		wc.port_num = 0;
		/* Signal completion event if the solicited bit is set. */
		ipath_cq_enter(to_icq(qp->ibqp.recv_cq), &wc,
			       (ohdr->bth[0] &
				__constant_cpu_to_be32(1 << 23)) != 0);
		break;

	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_ONLY):
	case OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE):
		/* consume RWQE */
		/* RETH comes after BTH */
		if (!header_in_data)
			reth = &ohdr->u.rc.reth;
		else {
			reth = (struct ib_reth *)data;
			data += sizeof(*reth);
		}
		hdrsize += sizeof(*reth);
		qp->r_len = be32_to_cpu(reth->length);
		qp->r_rcv_len = 0;
		if (qp->r_len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/* Check rkey & NAK */
			ok = ipath_rkey_ok(qp, &qp->r_sge,
					   qp->r_len, vaddr, rkey,
					   IB_ACCESS_REMOTE_WRITE);
			if (unlikely(!ok))
				goto nack_acc;
		} else {
			qp->r_sge.sg_list = NULL;
			qp->r_sge.sge.mr = NULL;
			qp->r_sge.sge.vaddr = NULL;
			qp->r_sge.sge.length = 0;
			qp->r_sge.sge.sge_length = 0;
		}
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_WRITE)))
			goto nack_acc;
		if (opcode == OP(RDMA_WRITE_FIRST))
			goto send_middle;
		else if (opcode == OP(RDMA_WRITE_ONLY))
			goto send_last;
		if (!ipath_get_rwqe(qp, 1))
			goto rnr_nak;
		goto send_last_imm;

	case OP(RDMA_READ_REQUEST): {
		struct ipath_ack_entry *e;
		u32 len;
		u8 next;

		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_READ)))
			goto nack_acc;
		next = qp->r_head_ack_queue + 1;
		if (next > IPATH_MAX_RDMA_ATOMIC)
			next = 0;
		if (unlikely(next == qp->s_tail_ack_queue))
			goto nack_inv;
		e = &qp->s_ack_queue[qp->r_head_ack_queue];
		/* RETH comes after BTH */
		if (!header_in_data)
			reth = &ohdr->u.rc.reth;
		else {
			reth = (struct ib_reth *)data;
			data += sizeof(*reth);
		}
		len = be32_to_cpu(reth->length);
		if (len) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/* Check rkey & NAK */
			ok = ipath_rkey_ok(qp, &e->rdma_sge, len, vaddr,
					   rkey, IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok))
				goto nack_acc;
			/*
			 * Update the next expected PSN.  We add 1 later
			 * below, so only add the remainder here.
			 */
			if (len > pmtu)
				qp->r_psn += (len - 1) / pmtu;
		} else {
			e->rdma_sge.sg_list = NULL;
			e->rdma_sge.num_sge = 0;
			e->rdma_sge.sge.mr = NULL;
			e->rdma_sge.sge.vaddr = NULL;
			e->rdma_sge.sge.length = 0;
			e->rdma_sge.sge.sge_length = 0;
		}
		e->opcode = opcode;
		e->psn = psn;
		/*
		 * We need to increment the MSN here instead of when we
		 * finish sending the result since a duplicate request would
		 * increment it more than once.
		 */
		qp->r_msn++;
		qp->r_psn++;
		qp->r_state = opcode;
		qp->r_nak_state = 0;
		barrier();
		qp->r_head_ack_queue = next;

		/* Call ipath_do_rc_send() in another thread. */
		tasklet_hi_schedule(&qp->s_task);

		goto done;
	}

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD): {
		struct ib_atomic_eth *ateth;
		struct ipath_ack_entry *e;
		u64 vaddr;
		atomic64_t *maddr;
		u64 sdata;
		u32 rkey;
		u8 next;

		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_acc;
		next = qp->r_head_ack_queue + 1;
		if (next > IPATH_MAX_RDMA_ATOMIC)
			next = 0;
		if (unlikely(next == qp->s_tail_ack_queue))
			goto nack_inv;
		if (!header_in_data)
			ateth = &ohdr->u.atomic_eth;
		else
			ateth = (struct ib_atomic_eth *)data;
		vaddr = ((u64) be32_to_cpu(ateth->vaddr[0]) << 32) |
			be32_to_cpu(ateth->vaddr[1]);
		if (unlikely(vaddr & (sizeof(u64) - 1)))
			goto nack_inv;
		rkey = be32_to_cpu(ateth->rkey);
		/* Check rkey & NAK */
		if (unlikely(!ipath_rkey_ok(qp, &qp->r_sge,
					    sizeof(u64), vaddr, rkey,
					    IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_acc;
		/* Perform atomic OP and save result. */
		maddr = (atomic64_t *) qp->r_sge.sge.vaddr;
		sdata = be64_to_cpu(ateth->swap_data);
		e = &qp->s_ack_queue[qp->r_head_ack_queue];
		e->atomic_data = (opcode == OP(FETCH_ADD)) ?
			(u64) atomic64_add_return(sdata, maddr) - sdata :
			(u64) cmpxchg((u64 *) qp->r_sge.sge.vaddr,
				      be64_to_cpu(ateth->compare_data),
				      sdata);
		e->opcode = opcode;
		e->psn = psn & IPATH_PSN_MASK;
		qp->r_msn++;
		qp->r_psn++;
		qp->r_state = opcode;
		qp->r_nak_state = 0;
		barrier();
		qp->r_head_ack_queue = next;

		/* Call ipath_do_rc_send() in another thread. */
		tasklet_hi_schedule(&qp->s_task);

		goto done;
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
	goto done;

nack_acc:
	ipath_rc_error(qp, IB_WC_REM_ACCESS_ERR);
	qp->r_nak_state = IB_NAK_REMOTE_ACCESS_ERROR;
	qp->r_ack_psn = qp->r_psn;

send_ack:
	send_rc_ack(qp);

done:
	return;
}
