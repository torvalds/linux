/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/io.h>
#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_qp.h>

#include "hfi.h"
#include "qp.h"
#include "verbs_txreq.h"
#include "trace.h"

/* cut down ridiculously long IB macro names */
#define OP(x) RC_OP(x)

static u32 restart_sge(struct rvt_sge_state *ss, struct rvt_swqe *wqe,
		       u32 psn, u32 pmtu)
{
	u32 len;

	len = delta_psn(psn, wqe->psn) * pmtu;
	ss->sge = wqe->sg_list[0];
	ss->sg_list = wqe->sg_list + 1;
	ss->num_sge = wqe->wr.num_sge;
	ss->total_len = wqe->length;
	rvt_skip_sge(ss, len, false);
	return wqe->length - len;
}

/**
 * make_rc_ack - construct a response packet (ACK, NAK, or RDMA read)
 * @dev: the device for this QP
 * @qp: a pointer to the QP
 * @ohdr: a pointer to the IB header being constructed
 * @ps: the xmit packet state
 *
 * Return 1 if constructed; otherwise, return 0.
 * Note that we are in the responder's side of the QP context.
 * Note the QP s_lock must be held.
 */
static int make_rc_ack(struct hfi1_ibdev *dev, struct rvt_qp *qp,
		       struct ib_other_headers *ohdr,
		       struct hfi1_pkt_state *ps)
{
	struct rvt_ack_entry *e;
	u32 hwords;
	u32 len;
	u32 bth0;
	u32 bth2;
	int middle = 0;
	u32 pmtu = qp->pmtu;
	struct hfi1_qp_priv *priv = qp->priv;

	lockdep_assert_held(&qp->s_lock);
	/* Don't send an ACK if we aren't supposed to. */
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK))
		goto bail;

	if (priv->hdr_type == HFI1_PKT_TYPE_9B)
		/* header size in 32-bit words LRH+BTH = (8+12)/4. */
		hwords = 5;
	else
		/* header size in 32-bit words 16B LRH+BTH = (16+12)/4. */
		hwords = 7;

	switch (qp->s_ack_state) {
	case OP(RDMA_READ_RESPONSE_LAST):
	case OP(RDMA_READ_RESPONSE_ONLY):
		e = &qp->s_ack_queue[qp->s_tail_ack_queue];
		if (e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
		/* FALLTHROUGH */
	case OP(ATOMIC_ACKNOWLEDGE):
		/*
		 * We can increment the tail pointer now that the last
		 * response has been sent instead of only being
		 * constructed.
		 */
		if (++qp->s_tail_ack_queue > HFI1_MAX_RDMA_ATOMIC)
			qp->s_tail_ack_queue = 0;
		/* FALLTHROUGH */
	case OP(SEND_ONLY):
	case OP(ACKNOWLEDGE):
		/* Check for no next entry in the queue. */
		if (qp->r_head_ack_queue == qp->s_tail_ack_queue) {
			if (qp->s_flags & RVT_S_ACK_PENDING)
				goto normal;
			goto bail;
		}

		e = &qp->s_ack_queue[qp->s_tail_ack_queue];
		if (e->opcode == OP(RDMA_READ_REQUEST)) {
			/*
			 * If a RDMA read response is being resent and
			 * we haven't seen the duplicate request yet,
			 * then stop sending the remaining responses the
			 * responder has seen until the requester re-sends it.
			 */
			len = e->rdma_sge.sge_length;
			if (len && !e->rdma_sge.mr) {
				qp->s_tail_ack_queue = qp->r_head_ack_queue;
				goto bail;
			}
			/* Copy SGE state in case we need to resend */
			ps->s_txreq->mr = e->rdma_sge.mr;
			if (ps->s_txreq->mr)
				rvt_get_mr(ps->s_txreq->mr);
			qp->s_ack_rdma_sge.sge = e->rdma_sge;
			qp->s_ack_rdma_sge.num_sge = 1;
			ps->s_txreq->ss = &qp->s_ack_rdma_sge;
			if (len > pmtu) {
				len = pmtu;
				qp->s_ack_state = OP(RDMA_READ_RESPONSE_FIRST);
			} else {
				qp->s_ack_state = OP(RDMA_READ_RESPONSE_ONLY);
				e->sent = 1;
			}
			ohdr->u.aeth = rvt_compute_aeth(qp);
			hwords++;
			qp->s_ack_rdma_psn = e->psn;
			bth2 = mask_psn(qp->s_ack_rdma_psn++);
		} else {
			/* COMPARE_SWAP or FETCH_ADD */
			ps->s_txreq->ss = NULL;
			len = 0;
			qp->s_ack_state = OP(ATOMIC_ACKNOWLEDGE);
			ohdr->u.at.aeth = rvt_compute_aeth(qp);
			ib_u64_put(e->atomic_data, &ohdr->u.at.atomic_ack_eth);
			hwords += sizeof(ohdr->u.at) / sizeof(u32);
			bth2 = mask_psn(e->psn);
			e->sent = 1;
		}
		bth0 = qp->s_ack_state << 24;
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		qp->s_ack_state = OP(RDMA_READ_RESPONSE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_READ_RESPONSE_MIDDLE):
		ps->s_txreq->ss = &qp->s_ack_rdma_sge;
		ps->s_txreq->mr = qp->s_ack_rdma_sge.sge.mr;
		if (ps->s_txreq->mr)
			rvt_get_mr(ps->s_txreq->mr);
		len = qp->s_ack_rdma_sge.sge.sge_length;
		if (len > pmtu) {
			len = pmtu;
			middle = HFI1_CAP_IS_KSET(SDMA_AHG);
		} else {
			ohdr->u.aeth = rvt_compute_aeth(qp);
			hwords++;
			qp->s_ack_state = OP(RDMA_READ_RESPONSE_LAST);
			e = &qp->s_ack_queue[qp->s_tail_ack_queue];
			e->sent = 1;
		}
		bth0 = qp->s_ack_state << 24;
		bth2 = mask_psn(qp->s_ack_rdma_psn++);
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
		qp->s_flags &= ~RVT_S_ACK_PENDING;
		ps->s_txreq->ss = NULL;
		if (qp->s_nak_state)
			ohdr->u.aeth =
				cpu_to_be32((qp->r_msn & IB_MSN_MASK) |
					    (qp->s_nak_state <<
					     IB_AETH_CREDIT_SHIFT));
		else
			ohdr->u.aeth = rvt_compute_aeth(qp);
		hwords++;
		len = 0;
		bth0 = OP(ACKNOWLEDGE) << 24;
		bth2 = mask_psn(qp->s_ack_psn);
	}
	qp->s_rdma_ack_cnt++;
	ps->s_txreq->sde = priv->s_sde;
	ps->s_txreq->s_cur_size = len;
	ps->s_txreq->hdr_dwords = hwords;
	hfi1_make_ruc_header(qp, ohdr, bth0, bth2, middle, ps);
	return 1;

bail:
	qp->s_ack_state = OP(ACKNOWLEDGE);
	/*
	 * Ensure s_rdma_ack_cnt changes are committed prior to resetting
	 * RVT_S_RESP_PENDING
	 */
	smp_wmb();
	qp->s_flags &= ~(RVT_S_RESP_PENDING
				| RVT_S_ACK_PENDING
				| HFI1_S_AHG_VALID);
	return 0;
}

/**
 * hfi1_make_rc_req - construct a request packet (SEND, RDMA r/w, ATOMIC)
 * @qp: a pointer to the QP
 *
 * Assumes s_lock is held.
 *
 * Return 1 if constructed; otherwise, return 0.
 */
int hfi1_make_rc_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	struct ib_other_headers *ohdr;
	struct rvt_sge_state *ss;
	struct rvt_swqe *wqe;
	u32 hwords;
	u32 len;
	u32 bth0 = 0;
	u32 bth2;
	u32 pmtu = qp->pmtu;
	char newreq;
	int middle = 0;
	int delta;

	lockdep_assert_held(&qp->s_lock);
	ps->s_txreq = get_txreq(ps->dev, qp);
	if (!ps->s_txreq)
		goto bail_no_tx;

	if (priv->hdr_type == HFI1_PKT_TYPE_9B) {
		/* header size in 32-bit words LRH+BTH = (8+12)/4. */
		hwords = 5;
		if (rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH)
			ohdr = &ps->s_txreq->phdr.hdr.ibh.u.l.oth;
		else
			ohdr = &ps->s_txreq->phdr.hdr.ibh.u.oth;
	} else {
		/* header size in 32-bit words 16B LRH+BTH = (16+12)/4. */
		hwords = 7;
		if ((rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH) &&
		    (hfi1_check_mcast(rdma_ah_get_dlid(&qp->remote_ah_attr))))
			ohdr = &ps->s_txreq->phdr.hdr.opah.u.l.oth;
		else
			ohdr = &ps->s_txreq->phdr.hdr.opah.u.oth;
	}

	/* Sending responses has higher priority over sending requests. */
	if ((qp->s_flags & RVT_S_RESP_PENDING) &&
	    make_rc_ack(dev, qp, ohdr, ps))
		return 1;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_SEND_OK)) {
		if (!(ib_rvt_state_ops[qp->state] & RVT_FLUSH_SEND))
			goto bail;
		/* We are in the error state, flush the work request. */
		if (qp->s_last == READ_ONCE(qp->s_head))
			goto bail;
		/* If DMAs are in progress, we can't flush immediately. */
		if (iowait_sdma_pending(&priv->s_iowait)) {
			qp->s_flags |= RVT_S_WAIT_DMA;
			goto bail;
		}
		clear_ahg(qp);
		wqe = rvt_get_swqe_ptr(qp, qp->s_last);
		hfi1_send_complete(qp, wqe, qp->s_last != qp->s_acked ?
			IB_WC_SUCCESS : IB_WC_WR_FLUSH_ERR);
		/* will get called again */
		goto done_free_tx;
	}

	if (qp->s_flags & (RVT_S_WAIT_RNR | RVT_S_WAIT_ACK))
		goto bail;

	if (cmp_psn(qp->s_psn, qp->s_sending_hpsn) <= 0) {
		if (cmp_psn(qp->s_sending_psn, qp->s_sending_hpsn) <= 0) {
			qp->s_flags |= RVT_S_WAIT_PSN;
			goto bail;
		}
		qp->s_sending_psn = qp->s_psn;
		qp->s_sending_hpsn = qp->s_psn - 1;
	}

	/* Send a request. */
	wqe = rvt_get_swqe_ptr(qp, qp->s_cur);
	switch (qp->s_state) {
	default:
		if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_NEXT_SEND_OK))
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
			if (qp->s_tail == READ_ONCE(qp->s_head)) {
				clear_ahg(qp);
				goto bail;
			}
			/*
			 * If a fence is requested, wait for previous
			 * RDMA read and atomic operations to finish.
			 */
			if ((wqe->wr.send_flags & IB_SEND_FENCE) &&
			    qp->s_num_rd_atomic) {
				qp->s_flags |= RVT_S_WAIT_FENCE;
				goto bail;
			}
			/*
			 * Local operations are processed immediately
			 * after all prior requests have completed
			 */
			if (wqe->wr.opcode == IB_WR_REG_MR ||
			    wqe->wr.opcode == IB_WR_LOCAL_INV) {
				int local_ops = 0;
				int err = 0;

				if (qp->s_last != qp->s_cur)
					goto bail;
				if (++qp->s_cur == qp->s_size)
					qp->s_cur = 0;
				if (++qp->s_tail == qp->s_size)
					qp->s_tail = 0;
				if (!(wqe->wr.send_flags &
				      RVT_SEND_COMPLETION_ONLY)) {
					err = rvt_invalidate_rkey(
						qp,
						wqe->wr.ex.invalidate_rkey);
					local_ops = 1;
				}
				hfi1_send_complete(qp, wqe,
						   err ? IB_WC_LOC_PROT_ERR
						       : IB_WC_SUCCESS);
				if (local_ops)
					atomic_dec(&qp->local_ops_pending);
				goto done_free_tx;
			}

			newreq = 1;
			qp->s_psn = wqe->psn;
		}
		/*
		 * Note that we have to be careful not to modify the
		 * original work request since we may need to resend
		 * it.
		 */
		len = wqe->length;
		ss = &qp->s_sge;
		bth2 = mask_psn(qp->s_psn);
		switch (wqe->wr.opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
		case IB_WR_SEND_WITH_INV:
			/* If no credit, return. */
			if (!(qp->s_flags & RVT_S_UNLIMITED_CREDIT) &&
			    rvt_cmp_msn(wqe->ssn, qp->s_lsn + 1) > 0) {
				qp->s_flags |= RVT_S_WAIT_SSN_CREDIT;
				goto bail;
			}
			if (len > pmtu) {
				qp->s_state = OP(SEND_FIRST);
				len = pmtu;
				break;
			}
			if (wqe->wr.opcode == IB_WR_SEND) {
				qp->s_state = OP(SEND_ONLY);
			} else if (wqe->wr.opcode == IB_WR_SEND_WITH_IMM) {
				qp->s_state = OP(SEND_ONLY_WITH_IMMEDIATE);
				/* Immediate data comes after the BTH */
				ohdr->u.imm_data = wqe->wr.ex.imm_data;
				hwords += 1;
			} else {
				qp->s_state = OP(SEND_ONLY_WITH_INVALIDATE);
				/* Invalidate rkey comes after the BTH */
				ohdr->u.ieth = cpu_to_be32(
						wqe->wr.ex.invalidate_rkey);
				hwords += 1;
			}
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= IB_BTH_SOLICITED;
			bth2 |= IB_BTH_REQ_ACK;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_RDMA_WRITE:
			if (newreq && !(qp->s_flags & RVT_S_UNLIMITED_CREDIT))
				qp->s_lsn++;
			goto no_flow_control;
		case IB_WR_RDMA_WRITE_WITH_IMM:
			/* If no credit, return. */
			if (!(qp->s_flags & RVT_S_UNLIMITED_CREDIT) &&
			    rvt_cmp_msn(wqe->ssn, qp->s_lsn + 1) > 0) {
				qp->s_flags |= RVT_S_WAIT_SSN_CREDIT;
				goto bail;
			}
no_flow_control:
			put_ib_reth_vaddr(
				wqe->rdma_wr.remote_addr,
				&ohdr->u.rc.reth);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->rdma_wr.rkey);
			ohdr->u.rc.reth.length = cpu_to_be32(len);
			hwords += sizeof(struct ib_reth) / sizeof(u32);
			if (len > pmtu) {
				qp->s_state = OP(RDMA_WRITE_FIRST);
				len = pmtu;
				break;
			}
			if (wqe->wr.opcode == IB_WR_RDMA_WRITE) {
				qp->s_state = OP(RDMA_WRITE_ONLY);
			} else {
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
					qp->s_flags |= RVT_S_WAIT_RDMAR;
					goto bail;
				}
				qp->s_num_rd_atomic++;
				if (!(qp->s_flags & RVT_S_UNLIMITED_CREDIT))
					qp->s_lsn++;
			}
			put_ib_reth_vaddr(
				wqe->rdma_wr.remote_addr,
				&ohdr->u.rc.reth);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->rdma_wr.rkey);
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
					qp->s_flags |= RVT_S_WAIT_RDMAR;
					goto bail;
				}
				qp->s_num_rd_atomic++;
				if (!(qp->s_flags & RVT_S_UNLIMITED_CREDIT))
					qp->s_lsn++;
			}
			if (wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP) {
				qp->s_state = OP(COMPARE_SWAP);
				put_ib_ateth_swap(wqe->atomic_wr.swap,
						  &ohdr->u.atomic_eth);
				put_ib_ateth_compare(wqe->atomic_wr.compare_add,
						     &ohdr->u.atomic_eth);
			} else {
				qp->s_state = OP(FETCH_ADD);
				put_ib_ateth_swap(wqe->atomic_wr.compare_add,
						  &ohdr->u.atomic_eth);
				put_ib_ateth_compare(0, &ohdr->u.atomic_eth);
			}
			put_ib_ateth_vaddr(wqe->atomic_wr.remote_addr,
					   &ohdr->u.atomic_eth);
			ohdr->u.atomic_eth.rkey = cpu_to_be32(
				wqe->atomic_wr.rkey);
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
		else
			qp->s_psn++;
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		/*
		 * qp->s_state is normally set to the opcode of the
		 * last packet constructed for new requests and therefore
		 * is never set to RDMA read response.
		 * RDMA_READ_RESPONSE_FIRST is used by the ACK processing
		 * thread to indicate a SEND needs to be restarted from an
		 * earlier PSN without interfering with the sending thread.
		 * See restart_rc().
		 */
		qp->s_len = restart_sge(&qp->s_sge, wqe, qp->s_psn, pmtu);
		/* FALLTHROUGH */
	case OP(SEND_FIRST):
		qp->s_state = OP(SEND_MIDDLE);
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		bth2 = mask_psn(qp->s_psn++);
		ss = &qp->s_sge;
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			middle = HFI1_CAP_IS_KSET(SDMA_AHG);
			break;
		}
		if (wqe->wr.opcode == IB_WR_SEND) {
			qp->s_state = OP(SEND_LAST);
		} else if (wqe->wr.opcode == IB_WR_SEND_WITH_IMM) {
			qp->s_state = OP(SEND_LAST_WITH_IMMEDIATE);
			/* Immediate data comes after the BTH */
			ohdr->u.imm_data = wqe->wr.ex.imm_data;
			hwords += 1;
		} else {
			qp->s_state = OP(SEND_LAST_WITH_INVALIDATE);
			/* invalidate data comes after the BTH */
			ohdr->u.ieth = cpu_to_be32(wqe->wr.ex.invalidate_rkey);
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
		 * an earlier PSN without interfering with the sending thread.
		 * See restart_rc().
		 */
		qp->s_len = restart_sge(&qp->s_sge, wqe, qp->s_psn, pmtu);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_FIRST):
		qp->s_state = OP(RDMA_WRITE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		bth2 = mask_psn(qp->s_psn++);
		ss = &qp->s_sge;
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			middle = HFI1_CAP_IS_KSET(SDMA_AHG);
			break;
		}
		if (wqe->wr.opcode == IB_WR_RDMA_WRITE) {
			qp->s_state = OP(RDMA_WRITE_LAST);
		} else {
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
		 * an earlier PSN without interfering with the sending thread.
		 * See restart_rc().
		 */
		len = (delta_psn(qp->s_psn, wqe->psn)) * pmtu;
		put_ib_reth_vaddr(
			wqe->rdma_wr.remote_addr + len,
			&ohdr->u.rc.reth);
		ohdr->u.rc.reth.rkey =
			cpu_to_be32(wqe->rdma_wr.rkey);
		ohdr->u.rc.reth.length = cpu_to_be32(wqe->length - len);
		qp->s_state = OP(RDMA_READ_REQUEST);
		hwords += sizeof(ohdr->u.rc.reth) / sizeof(u32);
		bth2 = mask_psn(qp->s_psn) | IB_BTH_REQ_ACK;
		qp->s_psn = wqe->lpsn + 1;
		ss = NULL;
		len = 0;
		qp->s_cur++;
		if (qp->s_cur == qp->s_size)
			qp->s_cur = 0;
		break;
	}
	qp->s_sending_hpsn = bth2;
	delta = delta_psn(bth2, wqe->psn);
	if (delta && delta % HFI1_PSN_CREDIT == 0)
		bth2 |= IB_BTH_REQ_ACK;
	if (qp->s_flags & RVT_S_SEND_ONE) {
		qp->s_flags &= ~RVT_S_SEND_ONE;
		qp->s_flags |= RVT_S_WAIT_ACK;
		bth2 |= IB_BTH_REQ_ACK;
	}
	qp->s_len -= len;
	ps->s_txreq->hdr_dwords = hwords;
	ps->s_txreq->sde = priv->s_sde;
	ps->s_txreq->ss = ss;
	ps->s_txreq->s_cur_size = len;
	hfi1_make_ruc_header(
		qp,
		ohdr,
		bth0 | (qp->s_state << 24),
		bth2,
		middle,
		ps);
	return 1;

done_free_tx:
	hfi1_put_txreq(ps->s_txreq);
	ps->s_txreq = NULL;
	return 1;

bail:
	hfi1_put_txreq(ps->s_txreq);

bail_no_tx:
	ps->s_txreq = NULL;
	qp->s_flags &= ~RVT_S_BUSY;
	return 0;
}

static inline void hfi1_make_bth_aeth(struct rvt_qp *qp,
				      struct ib_other_headers *ohdr,
				      u32 bth0, u32 bth1)
{
	if (qp->r_nak_state)
		ohdr->u.aeth = cpu_to_be32((qp->r_msn & IB_MSN_MASK) |
					    (qp->r_nak_state <<
					     IB_AETH_CREDIT_SHIFT));
	else
		ohdr->u.aeth = rvt_compute_aeth(qp);

	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(bth1 | qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(mask_psn(qp->r_ack_psn));
}

static inline void hfi1_queue_rc_ack(struct hfi1_packet *packet, bool is_fecn)
{
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp;
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK))
		goto unlock;
	ibp = rcd_to_iport(packet->rcd);
	this_cpu_inc(*ibp->rvp.rc_qacks);
	qp->s_flags |= RVT_S_ACK_PENDING | RVT_S_RESP_PENDING;
	qp->s_nak_state = qp->r_nak_state;
	qp->s_ack_psn = qp->r_ack_psn;
	if (is_fecn)
		qp->s_flags |= RVT_S_ECN;

	/* Schedule the send tasklet. */
	hfi1_schedule_send(qp);
unlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
}

static inline void hfi1_make_rc_ack_9B(struct hfi1_packet *packet,
				       struct hfi1_opa_header *opa_hdr,
				       u8 sc5, bool is_fecn,
				       u64 *pbc_flags, u32 *hwords,
				       u32 *nwords)
{
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct ib_header *hdr = &opa_hdr->ibh;
	struct ib_other_headers *ohdr;
	u16 lrh0 = HFI1_LRH_BTH;
	u16 pkey;
	u32 bth0, bth1;

	opa_hdr->hdr_type = HFI1_PKT_TYPE_9B;
	ohdr = &hdr->u.oth;
	/* header size in 32-bit words LRH+BTH+AETH = (8+12+4)/4 */
	*hwords = 6;

	if (unlikely(rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH)) {
		*hwords += hfi1_make_grh(ibp, &hdr->u.l.grh,
					 rdma_ah_read_grh(&qp->remote_ah_attr),
					 *hwords - 2, SIZE_OF_CRC);
		ohdr = &hdr->u.l.oth;
		lrh0 = HFI1_LRH_GRH;
	}
	/* set PBC_DC_INFO bit (aka SC[4]) in pbc_flags */
	*pbc_flags |= ((!!(sc5 & 0x10)) << PBC_DC_INFO_SHIFT);

	/* read pkey_index w/o lock (its atomic) */
	pkey = hfi1_get_pkey(ibp, qp->s_pkey_index);

	lrh0 |= (sc5 & IB_SC_MASK) << IB_SC_SHIFT |
		(rdma_ah_get_sl(&qp->remote_ah_attr) & IB_SL_MASK) <<
			IB_SL_SHIFT;

	hfi1_make_ib_hdr(hdr, lrh0, *hwords + SIZE_OF_CRC,
			 opa_get_lid(rdma_ah_get_dlid(&qp->remote_ah_attr), 9B),
			 ppd->lid | rdma_ah_get_path_bits(&qp->remote_ah_attr));

	bth0 = pkey | (OP(ACKNOWLEDGE) << 24);
	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth0 |= IB_BTH_MIG_REQ;
	bth1 = (!!is_fecn) << IB_BECN_SHIFT;
	hfi1_make_bth_aeth(qp, ohdr, bth0, bth1);
}

static inline void hfi1_make_rc_ack_16B(struct hfi1_packet *packet,
					struct hfi1_opa_header *opa_hdr,
					u8 sc5, bool is_fecn,
					u64 *pbc_flags, u32 *hwords,
					u32 *nwords)
{
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct hfi1_16b_header *hdr = &opa_hdr->opah;
	struct ib_other_headers *ohdr;
	u32 bth0, bth1 = 0;
	u16 len, pkey;
	bool becn = is_fecn;
	u8 l4 = OPA_16B_L4_IB_LOCAL;
	u8 extra_bytes;

	opa_hdr->hdr_type = HFI1_PKT_TYPE_16B;
	ohdr = &hdr->u.oth;
	/* header size in 32-bit words 16B LRH+BTH+AETH = (16+12+4)/4 */
	*hwords = 8;
	extra_bytes = hfi1_get_16b_padding(*hwords << 2, 0);
	*nwords = SIZE_OF_CRC + ((extra_bytes + SIZE_OF_LT) >> 2);

	if (unlikely(rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH) &&
	    hfi1_check_mcast(rdma_ah_get_dlid(&qp->remote_ah_attr))) {
		*hwords += hfi1_make_grh(ibp, &hdr->u.l.grh,
					 rdma_ah_read_grh(&qp->remote_ah_attr),
					 *hwords - 4, *nwords);
		ohdr = &hdr->u.l.oth;
		l4 = OPA_16B_L4_IB_GLOBAL;
	}
	*pbc_flags |= PBC_PACKET_BYPASS | PBC_INSERT_BYPASS_ICRC;

	/* read pkey_index w/o lock (its atomic) */
	pkey = hfi1_get_pkey(ibp, qp->s_pkey_index);

	/* Convert dwords to flits */
	len = (*hwords + *nwords) >> 1;

	hfi1_make_16b_hdr(hdr, ppd->lid |
			  (rdma_ah_get_path_bits(&qp->remote_ah_attr) &
			  ((1 << ppd->lmc) - 1)),
			  opa_get_lid(rdma_ah_get_dlid(&qp->remote_ah_attr),
				      16B), len, pkey, becn, 0, l4, sc5);

	bth0 = pkey | (OP(ACKNOWLEDGE) << 24);
	bth0 |= extra_bytes << 20;
	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth1 = OPA_BTH_MIG_REQ;
	hfi1_make_bth_aeth(qp, ohdr, bth0, bth1);
}

typedef void (*hfi1_make_rc_ack)(struct hfi1_packet *packet,
				 struct hfi1_opa_header *opa_hdr,
				 u8 sc5, bool is_fecn,
				 u64 *pbc_flags, u32 *hwords,
				 u32 *nwords);

/* We support only two types - 9B and 16B for now */
static const hfi1_make_rc_ack hfi1_make_rc_ack_tbl[2] = {
	[HFI1_PKT_TYPE_9B] = &hfi1_make_rc_ack_9B,
	[HFI1_PKT_TYPE_16B] = &hfi1_make_rc_ack_16B
};

/**
 * hfi1_send_rc_ack - Construct an ACK packet and send it
 * @qp: a pointer to the QP
 *
 * This is called from hfi1_rc_rcv() and handle_receive_interrupt().
 * Note that RDMA reads and atomics are handled in the
 * send side QP state and send engine.
 */
void hfi1_send_rc_ack(struct hfi1_packet *packet, bool is_fecn)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	u8 sc5 = ibp->sl_to_sc[rdma_ah_get_sl(&qp->remote_ah_attr)];
	u64 pbc, pbc_flags = 0;
	u32 hwords = 0;
	u32 nwords = 0;
	u32 plen;
	struct pio_buf *pbuf;
	struct hfi1_opa_header opa_hdr;

	/* clear the defer count */
	qp->r_adefered = 0;

	/* Don't send ACK or NAK if a RDMA read or atomic is pending. */
	if (qp->s_flags & RVT_S_RESP_PENDING) {
		hfi1_queue_rc_ack(packet, is_fecn);
		return;
	}

	/* Ensure s_rdma_ack_cnt changes are committed */
	if (qp->s_rdma_ack_cnt) {
		hfi1_queue_rc_ack(packet, is_fecn);
		return;
	}

	/* Don't try to send ACKs if the link isn't ACTIVE */
	if (driver_lstate(ppd) != IB_PORT_ACTIVE)
		return;

	/* Make the appropriate header */
	hfi1_make_rc_ack_tbl[priv->hdr_type](packet, &opa_hdr, sc5, is_fecn,
					     &pbc_flags, &hwords, &nwords);

	plen = 2 /* PBC */ + hwords + nwords;
	pbc = create_pbc(ppd, pbc_flags, qp->srate_mbps,
			 sc_to_vlt(ppd->dd, sc5), plen);
	pbuf = sc_buffer_alloc(rcd->sc, plen, NULL, NULL);
	if (!pbuf) {
		/*
		 * We have no room to send at the moment.  Pass
		 * responsibility for sending the ACK to the send engine
		 * so that when enough buffer space becomes available,
		 * the ACK is sent ahead of other outgoing packets.
		 */
		hfi1_queue_rc_ack(packet, is_fecn);
		return;
	}
	trace_ack_output_ibhdr(dd_from_ibdev(qp->ibqp.device),
			       &opa_hdr, ib_is_sc5(sc5));

	/* write the pbc and data */
	ppd->dd->pio_inline_send(ppd->dd, pbuf, pbc,
				 (priv->hdr_type == HFI1_PKT_TYPE_9B ?
				 (void *)&opa_hdr.ibh :
				 (void *)&opa_hdr.opah), hwords);
	return;
}

/**
 * reset_psn - reset the QP state to send starting from PSN
 * @qp: the QP
 * @psn: the packet sequence number to restart at
 *
 * This is called from hfi1_rc_rcv() to process an incoming RC ACK
 * for the given QP.
 * Called at interrupt level with the QP s_lock held.
 */
static void reset_psn(struct rvt_qp *qp, u32 psn)
{
	u32 n = qp->s_acked;
	struct rvt_swqe *wqe = rvt_get_swqe_ptr(qp, n);
	u32 opcode;

	lockdep_assert_held(&qp->s_lock);
	qp->s_cur = n;

	/*
	 * If we are starting the request from the beginning,
	 * let the normal send code handle initialization.
	 */
	if (cmp_psn(psn, wqe->psn) <= 0) {
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
		wqe = rvt_get_swqe_ptr(qp, n);
		diff = cmp_psn(psn, wqe->psn);
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
	 * See hfi1_make_rc_req().
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
	 * Set RVT_S_WAIT_PSN as rc_complete() may start the timer
	 * asynchronously before the send engine can get scheduled.
	 * Doing it in hfi1_make_rc_req() is too late.
	 */
	if ((cmp_psn(qp->s_psn, qp->s_sending_hpsn) <= 0) &&
	    (cmp_psn(qp->s_sending_psn, qp->s_sending_hpsn) <= 0))
		qp->s_flags |= RVT_S_WAIT_PSN;
	qp->s_flags &= ~HFI1_S_AHG_VALID;
}

/*
 * Back up requester to resend the last un-ACKed request.
 * The QP r_lock and s_lock should be held and interrupts disabled.
 */
void hfi1_restart_rc(struct rvt_qp *qp, u32 psn, int wait)
{
	struct rvt_swqe *wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
	struct hfi1_ibport *ibp;

	lockdep_assert_held(&qp->r_lock);
	lockdep_assert_held(&qp->s_lock);
	if (qp->s_retry == 0) {
		if (qp->s_mig_state == IB_MIG_ARMED) {
			hfi1_migrate_qp(qp);
			qp->s_retry = qp->s_retry_cnt;
		} else if (qp->s_last == qp->s_acked) {
			hfi1_send_complete(qp, wqe, IB_WC_RETRY_EXC_ERR);
			rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);
			return;
		} else { /* need to handle delayed completion */
			return;
		}
	} else {
		qp->s_retry--;
	}

	ibp = to_iport(qp->ibqp.device, qp->port_num);
	if (wqe->wr.opcode == IB_WR_RDMA_READ)
		ibp->rvp.n_rc_resends++;
	else
		ibp->rvp.n_rc_resends += delta_psn(qp->s_psn, psn);

	qp->s_flags &= ~(RVT_S_WAIT_FENCE | RVT_S_WAIT_RDMAR |
			 RVT_S_WAIT_SSN_CREDIT | RVT_S_WAIT_PSN |
			 RVT_S_WAIT_ACK);
	if (wait)
		qp->s_flags |= RVT_S_SEND_ONE;
	reset_psn(qp, psn);
}

/*
 * Set qp->s_sending_psn to the next PSN after the given one.
 * This would be psn+1 except when RDMA reads are present.
 */
static void reset_sending_psn(struct rvt_qp *qp, u32 psn)
{
	struct rvt_swqe *wqe;
	u32 n = qp->s_last;

	lockdep_assert_held(&qp->s_lock);
	/* Find the work request corresponding to the given PSN. */
	for (;;) {
		wqe = rvt_get_swqe_ptr(qp, n);
		if (cmp_psn(psn, wqe->lpsn) <= 0) {
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
void hfi1_rc_send_complete(struct rvt_qp *qp, struct hfi1_opa_header *opah)
{
	struct ib_other_headers *ohdr;
	struct hfi1_qp_priv *priv = qp->priv;
	struct rvt_swqe *wqe;
	struct ib_header *hdr = NULL;
	struct hfi1_16b_header *hdr_16b = NULL;
	u32 opcode;
	u32 psn;

	lockdep_assert_held(&qp->s_lock);
	if (!(ib_rvt_state_ops[qp->state] & RVT_SEND_OR_FLUSH_OR_RECV_OK))
		return;

	/* Find out where the BTH is */
	if (priv->hdr_type == HFI1_PKT_TYPE_9B) {
		hdr = &opah->ibh;
		if (ib_get_lnh(hdr) == HFI1_LRH_BTH)
			ohdr = &hdr->u.oth;
		else
			ohdr = &hdr->u.l.oth;
	} else {
		u8 l4;

		hdr_16b = &opah->opah;
		l4  = hfi1_16B_get_l4(hdr_16b);
		if (l4 == OPA_16B_L4_IB_LOCAL)
			ohdr = &hdr_16b->u.oth;
		else
			ohdr = &hdr_16b->u.l.oth;
	}

	opcode = ib_bth_get_opcode(ohdr);
	if (opcode >= OP(RDMA_READ_RESPONSE_FIRST) &&
	    opcode <= OP(ATOMIC_ACKNOWLEDGE)) {
		WARN_ON(!qp->s_rdma_ack_cnt);
		qp->s_rdma_ack_cnt--;
		return;
	}

	psn = ib_bth_get_psn(ohdr);
	reset_sending_psn(qp, psn);

	/*
	 * Start timer after a packet requesting an ACK has been sent and
	 * there are still requests that haven't been acked.
	 */
	if ((psn & IB_BTH_REQ_ACK) && qp->s_acked != qp->s_tail &&
	    !(qp->s_flags &
		(RVT_S_TIMER | RVT_S_WAIT_RNR | RVT_S_WAIT_PSN)) &&
		(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK))
		rvt_add_retry_timer(qp);

	while (qp->s_last != qp->s_acked) {
		u32 s_last;

		wqe = rvt_get_swqe_ptr(qp, qp->s_last);
		if (cmp_psn(wqe->lpsn, qp->s_sending_psn) >= 0 &&
		    cmp_psn(qp->s_sending_psn, qp->s_sending_hpsn) <= 0)
			break;
		rvt_qp_wqe_unreserve(qp, wqe);
		s_last = qp->s_last;
		trace_hfi1_qp_send_completion(qp, wqe, s_last);
		if (++s_last >= qp->s_size)
			s_last = 0;
		qp->s_last = s_last;
		/* see post_send() */
		barrier();
		rvt_put_swqe(wqe);
		rvt_qp_swqe_complete(qp,
				     wqe,
				     ib_hfi1_wc_opcode[wqe->wr.opcode],
				     IB_WC_SUCCESS);
	}
	/*
	 * If we were waiting for sends to complete before re-sending,
	 * and they are now complete, restart sending.
	 */
	trace_hfi1_sendcomplete(qp, psn);
	if (qp->s_flags & RVT_S_WAIT_PSN &&
	    cmp_psn(qp->s_sending_psn, qp->s_sending_hpsn) > 0) {
		qp->s_flags &= ~RVT_S_WAIT_PSN;
		qp->s_sending_psn = qp->s_psn;
		qp->s_sending_hpsn = qp->s_psn - 1;
		hfi1_schedule_send(qp);
	}
}

static inline void update_last_psn(struct rvt_qp *qp, u32 psn)
{
	qp->s_last_psn = psn;
}

/*
 * Generate a SWQE completion.
 * This is similar to hfi1_send_complete but has to check to be sure
 * that the SGEs are not being referenced if the SWQE is being resent.
 */
static struct rvt_swqe *do_rc_completion(struct rvt_qp *qp,
					 struct rvt_swqe *wqe,
					 struct hfi1_ibport *ibp)
{
	lockdep_assert_held(&qp->s_lock);
	/*
	 * Don't decrement refcount and don't generate a
	 * completion if the SWQE is being resent until the send
	 * is finished.
	 */
	if (cmp_psn(wqe->lpsn, qp->s_sending_psn) < 0 ||
	    cmp_psn(qp->s_sending_psn, qp->s_sending_hpsn) > 0) {
		u32 s_last;

		rvt_put_swqe(wqe);
		rvt_qp_wqe_unreserve(qp, wqe);
		s_last = qp->s_last;
		trace_hfi1_qp_send_completion(qp, wqe, s_last);
		if (++s_last >= qp->s_size)
			s_last = 0;
		qp->s_last = s_last;
		/* see post_send() */
		barrier();
		rvt_qp_swqe_complete(qp,
				     wqe,
				     ib_hfi1_wc_opcode[wqe->wr.opcode],
				     IB_WC_SUCCESS);
	} else {
		struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

		this_cpu_inc(*ibp->rvp.rc_delayed_comp);
		/*
		 * If send progress not running attempt to progress
		 * SDMA queue.
		 */
		if (ppd->dd->flags & HFI1_HAS_SEND_DMA) {
			struct sdma_engine *engine;
			u8 sl = rdma_ah_get_sl(&qp->remote_ah_attr);
			u8 sc5;

			/* For now use sc to find engine */
			sc5 = ibp->sl_to_sc[sl];
			engine = qp_to_sdma_engine(qp, sc5);
			sdma_engine_progress_schedule(engine);
		}
	}

	qp->s_retry = qp->s_retry_cnt;
	update_last_psn(qp, wqe->lpsn);

	/*
	 * If we are completing a request which is in the process of
	 * being resent, we can stop re-sending it since we know the
	 * responder has already seen it.
	 */
	if (qp->s_acked == qp->s_cur) {
		if (++qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		qp->s_acked = qp->s_cur;
		wqe = rvt_get_swqe_ptr(qp, qp->s_cur);
		if (qp->s_acked != qp->s_tail) {
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = wqe->psn;
		}
	} else {
		if (++qp->s_acked >= qp->s_size)
			qp->s_acked = 0;
		if (qp->state == IB_QPS_SQD && qp->s_acked == qp->s_cur)
			qp->s_draining = 0;
		wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
	}
	return wqe;
}

/**
 * do_rc_ack - process an incoming RC ACK
 * @qp: the QP the ACK came in on
 * @psn: the packet sequence number of the ACK
 * @opcode: the opcode of the request that resulted in the ACK
 *
 * This is called from rc_rcv_resp() to process an incoming RC ACK
 * for the given QP.
 * May be called at interrupt level, with the QP s_lock held.
 * Returns 1 if OK, 0 if current operation should be aborted (NAK).
 */
static int do_rc_ack(struct rvt_qp *qp, u32 aeth, u32 psn, int opcode,
		     u64 val, struct hfi1_ctxtdata *rcd)
{
	struct hfi1_ibport *ibp;
	enum ib_wc_status status;
	struct rvt_swqe *wqe;
	int ret = 0;
	u32 ack_psn;
	int diff;

	lockdep_assert_held(&qp->s_lock);
	/*
	 * Note that NAKs implicitly ACK outstanding SEND and RDMA write
	 * requests and implicitly NAK RDMA read and atomic requests issued
	 * before the NAK'ed request.  The MSN won't include the NAK'ed
	 * request but will include an ACK'ed request(s).
	 */
	ack_psn = psn;
	if (aeth >> IB_AETH_NAK_SHIFT)
		ack_psn--;
	wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
	ibp = rcd_to_iport(rcd);

	/*
	 * The MSN might be for a later WQE than the PSN indicates so
	 * only complete WQEs that the PSN finishes.
	 */
	while ((diff = delta_psn(ack_psn, wqe->lpsn)) >= 0) {
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
			goto bail_stop;
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
			if (!(qp->r_flags & RVT_R_RDMAR_SEQ)) {
				qp->r_flags |= RVT_R_RDMAR_SEQ;
				hfi1_restart_rc(qp, qp->s_last_psn + 1, 0);
				if (list_empty(&qp->rspwait)) {
					qp->r_flags |= RVT_R_RSP_SEND;
					rvt_get_qp(qp);
					list_add_tail(&qp->rspwait,
						      &rcd->qp_wait_list);
				}
			}
			/*
			 * No need to process the ACK/NAK since we are
			 * restarting an earlier request.
			 */
			goto bail_stop;
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
			if ((qp->s_flags & RVT_S_WAIT_FENCE) &&
			    !qp->s_num_rd_atomic) {
				qp->s_flags &= ~(RVT_S_WAIT_FENCE |
						 RVT_S_WAIT_ACK);
				hfi1_schedule_send(qp);
			} else if (qp->s_flags & RVT_S_WAIT_RDMAR) {
				qp->s_flags &= ~(RVT_S_WAIT_RDMAR |
						 RVT_S_WAIT_ACK);
				hfi1_schedule_send(qp);
			}
		}
		wqe = do_rc_completion(qp, wqe, ibp);
		if (qp->s_acked == qp->s_tail)
			break;
	}

	switch (aeth >> IB_AETH_NAK_SHIFT) {
	case 0:         /* ACK */
		this_cpu_inc(*ibp->rvp.rc_acks);
		if (qp->s_acked != qp->s_tail) {
			/*
			 * We are expecting more ACKs so
			 * mod the retry timer.
			 */
			rvt_mod_retry_timer(qp);
			/*
			 * We can stop re-sending the earlier packets and
			 * continue with the next packet the receiver wants.
			 */
			if (cmp_psn(qp->s_psn, psn) <= 0)
				reset_psn(qp, psn + 1);
		} else {
			/* No more acks - kill all timers */
			rvt_stop_rc_timers(qp);
			if (cmp_psn(qp->s_psn, psn) <= 0) {
				qp->s_state = OP(SEND_LAST);
				qp->s_psn = psn + 1;
			}
		}
		if (qp->s_flags & RVT_S_WAIT_ACK) {
			qp->s_flags &= ~RVT_S_WAIT_ACK;
			hfi1_schedule_send(qp);
		}
		rvt_get_credit(qp, aeth);
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		qp->s_retry = qp->s_retry_cnt;
		update_last_psn(qp, psn);
		return 1;

	case 1:         /* RNR NAK */
		ibp->rvp.n_rnr_naks++;
		if (qp->s_acked == qp->s_tail)
			goto bail_stop;
		if (qp->s_flags & RVT_S_WAIT_RNR)
			goto bail_stop;
		if (qp->s_rnr_retry == 0) {
			status = IB_WC_RNR_RETRY_EXC_ERR;
			goto class_b;
		}
		if (qp->s_rnr_retry_cnt < 7)
			qp->s_rnr_retry--;

		/* The last valid PSN is the previous PSN. */
		update_last_psn(qp, psn - 1);

		ibp->rvp.n_rc_resends += delta_psn(qp->s_psn, psn);

		reset_psn(qp, psn);

		qp->s_flags &= ~(RVT_S_WAIT_SSN_CREDIT | RVT_S_WAIT_ACK);
		rvt_stop_rc_timers(qp);
		rvt_add_rnr_timer(qp, aeth);
		return 0;

	case 3:         /* NAK */
		if (qp->s_acked == qp->s_tail)
			goto bail_stop;
		/* The last valid PSN is the previous PSN. */
		update_last_psn(qp, psn - 1);
		switch ((aeth >> IB_AETH_CREDIT_SHIFT) &
			IB_AETH_CREDIT_MASK) {
		case 0: /* PSN sequence error */
			ibp->rvp.n_seq_naks++;
			/*
			 * Back up to the responder's expected PSN.
			 * Note that we might get a NAK in the middle of an
			 * RDMA READ response which terminates the RDMA
			 * READ.
			 */
			hfi1_restart_rc(qp, psn, 0);
			hfi1_schedule_send(qp);
			break;

		case 1: /* Invalid Request */
			status = IB_WC_REM_INV_REQ_ERR;
			ibp->rvp.n_other_naks++;
			goto class_b;

		case 2: /* Remote Access Error */
			status = IB_WC_REM_ACCESS_ERR;
			ibp->rvp.n_other_naks++;
			goto class_b;

		case 3: /* Remote Operation Error */
			status = IB_WC_REM_OP_ERR;
			ibp->rvp.n_other_naks++;
class_b:
			if (qp->s_last == qp->s_acked) {
				hfi1_send_complete(qp, wqe, status);
				rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);
			}
			break;

		default:
			/* Ignore other reserved NAK error codes */
			goto reserved;
		}
		qp->s_retry = qp->s_retry_cnt;
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		goto bail_stop;

	default:                /* 2: reserved */
reserved:
		/* Ignore reserved NAK codes. */
		goto bail_stop;
	}
	/* cannot be reached  */
bail_stop:
	rvt_stop_rc_timers(qp);
	return ret;
}

/*
 * We have seen an out of sequence RDMA read middle or last packet.
 * This ACKs SENDs and RDMA writes up to the first RDMA read or atomic SWQE.
 */
static void rdma_seq_err(struct rvt_qp *qp, struct hfi1_ibport *ibp, u32 psn,
			 struct hfi1_ctxtdata *rcd)
{
	struct rvt_swqe *wqe;

	lockdep_assert_held(&qp->s_lock);
	/* Remove QP from retry timer */
	rvt_stop_rc_timers(qp);

	wqe = rvt_get_swqe_ptr(qp, qp->s_acked);

	while (cmp_psn(psn, wqe->lpsn) > 0) {
		if (wqe->wr.opcode == IB_WR_RDMA_READ ||
		    wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		    wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD)
			break;
		wqe = do_rc_completion(qp, wqe, ibp);
	}

	ibp->rvp.n_rdma_seq++;
	qp->r_flags |= RVT_R_RDMAR_SEQ;
	hfi1_restart_rc(qp, qp->s_last_psn + 1, 0);
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= RVT_R_RSP_SEND;
		rvt_get_qp(qp);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
}

/**
 * rc_rcv_resp - process an incoming RC response packet
 * @packet: data packet information
 *
 * This is called from hfi1_rc_rcv() to process an incoming RC response
 * packet for the given QP.
 * Called at interrupt level.
 */
static void rc_rcv_resp(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	void *data = packet->payload;
	u32 tlen = packet->tlen;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp;
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_swqe *wqe;
	enum ib_wc_status status;
	unsigned long flags;
	int diff;
	u64 val;
	u32 aeth;
	u32 psn = ib_bth_get_psn(packet->ohdr);
	u32 pmtu = qp->pmtu;
	u16 hdrsize = packet->hlen;
	u8 opcode = packet->opcode;
	u8 pad = packet->pad;
	u8 extra_bytes = pad + packet->extra_byte + (SIZE_OF_CRC << 2);

	spin_lock_irqsave(&qp->s_lock, flags);
	trace_hfi1_ack(qp, psn);

	/* Ignore invalid responses. */
	if (cmp_psn(psn, READ_ONCE(qp->s_next_psn)) >= 0)
		goto ack_done;

	/* Ignore duplicate responses. */
	diff = cmp_psn(psn, qp->s_last_psn);
	if (unlikely(diff <= 0)) {
		/* Update credits for "ghost" ACKs */
		if (diff == 0 && opcode == OP(ACKNOWLEDGE)) {
			aeth = be32_to_cpu(ohdr->u.aeth);
			if ((aeth >> IB_AETH_NAK_SHIFT) == 0)
				rvt_get_credit(qp, aeth);
		}
		goto ack_done;
	}

	/*
	 * Skip everything other than the PSN we expect, if we are waiting
	 * for a reply to a restarted RDMA read or atomic op.
	 */
	if (qp->r_flags & RVT_R_RDMAR_SEQ) {
		if (cmp_psn(psn, qp->s_last_psn + 1) != 0)
			goto ack_done;
		qp->r_flags &= ~RVT_R_RDMAR_SEQ;
	}

	if (unlikely(qp->s_acked == qp->s_tail))
		goto ack_done;
	wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
	status = IB_WC_SUCCESS;

	switch (opcode) {
	case OP(ACKNOWLEDGE):
	case OP(ATOMIC_ACKNOWLEDGE):
	case OP(RDMA_READ_RESPONSE_FIRST):
		aeth = be32_to_cpu(ohdr->u.aeth);
		if (opcode == OP(ATOMIC_ACKNOWLEDGE))
			val = ib_u64_get(&ohdr->u.at.atomic_ack_eth);
		else
			val = 0;
		if (!do_rc_ack(qp, aeth, psn, opcode, val, rcd) ||
		    opcode != OP(RDMA_READ_RESPONSE_FIRST))
			goto ack_done;
		wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
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
		if (unlikely(cmp_psn(psn, qp->s_last_psn + 1)))
			goto ack_seq_err;
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
read_middle:
		if (unlikely(tlen != (hdrsize + pmtu + extra_bytes)))
			goto ack_len_err;
		if (unlikely(pmtu >= qp->s_rdma_read_len))
			goto ack_len_err;

		/*
		 * We got a response so update the timeout.
		 * 4.096 usec. * (1 << qp->timeout)
		 */
		rvt_mod_retry_timer(qp);
		if (qp->s_flags & RVT_S_WAIT_ACK) {
			qp->s_flags &= ~RVT_S_WAIT_ACK;
			hfi1_schedule_send(qp);
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
		hfi1_copy_sge(&qp->s_rdma_read_sge, data, pmtu, false, false);
		goto bail;

	case OP(RDMA_READ_RESPONSE_ONLY):
		aeth = be32_to_cpu(ohdr->u.aeth);
		if (!do_rc_ack(qp, aeth, psn, opcode, 0, rcd))
			goto ack_done;
		/*
		 * Check that the data size is >= 0 && <= pmtu.
		 * Remember to account for ICRC (4).
		 */
		if (unlikely(tlen < (hdrsize + extra_bytes)))
			goto ack_len_err;
		/*
		 * If this is a response to a resent RDMA read, we
		 * have to be careful to copy the data to the right
		 * location.
		 */
		wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
		qp->s_rdma_read_len = restart_sge(&qp->s_rdma_read_sge,
						  wqe, psn, pmtu);
		goto read_last;

	case OP(RDMA_READ_RESPONSE_LAST):
		/* ACKs READ req. */
		if (unlikely(cmp_psn(psn, qp->s_last_psn + 1)))
			goto ack_seq_err;
		if (unlikely(wqe->wr.opcode != IB_WR_RDMA_READ))
			goto ack_op_err;
		/*
		 * Check that the data size is >= 1 && <= pmtu.
		 * Remember to account for ICRC (4).
		 */
		if (unlikely(tlen <= (hdrsize + extra_bytes)))
			goto ack_len_err;
read_last:
		tlen -= hdrsize + extra_bytes;
		if (unlikely(tlen != qp->s_rdma_read_len))
			goto ack_len_err;
		aeth = be32_to_cpu(ohdr->u.aeth);
		hfi1_copy_sge(&qp->s_rdma_read_sge, data, tlen, false, false);
		WARN_ON(qp->s_rdma_read_sge.num_sge);
		(void)do_rc_ack(qp, aeth, psn,
				 OP(RDMA_READ_RESPONSE_LAST), 0, rcd);
		goto ack_done;
	}

ack_op_err:
	status = IB_WC_LOC_QP_OP_ERR;
	goto ack_err;

ack_seq_err:
	ibp = rcd_to_iport(rcd);
	rdma_seq_err(qp, ibp, psn, rcd);
	goto ack_done;

ack_len_err:
	status = IB_WC_LOC_LEN_ERR;
ack_err:
	if (qp->s_last == qp->s_acked) {
		hfi1_send_complete(qp, wqe, status);
		rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);
	}
ack_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
bail:
	return;
}

static inline void rc_defered_ack(struct hfi1_ctxtdata *rcd,
				  struct rvt_qp *qp)
{
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= RVT_R_RSP_NAK;
		rvt_get_qp(qp);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
}

static inline void rc_cancel_ack(struct rvt_qp *qp)
{
	qp->r_adefered = 0;
	if (list_empty(&qp->rspwait))
		return;
	list_del_init(&qp->rspwait);
	qp->r_flags &= ~RVT_R_RSP_NAK;
	rvt_put_qp(qp);
}

/**
 * rc_rcv_error - process an incoming duplicate or error RC packet
 * @ohdr: the other headers for this packet
 * @data: the packet data
 * @qp: the QP for this packet
 * @opcode: the opcode for this packet
 * @psn: the packet sequence number for this packet
 * @diff: the difference between the PSN and the expected PSN
 *
 * This is called from hfi1_rc_rcv() to process an unexpected
 * incoming RC packet for the given QP.
 * Called at interrupt level.
 * Return 1 if no more processing is needed; otherwise return 0 to
 * schedule a response to be sent.
 */
static noinline int rc_rcv_error(struct ib_other_headers *ohdr, void *data,
				 struct rvt_qp *qp, u32 opcode, u32 psn,
				 int diff, struct hfi1_ctxtdata *rcd)
{
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	struct rvt_ack_entry *e;
	unsigned long flags;
	u8 i, prev;
	int old_req;

	trace_hfi1_rcv_error(qp, psn);
	if (diff > 0) {
		/*
		 * Packet sequence error.
		 * A NAK will ACK earlier sends and RDMA writes.
		 * Don't queue the NAK if we already sent one.
		 */
		if (!qp->r_nak_state) {
			ibp->rvp.n_rc_seqnak++;
			qp->r_nak_state = IB_NAK_PSN_ERROR;
			/* Use the expected PSN. */
			qp->r_ack_psn = qp->r_psn;
			/*
			 * Wait to send the sequence NAK until all packets
			 * in the receive queue have been processed.
			 * Otherwise, we end up propagating congestion.
			 */
			rc_defered_ack(rcd, qp);
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
	ibp->rvp.n_rc_dupreq++;

	spin_lock_irqsave(&qp->s_lock, flags);

	for (i = qp->r_head_ack_queue; ; i = prev) {
		if (i == qp->s_tail_ack_queue)
			old_req = 0;
		if (i)
			prev = i - 1;
		else
			prev = HFI1_MAX_RDMA_ATOMIC;
		if (prev == qp->r_head_ack_queue) {
			e = NULL;
			break;
		}
		e = &qp->s_ack_queue[prev];
		if (!e->opcode) {
			e = NULL;
			break;
		}
		if (cmp_psn(psn, e->psn) >= 0) {
			if (prev == qp->s_tail_ack_queue &&
			    cmp_psn(psn, e->lpsn) <= 0)
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
		offset = delta_psn(psn, e->psn) * qp->pmtu;
		len = be32_to_cpu(reth->length);
		if (unlikely(offset + len != e->rdma_sge.sge_length))
			goto unlock_done;
		if (e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
		if (len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = get_ib_reth_vaddr(reth);
			int ok;

			ok = rvt_rkey_ok(qp, &e->rdma_sge, len, vaddr, rkey,
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
		 * or the send engine is already backed up to send an
		 * earlier entry, we can ignore this request.
		 */
		if (!e || e->opcode != (u8)opcode || old_req)
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
		 * Resend the RDMA read or atomic op which
		 * ACKs this duplicate request.
		 */
		qp->s_tail_ack_queue = i;
		break;
	}
	qp->s_ack_state = OP(ACKNOWLEDGE);
	qp->s_flags |= RVT_S_RESP_PENDING;
	qp->r_nak_state = 0;
	hfi1_schedule_send(qp);

unlock_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
done:
	return 1;

send_ack:
	return 0;
}

static inline void update_ack_queue(struct rvt_qp *qp, unsigned n)
{
	unsigned next;

	next = n + 1;
	if (next > HFI1_MAX_RDMA_ATOMIC)
		next = 0;
	qp->s_tail_ack_queue = next;
	qp->s_ack_state = OP(ACKNOWLEDGE);
}

static void log_cca_event(struct hfi1_pportdata *ppd, u8 sl, u32 rlid,
			  u32 lqpn, u32 rqpn, u8 svc_type)
{
	struct opa_hfi1_cong_log_event_internal *cc_event;
	unsigned long flags;

	if (sl >= OPA_MAX_SLS)
		return;

	spin_lock_irqsave(&ppd->cc_log_lock, flags);

	ppd->threshold_cong_event_map[sl / 8] |= 1 << (sl % 8);
	ppd->threshold_event_counter++;

	cc_event = &ppd->cc_events[ppd->cc_log_idx++];
	if (ppd->cc_log_idx == OPA_CONG_LOG_ELEMS)
		ppd->cc_log_idx = 0;
	cc_event->lqpn = lqpn & RVT_QPN_MASK;
	cc_event->rqpn = rqpn & RVT_QPN_MASK;
	cc_event->sl = sl;
	cc_event->svc_type = svc_type;
	cc_event->rlid = rlid;
	/* keep timestamp in units of 1.024 usec */
	cc_event->timestamp = ktime_get_ns() / 1024;

	spin_unlock_irqrestore(&ppd->cc_log_lock, flags);
}

void process_becn(struct hfi1_pportdata *ppd, u8 sl, u32 rlid, u32 lqpn,
		  u32 rqpn, u8 svc_type)
{
	struct cca_timer *cca_timer;
	u16 ccti, ccti_incr, ccti_timer, ccti_limit;
	u8 trigger_threshold;
	struct cc_state *cc_state;
	unsigned long flags;

	if (sl >= OPA_MAX_SLS)
		return;

	cc_state = get_cc_state(ppd);

	if (!cc_state)
		return;

	/*
	 * 1) increase CCTI (for this SL)
	 * 2) select IPG (i.e., call set_link_ipg())
	 * 3) start timer
	 */
	ccti_limit = cc_state->cct.ccti_limit;
	ccti_incr = cc_state->cong_setting.entries[sl].ccti_increase;
	ccti_timer = cc_state->cong_setting.entries[sl].ccti_timer;
	trigger_threshold =
		cc_state->cong_setting.entries[sl].trigger_threshold;

	spin_lock_irqsave(&ppd->cca_timer_lock, flags);

	cca_timer = &ppd->cca_timer[sl];
	if (cca_timer->ccti < ccti_limit) {
		if (cca_timer->ccti + ccti_incr <= ccti_limit)
			cca_timer->ccti += ccti_incr;
		else
			cca_timer->ccti = ccti_limit;
		set_link_ipg(ppd);
	}

	ccti = cca_timer->ccti;

	if (!hrtimer_active(&cca_timer->hrtimer)) {
		/* ccti_timer is in units of 1.024 usec */
		unsigned long nsec = 1024 * ccti_timer;

		hrtimer_start(&cca_timer->hrtimer, ns_to_ktime(nsec),
			      HRTIMER_MODE_REL_PINNED);
	}

	spin_unlock_irqrestore(&ppd->cca_timer_lock, flags);

	if ((trigger_threshold != 0) && (ccti >= trigger_threshold))
		log_cca_event(ppd, sl, rlid, lqpn, rqpn, svc_type);
}

/**
 * hfi1_rc_rcv - process an incoming RC packet
 * @packet: data packet information
 *
 * This is called from qp_rcv() to process an incoming RC packet
 * for the given QP.
 * May be called at interrupt level.
 */
void hfi1_rc_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	void *data = packet->payload;
	u32 tlen = packet->tlen;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	struct ib_other_headers *ohdr = packet->ohdr;
	u32 opcode = packet->opcode;
	u32 hdrsize = packet->hlen;
	u32 psn = ib_bth_get_psn(packet->ohdr);
	u32 pad = packet->pad;
	struct ib_wc wc;
	u32 pmtu = qp->pmtu;
	int diff;
	struct ib_reth *reth;
	unsigned long flags;
	int ret;
	bool is_fecn = false;
	bool copy_last = false;
	u32 rkey;
	u8 extra_bytes = pad + packet->extra_byte + (SIZE_OF_CRC << 2);

	lockdep_assert_held(&qp->r_lock);

	if (hfi1_ruc_check_hdr(ibp, packet))
		return;

	is_fecn = process_ecn(qp, packet, false);

	/*
	 * Process responses (ACKs) before anything else.  Note that the
	 * packet sequence number will be for something in the send work
	 * queue rather than the expected receive packet sequence number.
	 * In other words, this QP is the requester.
	 */
	if (opcode >= OP(RDMA_READ_RESPONSE_FIRST) &&
	    opcode <= OP(ATOMIC_ACKNOWLEDGE)) {
		rc_rcv_resp(packet);
		if (is_fecn)
			goto send_ack;
		return;
	}

	/* Compute 24 bits worth of difference. */
	diff = delta_psn(psn, qp->r_psn);
	if (unlikely(diff)) {
		if (rc_rcv_error(ohdr, data, qp, opcode, psn, diff, rcd))
			return;
		goto send_ack;
	}

	/* Check for opcode sequence errors. */
	switch (qp->r_state) {
	case OP(SEND_FIRST):
	case OP(SEND_MIDDLE):
		if (opcode == OP(SEND_MIDDLE) ||
		    opcode == OP(SEND_LAST) ||
		    opcode == OP(SEND_LAST_WITH_IMMEDIATE) ||
		    opcode == OP(SEND_LAST_WITH_INVALIDATE))
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
		    opcode == OP(SEND_LAST_WITH_INVALIDATE) ||
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

	if (qp->state == IB_QPS_RTR && !(qp->r_flags & RVT_R_COMM_EST))
		rvt_comm_est(qp);

	/* OK, process the packet. */
	switch (opcode) {
	case OP(SEND_FIRST):
		ret = rvt_get_rwqe(qp, false);
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
		/*
		 * There will be no padding for 9B packet but 16B packets
		 * will come in with some padding since we always add
		 * CRC and LT bytes which will need to be flit aligned
		 */
		if (unlikely(tlen != (hdrsize + pmtu + extra_bytes)))
			goto nack_inv;
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len))
			goto nack_inv;
		hfi1_copy_sge(&qp->r_sge, data, pmtu, true, false);
		break;

	case OP(RDMA_WRITE_LAST_WITH_IMMEDIATE):
		/* consume RWQE */
		ret = rvt_get_rwqe(qp, true);
		if (ret < 0)
			goto nack_op_err;
		if (!ret)
			goto rnr_nak;
		goto send_last_imm;

	case OP(SEND_ONLY):
	case OP(SEND_ONLY_WITH_IMMEDIATE):
	case OP(SEND_ONLY_WITH_INVALIDATE):
		ret = rvt_get_rwqe(qp, false);
		if (ret < 0)
			goto nack_op_err;
		if (!ret)
			goto rnr_nak;
		qp->r_rcv_len = 0;
		if (opcode == OP(SEND_ONLY))
			goto no_immediate_data;
		if (opcode == OP(SEND_ONLY_WITH_INVALIDATE))
			goto send_last_inv;
		/* FALLTHROUGH -- for SEND_ONLY_WITH_IMMEDIATE */
	case OP(SEND_LAST_WITH_IMMEDIATE):
send_last_imm:
		wc.ex.imm_data = ohdr->u.imm_data;
		wc.wc_flags = IB_WC_WITH_IMM;
		goto send_last;
	case OP(SEND_LAST_WITH_INVALIDATE):
send_last_inv:
		rkey = be32_to_cpu(ohdr->u.ieth);
		if (rvt_invalidate_rkey(qp, rkey))
			goto no_immediate_data;
		wc.ex.invalidate_rkey = rkey;
		wc.wc_flags = IB_WC_WITH_INVALIDATE;
		goto send_last;
	case OP(RDMA_WRITE_LAST):
		copy_last = rvt_is_user_qp(qp);
		/* fall through */
	case OP(SEND_LAST):
no_immediate_data:
		wc.wc_flags = 0;
		wc.ex.imm_data = 0;
send_last:
		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + extra_bytes)))
			goto nack_inv;
		/* Don't count the CRC(and padding and LT byte for 16B). */
		tlen -= (hdrsize + extra_bytes);
		wc.byte_len = tlen + qp->r_rcv_len;
		if (unlikely(wc.byte_len > qp->r_len))
			goto nack_inv;
		hfi1_copy_sge(&qp->r_sge, data, tlen, true, copy_last);
		rvt_put_ss(&qp->r_sge);
		qp->r_msn++;
		if (!__test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags))
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
		wc.slid = rdma_ah_get_dlid(&qp->remote_ah_attr) & U16_MAX;
		/*
		 * It seems that IB mandates the presence of an SL in a
		 * work completion only for the UD transport (see section
		 * 11.4.2 of IBTA Vol. 1).
		 *
		 * However, the way the SL is chosen below is consistent
		 * with the way that IB/qib works and is trying avoid
		 * introducing incompatibilities.
		 *
		 * See also OPA Vol. 1, section 9.7.6, and table 9-17.
		 */
		wc.sl = rdma_ah_get_sl(&qp->remote_ah_attr);
		/* zero fields that are N/A */
		wc.vendor_err = 0;
		wc.pkey_index = 0;
		wc.dlid_path_bits = 0;
		wc.port_num = 0;
		/* Signal completion event if the solicited bit is set. */
		rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc,
			     ib_bth_is_solicited(ohdr));
		break;

	case OP(RDMA_WRITE_ONLY):
		copy_last = rvt_is_user_qp(qp);
		/* fall through */
	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE):
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
			goto nack_inv;
		/* consume RWQE */
		reth = &ohdr->u.rc.reth;
		qp->r_len = be32_to_cpu(reth->length);
		qp->r_rcv_len = 0;
		qp->r_sge.sg_list = NULL;
		if (qp->r_len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = get_ib_reth_vaddr(reth);
			int ok;

			/* Check rkey & NAK */
			ok = rvt_rkey_ok(qp, &qp->r_sge.sge, qp->r_len, vaddr,
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
		ret = rvt_get_rwqe(qp, true);
		if (ret < 0)
			goto nack_op_err;
		if (!ret) {
			/* peer will send again */
			rvt_put_ss(&qp->r_sge);
			goto rnr_nak;
		}
		wc.ex.imm_data = ohdr->u.rc.imm_data;
		wc.wc_flags = IB_WC_WITH_IMM;
		goto send_last;

	case OP(RDMA_READ_REQUEST): {
		struct rvt_ack_entry *e;
		u32 len;
		u8 next;

		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_READ)))
			goto nack_inv;
		next = qp->r_head_ack_queue + 1;
		/* s_ack_queue is size HFI1_MAX_RDMA_ATOMIC+1 so use > not >= */
		if (next > HFI1_MAX_RDMA_ATOMIC)
			next = 0;
		spin_lock_irqsave(&qp->s_lock, flags);
		if (unlikely(next == qp->s_tail_ack_queue)) {
			if (!qp->s_ack_queue[next].sent)
				goto nack_inv_unlck;
			update_ack_queue(qp, next);
		}
		e = &qp->s_ack_queue[qp->r_head_ack_queue];
		if (e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
		reth = &ohdr->u.rc.reth;
		len = be32_to_cpu(reth->length);
		if (len) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = get_ib_reth_vaddr(reth);
			int ok;

			/* Check rkey & NAK */
			ok = rvt_rkey_ok(qp, &e->rdma_sge, len, vaddr,
					 rkey, IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok))
				goto nack_acc_unlck;
			/*
			 * Update the next expected PSN.  We add 1 later
			 * below, so only add the remainder here.
			 */
			qp->r_psn += rvt_div_mtu(qp, len - 1);
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

		/* Schedule the send engine. */
		qp->s_flags |= RVT_S_RESP_PENDING;
		hfi1_schedule_send(qp);

		spin_unlock_irqrestore(&qp->s_lock, flags);
		if (is_fecn)
			goto send_ack;
		return;
	}

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD): {
		struct ib_atomic_eth *ateth;
		struct rvt_ack_entry *e;
		u64 vaddr;
		atomic64_t *maddr;
		u64 sdata;
		u32 rkey;
		u8 next;

		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_inv;
		next = qp->r_head_ack_queue + 1;
		if (next > HFI1_MAX_RDMA_ATOMIC)
			next = 0;
		spin_lock_irqsave(&qp->s_lock, flags);
		if (unlikely(next == qp->s_tail_ack_queue)) {
			if (!qp->s_ack_queue[next].sent)
				goto nack_inv_unlck;
			update_ack_queue(qp, next);
		}
		e = &qp->s_ack_queue[qp->r_head_ack_queue];
		if (e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
		ateth = &ohdr->u.atomic_eth;
		vaddr = get_ib_ateth_vaddr(ateth);
		if (unlikely(vaddr & (sizeof(u64) - 1)))
			goto nack_inv_unlck;
		rkey = be32_to_cpu(ateth->rkey);
		/* Check rkey & NAK */
		if (unlikely(!rvt_rkey_ok(qp, &qp->r_sge.sge, sizeof(u64),
					  vaddr, rkey,
					  IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_acc_unlck;
		/* Perform atomic OP and save result. */
		maddr = (atomic64_t *)qp->r_sge.sge.vaddr;
		sdata = get_ib_ateth_swap(ateth);
		e->atomic_data = (opcode == OP(FETCH_ADD)) ?
			(u64)atomic64_add_return(sdata, maddr) - sdata :
			(u64)cmpxchg((u64 *)qp->r_sge.sge.vaddr,
				      get_ib_ateth_compare(ateth),
				      sdata);
		rvt_put_mr(qp->r_sge.sge.mr);
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

		/* Schedule the send engine. */
		qp->s_flags |= RVT_S_RESP_PENDING;
		hfi1_schedule_send(qp);

		spin_unlock_irqrestore(&qp->s_lock, flags);
		if (is_fecn)
			goto send_ack;
		return;
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
	if (psn & IB_BTH_REQ_ACK) {
		if (packet->numpkt == 0) {
			rc_cancel_ack(qp);
			goto send_ack;
		}
		if (qp->r_adefered >= HFI1_PSN_CREDIT) {
			rc_cancel_ack(qp);
			goto send_ack;
		}
		if (unlikely(is_fecn)) {
			rc_cancel_ack(qp);
			goto send_ack;
		}
		qp->r_adefered++;
		rc_defered_ack(rcd, qp);
	}
	return;

rnr_nak:
	qp->r_nak_state = qp->r_min_rnr_timer | IB_RNR_NAK;
	qp->r_ack_psn = qp->r_psn;
	/* Queue RNR NAK for later */
	rc_defered_ack(rcd, qp);
	return;

nack_op_err:
	rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
	qp->r_nak_state = IB_NAK_REMOTE_OPERATIONAL_ERROR;
	qp->r_ack_psn = qp->r_psn;
	/* Queue NAK for later */
	rc_defered_ack(rcd, qp);
	return;

nack_inv_unlck:
	spin_unlock_irqrestore(&qp->s_lock, flags);
nack_inv:
	rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
	qp->r_nak_state = IB_NAK_INVALID_REQUEST;
	qp->r_ack_psn = qp->r_psn;
	/* Queue NAK for later */
	rc_defered_ack(rcd, qp);
	return;

nack_acc_unlck:
	spin_unlock_irqrestore(&qp->s_lock, flags);
nack_acc:
	rvt_rc_error(qp, IB_WC_LOC_PROT_ERR);
	qp->r_nak_state = IB_NAK_REMOTE_ACCESS_ERROR;
	qp->r_ack_psn = qp->r_psn;
send_ack:
	hfi1_send_rc_ack(packet, is_fecn);
}

void hfi1_rc_hdrerr(
	struct hfi1_ctxtdata *rcd,
	struct hfi1_packet *packet,
	struct rvt_qp *qp)
{
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	int diff;
	u32 opcode;
	u32 psn;

	if (hfi1_ruc_check_hdr(ibp, packet))
		return;

	psn = ib_bth_get_psn(packet->ohdr);
	opcode = ib_bth_get_opcode(packet->ohdr);

	/* Only deal with RDMA Writes for now */
	if (opcode < IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST) {
		diff = delta_psn(psn, qp->r_psn);
		if (!qp->r_nak_state && diff >= 0) {
			ibp->rvp.n_rc_seqnak++;
			qp->r_nak_state = IB_NAK_PSN_ERROR;
			/* Use the expected PSN. */
			qp->r_ack_psn = qp->r_psn;
			/*
			 * Wait to send the sequence
			 * NAK until all packets
			 * in the receive queue have
			 * been processed.
			 * Otherwise, we end up
			 * propagating congestion.
			 */
			rc_defered_ack(rcd, qp);
		} /* Out of sequence NAK */
	} /* QP Request NAKs */
}
