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
#include "ips_common.h"

/* cut down ridiculously long IB macro names */
#define OP(x) IB_OPCODE_RC_##x

/**
 * ipath_init_restart- initialize the qp->s_sge after a restart
 * @qp: the QP who's SGE we're restarting
 * @wqe: the work queue to initialize the QP's SGE from
 *
 * The QP s_lock should be held.
 */
static void ipath_init_restart(struct ipath_qp *qp, struct ipath_swqe *wqe)
{
	struct ipath_ibdev *dev;
	u32 len;

	len = ((qp->s_psn - wqe->psn) & IPS_PSN_MASK) *
		ib_mtu_enum_to_int(qp->path_mtu);
	qp->s_sge.sge = wqe->sg_list[0];
	qp->s_sge.sg_list = wqe->sg_list + 1;
	qp->s_sge.num_sge = wqe->wr.num_sge;
	ipath_skip_sge(&qp->s_sge, len);
	qp->s_len = wqe->length - len;
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
 * Return bth0 if constructed; otherwise, return 0.
 * Note the QP s_lock must be held.
 */
static inline u32 ipath_make_rc_ack(struct ipath_qp *qp,
				    struct ipath_other_headers *ohdr,
				    u32 pmtu)
{
	struct ipath_sge_state *ss;
	u32 hwords;
	u32 len;
	u32 bth0;

	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;

	/*
	 * Send a response.  Note that we are in the responder's
	 * side of the QP context.
	 */
	switch (qp->s_ack_state) {
	case OP(RDMA_READ_REQUEST):
		ss = &qp->s_rdma_sge;
		len = qp->s_rdma_len;
		if (len > pmtu) {
			len = pmtu;
			qp->s_ack_state = OP(RDMA_READ_RESPONSE_FIRST);
		}
		else
			qp->s_ack_state = OP(RDMA_READ_RESPONSE_ONLY);
		qp->s_rdma_len -= len;
		bth0 = qp->s_ack_state << 24;
		ohdr->u.aeth = ipath_compute_aeth(qp);
		hwords++;
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		qp->s_ack_state = OP(RDMA_READ_RESPONSE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_READ_RESPONSE_MIDDLE):
		ss = &qp->s_rdma_sge;
		len = qp->s_rdma_len;
		if (len > pmtu)
			len = pmtu;
		else {
			ohdr->u.aeth = ipath_compute_aeth(qp);
			hwords++;
			qp->s_ack_state = OP(RDMA_READ_RESPONSE_LAST);
		}
		qp->s_rdma_len -= len;
		bth0 = qp->s_ack_state << 24;
		break;

	case OP(RDMA_READ_RESPONSE_LAST):
	case OP(RDMA_READ_RESPONSE_ONLY):
		/*
		 * We have to prevent new requests from changing
		 * the r_sge state while a ipath_verbs_send()
		 * is in progress.
		 * Changing r_state allows the receiver
		 * to continue processing new packets.
		 * We do it here now instead of above so
		 * that we are sure the packet was sent before
		 * changing the state.
		 */
		qp->r_state = OP(RDMA_READ_RESPONSE_LAST);
		qp->s_ack_state = OP(ACKNOWLEDGE);
		return 0;

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD):
		ss = NULL;
		len = 0;
		qp->r_state = OP(SEND_LAST);
		qp->s_ack_state = OP(ACKNOWLEDGE);
		bth0 = IB_OPCODE_ATOMIC_ACKNOWLEDGE << 24;
		ohdr->u.at.aeth = ipath_compute_aeth(qp);
		ohdr->u.at.atomic_ack_eth = cpu_to_be64(qp->s_ack_atomic);
		hwords += sizeof(ohdr->u.at) / 4;
		break;

	default:
		/* Send a regular ACK. */
		ss = NULL;
		len = 0;
		qp->s_ack_state = OP(ACKNOWLEDGE);
		bth0 = qp->s_ack_state << 24;
		ohdr->u.aeth = ipath_compute_aeth(qp);
		hwords++;
	}
	qp->s_hdrwords = hwords;
	qp->s_cur_sge = ss;
	qp->s_cur_size = len;

	return bth0;
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
 * Note the QP s_lock must be held.
 */
static inline int ipath_make_rc_req(struct ipath_qp *qp,
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

	if (!(ib_ipath_state_ops[qp->state] & IPATH_PROCESS_SEND_OK) ||
	    qp->s_rnr_timeout)
		goto done;

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
				goto done;
			qp->s_psn = wqe->psn = qp->s_next_psn;
			newreq = 1;
		}
		/*
		 * Note that we have to be careful not to modify the
		 * original work request since we may need to resend
		 * it.
		 */
		qp->s_sge.sge = wqe->sg_list[0];
		qp->s_sge.sg_list = wqe->sg_list + 1;
		qp->s_sge.num_sge = wqe->wr.num_sge;
		qp->s_len = len = wqe->length;
		ss = &qp->s_sge;
		bth2 = 0;
		switch (wqe->wr.opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			/* If no credit, return. */
			if (qp->s_lsn != (u32) -1 &&
			    ipath_cmp24(wqe->ssn, qp->s_lsn + 1) > 0)
				goto done;
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
			if (newreq)
				qp->s_lsn++;
			/* FALLTHROUGH */
		case IB_WR_RDMA_WRITE_WITH_IMM:
			/* If no credit, return. */
			if (qp->s_lsn != (u32) -1 &&
			    ipath_cmp24(wqe->ssn, qp->s_lsn + 1) > 0)
				goto done;
			ohdr->u.rc.reth.vaddr =
				cpu_to_be64(wqe->wr.wr.rdma.remote_addr);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->wr.wr.rdma.rkey);
			ohdr->u.rc.reth.length = cpu_to_be32(len);
			hwords += sizeof(struct ib_reth) / 4;
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
				/* Immediate data comes
				 * after RETH */
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
			ohdr->u.rc.reth.vaddr =
				cpu_to_be64(wqe->wr.wr.rdma.remote_addr);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->wr.wr.rdma.rkey);
			ohdr->u.rc.reth.length = cpu_to_be32(len);
			qp->s_state = OP(RDMA_READ_REQUEST);
			hwords += sizeof(ohdr->u.rc.reth) / 4;
			if (newreq) {
				qp->s_lsn++;
				/*
				 * Adjust s_next_psn to count the
				 * expected number of responses.
				 */
				if (len > pmtu)
					qp->s_next_psn += (len - 1) / pmtu;
				wqe->lpsn = qp->s_next_psn++;
			}
			ss = NULL;
			len = 0;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_ATOMIC_CMP_AND_SWP:
		case IB_WR_ATOMIC_FETCH_AND_ADD:
			if (wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP)
				qp->s_state = OP(COMPARE_SWAP);
			else
				qp->s_state = OP(FETCH_ADD);
			ohdr->u.atomic_eth.vaddr = cpu_to_be64(
				wqe->wr.wr.atomic.remote_addr);
			ohdr->u.atomic_eth.rkey = cpu_to_be32(
				wqe->wr.wr.atomic.rkey);
			ohdr->u.atomic_eth.swap_data = cpu_to_be64(
				wqe->wr.wr.atomic.swap);
			ohdr->u.atomic_eth.compare_data = cpu_to_be64(
				wqe->wr.wr.atomic.compare_add);
			hwords += sizeof(struct ib_atomic_eth) / 4;
			if (newreq) {
				qp->s_lsn++;
				wqe->lpsn = wqe->psn;
			}
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			ss = NULL;
			len = 0;
			break;

		default:
			goto done;
		}
		if (newreq) {
			qp->s_tail++;
			if (qp->s_tail >= qp->s_size)
				qp->s_tail = 0;
		}
		bth2 |= qp->s_psn++ & IPS_PSN_MASK;
		if ((int)(qp->s_psn - qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		spin_lock(&dev->pending_lock);
		if (list_empty(&qp->timerwait))
			list_add_tail(&qp->timerwait,
				      &dev->pending[dev->pending_index]);
		spin_unlock(&dev->pending_lock);
		break;

	case OP(RDMA_READ_RESPONSE_FIRST):
		/*
		 * This case can only happen if a send is restarted.  See
		 * ipath_restart_rc().
		 */
		ipath_init_restart(qp, wqe);
		/* FALLTHROUGH */
	case OP(SEND_FIRST):
		qp->s_state = OP(SEND_MIDDLE);
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		bth2 = qp->s_psn++ & IPS_PSN_MASK;
		if ((int)(qp->s_psn - qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		ss = &qp->s_sge;
		len = qp->s_len;
		if (len > pmtu) {
			/*
			 * Request an ACK every 1/2 MB to avoid retransmit
			 * timeouts.
			 */
			if (((wqe->length - len) % (512 * 1024)) == 0)
				bth2 |= 1 << 31;
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
		bth2 = qp->s_psn++ & IPS_PSN_MASK;
		if ((int)(qp->s_psn - qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		ss = &qp->s_sge;
		len = qp->s_len;
		if (len > pmtu) {
			/*
			 * Request an ACK every 1/2 MB to avoid retransmit
			 * timeouts.
			 */
			if (((wqe->length - len) % (512 * 1024)) == 0)
				bth2 |= 1 << 31;
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
		len = ((qp->s_psn - wqe->psn) & IPS_PSN_MASK) * pmtu;
		ohdr->u.rc.reth.vaddr =
			cpu_to_be64(wqe->wr.wr.rdma.remote_addr + len);
		ohdr->u.rc.reth.rkey =
			cpu_to_be32(wqe->wr.wr.rdma.rkey);
		ohdr->u.rc.reth.length = cpu_to_be32(qp->s_len);
		qp->s_state = OP(RDMA_READ_REQUEST);
		hwords += sizeof(ohdr->u.rc.reth) / 4;
		bth2 = qp->s_psn++ & IPS_PSN_MASK;
		if ((int)(qp->s_psn - qp->s_next_psn) > 0)
			qp->s_next_psn = qp->s_psn;
		ss = NULL;
		len = 0;
		qp->s_cur++;
		if (qp->s_cur == qp->s_size)
			qp->s_cur = 0;
		break;

	case OP(RDMA_READ_REQUEST):
	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD):
		/*
		 * We shouldn't start anything new until this request is
		 * finished.  The ACK will handle rescheduling us.  XXX The
		 * number of outstanding ones is negotiated at connection
		 * setup time (see pg. 258,289)?  XXX Also, if we support
		 * multiple outstanding requests, we need to check the WQE
		 * IB_SEND_FENCE flag and not send a new request if a RDMA
		 * read or atomic is pending.
		 */
		goto done;
	}
	qp->s_len -= len;
	qp->s_hdrwords = hwords;
	qp->s_cur_sge = ss;
	qp->s_cur_size = len;
	*bth0p = bth0 | (qp->s_state << 24);
	*bth2p = bth2;
	return 1;

done:
	return 0;
}

static inline void ipath_make_rc_grh(struct ipath_qp *qp,
				     struct ib_global_route *grh,
				     u32 nwords)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);

	/* GRH header size in 32-bit words. */
	qp->s_hdrwords += 10;
	qp->s_hdr.u.l.grh.version_tclass_flow =
		cpu_to_be32((6 << 28) |
			    (grh->traffic_class << 20) |
			    grh->flow_label);
	qp->s_hdr.u.l.grh.paylen =
		cpu_to_be16(((qp->s_hdrwords - 12) + nwords +
			     SIZE_OF_CRC) << 2);
	/* next_hdr is defined by C8-7 in ch. 8.4.1 */
	qp->s_hdr.u.l.grh.next_hdr = 0x1B;
	qp->s_hdr.u.l.grh.hop_limit = grh->hop_limit;
	/* The SGID is 32-bit aligned. */
	qp->s_hdr.u.l.grh.sgid.global.subnet_prefix = dev->gid_prefix;
	qp->s_hdr.u.l.grh.sgid.global.interface_id =
		ipath_layer_get_guid(dev->dd);
	qp->s_hdr.u.l.grh.dgid = grh->dgid;
}

/**
 * ipath_do_rc_send - perform a send on an RC QP
 * @data: contains a pointer to the QP
 *
 * Process entries in the send work queue until credit or queue is
 * exhausted.  Only allow one CPU to send a packet per QP (tasklet).
 * Otherwise, after we drop the QP s_lock, two threads could send
 * packets out of order.
 */
void ipath_do_rc_send(unsigned long data)
{
	struct ipath_qp *qp = (struct ipath_qp *)data;
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	unsigned long flags;
	u16 lrh0;
	u32 nwords;
	u32 extra_bytes;
	u32 bth0;
	u32 bth2;
	u32 pmtu = ib_mtu_enum_to_int(qp->path_mtu);
	struct ipath_other_headers *ohdr;

	if (test_and_set_bit(IPATH_S_BUSY, &qp->s_flags))
		goto bail;

	if (unlikely(qp->remote_ah_attr.dlid ==
		     ipath_layer_get_lid(dev->dd))) {
		struct ib_wc wc;

		/*
		 * Pass in an uninitialized ib_wc to be consistent with
		 * other places where ipath_ruc_loopback() is called.
		 */
		ipath_ruc_loopback(qp, &wc);
		goto clear;
	}

	ohdr = &qp->s_hdr.u.oth;
	if (qp->remote_ah_attr.ah_flags & IB_AH_GRH)
		ohdr = &qp->s_hdr.u.l.oth;

again:
	/* Check for a constructed packet to be sent. */
	if (qp->s_hdrwords != 0) {
		/*
		 * If no PIO bufs are available, return.  An interrupt will
		 * call ipath_ib_piobufavail() when one is available.
		 */
		_VERBS_INFO("h %u %p\n", qp->s_hdrwords, &qp->s_hdr);
		_VERBS_INFO("d %u %p %u %p %u %u %u %u\n", qp->s_cur_size,
			    qp->s_cur_sge->sg_list,
			    qp->s_cur_sge->num_sge,
			    qp->s_cur_sge->sge.vaddr,
			    qp->s_cur_sge->sge.sge_length,
			    qp->s_cur_sge->sge.length,
			    qp->s_cur_sge->sge.m,
			    qp->s_cur_sge->sge.n);
		if (ipath_verbs_send(dev->dd, qp->s_hdrwords,
				     (u32 *) &qp->s_hdr, qp->s_cur_size,
				     qp->s_cur_sge)) {
			ipath_no_bufs_available(qp, dev);
			goto bail;
		}
		dev->n_unicast_xmit++;
		/* Record that we sent the packet and s_hdr is empty. */
		qp->s_hdrwords = 0;
	}

	/*
	 * The lock is needed to synchronize between setting
	 * qp->s_ack_state, resend timer, and post_send().
	 */
	spin_lock_irqsave(&qp->s_lock, flags);

	/* Sending responses has higher priority over sending requests. */
	if (qp->s_ack_state != OP(ACKNOWLEDGE) &&
	    (bth0 = ipath_make_rc_ack(qp, ohdr, pmtu)) != 0)
		bth2 = qp->s_ack_psn++ & IPS_PSN_MASK;
	else if (!ipath_make_rc_req(qp, ohdr, pmtu, &bth0, &bth2))
		goto done;

	spin_unlock_irqrestore(&qp->s_lock, flags);

	/* Construct the header. */
	extra_bytes = (4 - qp->s_cur_size) & 3;
	nwords = (qp->s_cur_size + extra_bytes) >> 2;
	lrh0 = IPS_LRH_BTH;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		ipath_make_rc_grh(qp, &qp->remote_ah_attr.grh, nwords);
		lrh0 = IPS_LRH_GRH;
	}
	lrh0 |= qp->remote_ah_attr.sl << 4;
	qp->s_hdr.lrh[0] = cpu_to_be16(lrh0);
	qp->s_hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	qp->s_hdr.lrh[2] = cpu_to_be16(qp->s_hdrwords + nwords +
				       SIZE_OF_CRC);
	qp->s_hdr.lrh[3] = cpu_to_be16(ipath_layer_get_lid(dev->dd));
	bth0 |= ipath_layer_get_pkey(dev->dd, qp->s_pkey_index);
	bth0 |= extra_bytes << 20;
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(bth2);

	/* Check for more work to do. */
	goto again;

done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
clear:
	clear_bit(IPATH_S_BUSY, &qp->s_flags);
bail:
	return;
}

static void send_rc_ack(struct ipath_qp *qp)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	u16 lrh0;
	u32 bth0;
	struct ipath_other_headers *ohdr;

	/* Construct the header. */
	ohdr = &qp->s_hdr.u.oth;
	lrh0 = IPS_LRH_BTH;
	/* header size in 32-bit words LRH+BTH+AETH = (8+12+4)/4. */
	qp->s_hdrwords = 6;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		ipath_make_rc_grh(qp, &qp->remote_ah_attr.grh, 0);
		ohdr = &qp->s_hdr.u.l.oth;
		lrh0 = IPS_LRH_GRH;
	}
	bth0 = ipath_layer_get_pkey(dev->dd, qp->s_pkey_index);
	ohdr->u.aeth = ipath_compute_aeth(qp);
	if (qp->s_ack_state >= OP(COMPARE_SWAP)) {
		bth0 |= IB_OPCODE_ATOMIC_ACKNOWLEDGE << 24;
		ohdr->u.at.atomic_ack_eth = cpu_to_be64(qp->s_ack_atomic);
		qp->s_hdrwords += sizeof(ohdr->u.at.atomic_ack_eth) / 4;
	}
	else
		bth0 |= OP(ACKNOWLEDGE) << 24;
	lrh0 |= qp->remote_ah_attr.sl << 4;
	qp->s_hdr.lrh[0] = cpu_to_be16(lrh0);
	qp->s_hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	qp->s_hdr.lrh[2] = cpu_to_be16(qp->s_hdrwords + SIZE_OF_CRC);
	qp->s_hdr.lrh[3] = cpu_to_be16(ipath_layer_get_lid(dev->dd));
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(qp->s_ack_psn & IPS_PSN_MASK);

	/*
	 * If we can send the ACK, clear the ACK state.
	 */
	if (ipath_verbs_send(dev->dd, qp->s_hdrwords, (u32 *) &qp->s_hdr,
			     0, NULL) == 0) {
		qp->s_ack_state = OP(ACKNOWLEDGE);
		dev->n_rc_qacks++;
		dev->n_unicast_xmit++;
	}
}

/**
 * ipath_restart_rc - back up requester to resend the last un-ACKed request
 * @qp: the QP to restart
 * @psn: packet sequence number for the request
 * @wc: the work completion request
 *
 * The QP s_lock should be held.
 */
void ipath_restart_rc(struct ipath_qp *qp, u32 psn, struct ib_wc *wc)
{
	struct ipath_swqe *wqe = get_swqe_ptr(qp, qp->s_last);
	struct ipath_ibdev *dev;
	u32 n;

	/*
	 * If there are no requests pending, we are done.
	 */
	if (ipath_cmp24(psn, qp->s_next_psn) >= 0 ||
	    qp->s_last == qp->s_tail)
		goto done;

	if (qp->s_retry == 0) {
		wc->wr_id = wqe->wr.wr_id;
		wc->status = IB_WC_RETRY_EXC_ERR;
		wc->opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		wc->vendor_err = 0;
		wc->byte_len = 0;
		wc->qp_num = qp->ibqp.qp_num;
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
		dev->n_rc_resends += (int)qp->s_psn - (int)psn;

	/*
	 * If we are starting the request from the beginning, let the normal
	 * send code handle initialization.
	 */
	qp->s_cur = qp->s_last;
	if (ipath_cmp24(psn, wqe->psn) <= 0) {
		qp->s_state = OP(SEND_LAST);
		qp->s_psn = wqe->psn;
	} else {
		n = qp->s_cur;
		for (;;) {
			if (++n == qp->s_size)
				n = 0;
			if (n == qp->s_tail) {
				if (ipath_cmp24(psn, qp->s_next_psn) >= 0) {
					qp->s_cur = n;
					wqe = get_swqe_ptr(qp, n);
				}
				break;
			}
			wqe = get_swqe_ptr(qp, n);
			if (ipath_cmp24(psn, wqe->psn) < 0)
				break;
			qp->s_cur = n;
		}
		qp->s_psn = psn;

		/*
		 * Reset the state to restart in the middle of a request.
		 * Don't change the s_sge, s_cur_sge, or s_cur_size.
		 * See ipath_do_rc_send().
		 */
		switch (wqe->wr.opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			qp->s_state = OP(RDMA_READ_RESPONSE_FIRST);
			break;

		case IB_WR_RDMA_WRITE:
		case IB_WR_RDMA_WRITE_WITH_IMM:
			qp->s_state = OP(RDMA_READ_RESPONSE_LAST);
			break;

		case IB_WR_RDMA_READ:
			qp->s_state =
				OP(RDMA_READ_RESPONSE_MIDDLE);
			break;

		default:
			/*
			 * This case shouldn't happen since its only
			 * one PSN per req.
			 */
			qp->s_state = OP(SEND_LAST);
		}
	}

done:
	tasklet_hi_schedule(&qp->s_task);

bail:
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
	struct ipath_swqe *wqe;
	u32 n;

	n = qp->s_cur;
	wqe = get_swqe_ptr(qp, n);
	for (;;) {
		if (++n == qp->s_size)
			n = 0;
		if (n == qp->s_tail) {
			if (ipath_cmp24(psn, qp->s_next_psn) >= 0) {
				qp->s_cur = n;
				wqe = get_swqe_ptr(qp, n);
			}
			break;
		}
		wqe = get_swqe_ptr(qp, n);
		if (ipath_cmp24(psn, wqe->psn) < 0)
			break;
		qp->s_cur = n;
	}
	qp->s_psn = psn;

	/*
	 * Set the state to restart in the middle of a
	 * request.  Don't change the s_sge, s_cur_sge, or
	 * s_cur_size.  See ipath_do_rc_send().
	 */
	switch (wqe->wr.opcode) {
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
}

/**
 * do_rc_ack - process an incoming RC ACK
 * @qp: the QP the ACK came in on
 * @psn: the packet sequence number of the ACK
 * @opcode: the opcode of the request that resulted in the ACK
 *
 * This is called from ipath_rc_rcv() to process an incoming RC ACK
 * for the given QP.
 * Called at interrupt level with the QP s_lock held.
 * Returns 1 if OK, 0 if current operation should be aborted (NAK).
 */
static int do_rc_ack(struct ipath_qp *qp, u32 aeth, u32 psn, int opcode)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	struct ib_wc wc;
	struct ipath_swqe *wqe;
	int ret = 0;

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
	wqe = get_swqe_ptr(qp, qp->s_last);

	/* Nothing is pending to ACK/NAK. */
	if (qp->s_last == qp->s_tail)
		goto bail;

	/*
	 * The MSN might be for a later WQE than the PSN indicates so
	 * only complete WQEs that the PSN finishes.
	 */
	while (ipath_cmp24(psn, wqe->lpsn) >= 0) {
		/* If we are ACKing a WQE, the MSN should be >= the SSN. */
		if (ipath_cmp24(aeth, wqe->ssn) < 0)
			break;
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
		     opcode != OP(RDMA_READ_RESPONSE_LAST)) ||
		    ((wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		      wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) &&
		     (opcode != OP(ATOMIC_ACKNOWLEDGE) ||
		      ipath_cmp24(wqe->psn, psn) != 0))) {
			/*
			 * The last valid PSN seen is the previous
			 * request's.
			 */
			qp->s_last_psn = wqe->psn - 1;
			/* Retry this request. */
			ipath_restart_rc(qp, wqe->psn, &wc);
			/*
			 * No need to process the ACK/NAK since we are
			 * restarting an earlier request.
			 */
			goto bail;
		}
		/* Post a send completion queue entry if requested. */
		if (!test_bit(IPATH_S_SIGNAL_REQ_WR, &qp->s_flags) ||
		    (wqe->wr.send_flags & IB_SEND_SIGNALED)) {
			wc.wr_id = wqe->wr.wr_id;
			wc.status = IB_WC_SUCCESS;
			wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
			wc.vendor_err = 0;
			wc.byte_len = wqe->length;
			wc.qp_num = qp->ibqp.qp_num;
			wc.src_qp = qp->remote_qpn;
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
			wqe = get_swqe_ptr(qp, qp->s_cur);
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = wqe->psn;
		}
		if (++qp->s_last >= qp->s_size)
			qp->s_last = 0;
		wqe = get_swqe_ptr(qp, qp->s_last);
		if (qp->s_last == qp->s_tail)
			break;
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
		}
		ipath_get_credit(qp, aeth);
		qp->s_rnr_retry = qp->s_rnr_retry_cnt;
		qp->s_retry = qp->s_retry_cnt;
		qp->s_last_psn = psn;
		ret = 1;
		goto bail;

	case 1:		/* RNR NAK */
		dev->n_rnr_naks++;
		if (qp->s_rnr_retry == 0) {
			if (qp->s_last == qp->s_tail)
				goto bail;

			wc.status = IB_WC_RNR_RETRY_EXC_ERR;
			goto class_b;
		}
		if (qp->s_rnr_retry_cnt < 7)
			qp->s_rnr_retry--;
		if (qp->s_last == qp->s_tail)
			goto bail;

		/* The last valid PSN seen is the previous request's. */
		qp->s_last_psn = wqe->psn - 1;

		dev->n_rc_resends += (int)qp->s_psn - (int)psn;

		/*
		 * If we are starting the request from the beginning, let
		 * the normal send code handle initialization.
		 */
		qp->s_cur = qp->s_last;
		wqe = get_swqe_ptr(qp, qp->s_cur);
		if (ipath_cmp24(psn, wqe->psn) <= 0) {
			qp->s_state = OP(SEND_LAST);
			qp->s_psn = wqe->psn;
		} else
			reset_psn(qp, psn);

		qp->s_rnr_timeout =
			ib_ipath_rnr_table[(aeth >> IPS_AETH_CREDIT_SHIFT) &
					   IPS_AETH_CREDIT_MASK];
		ipath_insert_rnr_queue(qp);
		goto bail;

	case 3:		/* NAK */
		/* The last valid PSN seen is the previous request's. */
		if (qp->s_last != qp->s_tail)
			qp->s_last_psn = wqe->psn - 1;
		switch ((aeth >> IPS_AETH_CREDIT_SHIFT) &
			IPS_AETH_CREDIT_MASK) {
		case 0:	/* PSN sequence error */
			dev->n_seq_naks++;
			/*
			 * Back up to the responder's expected PSN.  XXX
			 * Note that we might get a NAK in the middle of an
			 * RDMA READ response which terminates the RDMA
			 * READ.
			 */
			if (qp->s_last == qp->s_tail)
				break;

			if (ipath_cmp24(psn, wqe->psn) < 0)
				break;

			/* Retry the request. */
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
			wc.qp_num = qp->ibqp.qp_num;
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
		if (opcode == OP(ATOMIC_ACKNOWLEDGE))
			*(u64 *) qp->s_sge.sge.vaddr = *(u64 *) data;
		if (!do_rc_ack(qp, aeth, psn, opcode) ||
		    opcode != OP(RDMA_READ_RESPONSE_FIRST))
			goto ack_done;
		hdrsize += 4;
		/*
		 * do_rc_ack() has already checked the PSN so skip
		 * the sequence check.
		 */
		goto rdma_read;

	case OP(RDMA_READ_RESPONSE_MIDDLE):
		/* no AETH, no ACK */
		if (unlikely(ipath_cmp24(psn, qp->s_last_psn + 1))) {
			dev->n_rdma_seq++;
			ipath_restart_rc(qp, qp->s_last_psn + 1, &wc);
			goto ack_done;
		}
	rdma_read:
	if (unlikely(qp->s_state != OP(RDMA_READ_REQUEST)))
		goto ack_done;
	if (unlikely(tlen != (hdrsize + pmtu + 4)))
		goto ack_done;
	if (unlikely(pmtu >= qp->s_len))
		goto ack_done;
	/* We got a response so update the timeout. */
	if (unlikely(qp->s_last == qp->s_tail ||
		     get_swqe_ptr(qp, qp->s_last)->wr.opcode !=
		     IB_WR_RDMA_READ))
		goto ack_done;
	spin_lock(&dev->pending_lock);
	if (qp->s_rnr_timeout == 0 && !list_empty(&qp->timerwait))
		list_move_tail(&qp->timerwait,
			       &dev->pending[dev->pending_index]);
	spin_unlock(&dev->pending_lock);
	/*
	 * Update the RDMA receive state but do the copy w/o holding the
	 * locks and blocking interrupts.  XXX Yet another place that
	 * affects relaxed RDMA order since we don't want s_sge modified.
	 */
	qp->s_len -= pmtu;
	qp->s_last_psn = psn;
	spin_unlock_irqrestore(&qp->s_lock, flags);
	ipath_copy_sge(&qp->s_sge, data, pmtu);
	goto bail;

	case OP(RDMA_READ_RESPONSE_LAST):
		/* ACKs READ req. */
		if (unlikely(ipath_cmp24(psn, qp->s_last_psn + 1))) {
			dev->n_rdma_seq++;
			ipath_restart_rc(qp, qp->s_last_psn + 1, &wc);
			goto ack_done;
		}
		/* FALLTHROUGH */
	case OP(RDMA_READ_RESPONSE_ONLY):
		if (unlikely(qp->s_state != OP(RDMA_READ_REQUEST)))
			goto ack_done;
		/*
		 * Get the number of bytes the message was padded by.
		 */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/*
		 * Check that the data size is >= 1 && <= pmtu.
		 * Remember to account for the AETH header (4) and
		 * ICRC (4).
		 */
		if (unlikely(tlen <= (hdrsize + pad + 8))) {
			/*
			 * XXX Need to generate an error CQ
			 * entry.
			 */
			goto ack_done;
		}
		tlen -= hdrsize + pad + 8;
		if (unlikely(tlen != qp->s_len)) {
			/*
			 * XXX Need to generate an error CQ
			 * entry.
			 */
			goto ack_done;
		}
		if (!header_in_data)
			aeth = be32_to_cpu(ohdr->u.aeth);
		else {
			aeth = be32_to_cpu(((__be32 *) data)[0]);
			data += sizeof(__be32);
		}
		ipath_copy_sge(&qp->s_sge, data, tlen);
		if (do_rc_ack(qp, aeth, psn, OP(RDMA_READ_RESPONSE_LAST))) {
			/*
			 * Change the state so we contimue
			 * processing new requests.
			 */
			qp->s_state = OP(SEND_LAST);
		}
		goto ack_done;
	}

ack_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
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
 * schedule a response to be sent and the s_lock unlocked.
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
	struct ib_reth *reth;

	if (diff > 0) {
		/*
		 * Packet sequence error.
		 * A NAK will ACK earlier sends and RDMA writes.
		 * Don't queue the NAK if a RDMA read, atomic, or
		 * NAK is pending though.
		 */
		spin_lock(&qp->s_lock);
		if ((qp->s_ack_state >= OP(RDMA_READ_REQUEST) &&
		     qp->s_ack_state != IB_OPCODE_ACKNOWLEDGE) ||
		    qp->s_nak_state != 0) {
			spin_unlock(&qp->s_lock);
			goto done;
		}
		qp->s_ack_state = OP(SEND_ONLY);
		qp->s_nak_state = IB_NAK_PSN_ERROR;
		/* Use the expected PSN. */
		qp->s_ack_psn = qp->r_psn;
		goto resched;
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
	 */
	spin_lock(&qp->s_lock);
	if (qp->s_ack_state != IB_OPCODE_ACKNOWLEDGE &&
	    ipath_cmp24(psn, qp->s_ack_psn) >= 0) {
		if (qp->s_ack_state < IB_OPCODE_RDMA_READ_REQUEST)
			qp->s_ack_psn = psn;
		spin_unlock(&qp->s_lock);
		goto done;
	}
	switch (opcode) {
	case OP(RDMA_READ_REQUEST):
		/*
		 * We have to be careful to not change s_rdma_sge
		 * while ipath_do_rc_send() is using it and not
		 * holding the s_lock.
		 */
		if (qp->s_ack_state != OP(ACKNOWLEDGE) &&
		    qp->s_ack_state >= IB_OPCODE_RDMA_READ_REQUEST) {
			spin_unlock(&qp->s_lock);
			dev->n_rdma_dup_busy++;
			goto done;
		}
		/* RETH comes after BTH */
		if (!header_in_data)
			reth = &ohdr->u.rc.reth;
		else {
			reth = (struct ib_reth *)data;
			data += sizeof(*reth);
		}
		qp->s_rdma_len = be32_to_cpu(reth->length);
		if (qp->s_rdma_len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/*
			 * Address range must be a subset of the original
			 * request and start on pmtu boundaries.
			 */
			ok = ipath_rkey_ok(dev, &qp->s_rdma_sge,
					   qp->s_rdma_len, vaddr, rkey,
					   IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok))
				goto done;
		} else {
			qp->s_rdma_sge.sg_list = NULL;
			qp->s_rdma_sge.num_sge = 0;
			qp->s_rdma_sge.sge.mr = NULL;
			qp->s_rdma_sge.sge.vaddr = NULL;
			qp->s_rdma_sge.sge.length = 0;
			qp->s_rdma_sge.sge.sge_length = 0;
		}
		break;

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD):
		/*
		 * Check for the PSN of the last atomic operations
		 * performed and resend the result if found.
		 */
		if ((psn & IPS_PSN_MASK) != qp->r_atomic_psn) {
			spin_unlock(&qp->s_lock);
			goto done;
		}
		qp->s_ack_atomic = qp->r_atomic_data;
		break;
	}
	qp->s_ack_state = opcode;
	qp->s_nak_state = 0;
	qp->s_ack_psn = psn;
resched:
	return 0;

done:
	return 1;
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
	unsigned long flags;
	struct ib_wc wc;
	u32 pmtu = ib_mtu_enum_to_int(qp->path_mtu);
	int diff;
	struct ib_reth *reth;
	int header_in_data;

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
		header_in_data =
			ipath_layer_get_rcvhdrentsize(dev->dd) == 16;
		if (header_in_data) {
			psn = be32_to_cpu(((__be32 *) data)[0]);
			data += sizeof(__be32);
		} else
			psn = be32_to_cpu(ohdr->bth[2]);
	}
	/*
	 * The opcode is in the low byte when its in network order
	 * (top byte when in host order).
	 */
	opcode = be32_to_cpu(ohdr->bth[0]) >> 24;

	/*
	 * Process responses (ACKs) before anything else.  Note that the
	 * packet sequence number will be for something in the send work
	 * queue rather than the expected receive packet sequence number.
	 * In other words, this QP is the requester.
	 */
	if (opcode >= OP(RDMA_READ_RESPONSE_FIRST) &&
	    opcode <= OP(ATOMIC_ACKNOWLEDGE)) {
		ipath_rc_rcv_resp(dev, ohdr, data, tlen, qp, opcode, psn,
				  hdrsize, pmtu, header_in_data);
		goto bail;
	}

	spin_lock_irqsave(&qp->r_rq.lock, flags);

	/* Compute 24 bits worth of difference. */
	diff = ipath_cmp24(psn, qp->r_psn);
	if (unlikely(diff)) {
		if (ipath_rc_rcv_error(dev, ohdr, data, qp, opcode,
				       psn, diff, header_in_data))
			goto done;
		goto resched;
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
	/*
	 * A NAK will ACK earlier sends and RDMA writes.  Don't queue the
	 * NAK if a RDMA read, atomic, or NAK is pending though.
	 */
	spin_lock(&qp->s_lock);
	if (qp->s_ack_state >= OP(RDMA_READ_REQUEST) &&
	    qp->s_ack_state != IB_OPCODE_ACKNOWLEDGE) {
		spin_unlock(&qp->s_lock);
		goto done;
	}
	/* XXX Flush WQEs */
	qp->state = IB_QPS_ERR;
	qp->s_ack_state = OP(SEND_ONLY);
	qp->s_nak_state = IB_NAK_INVALID_REQUEST;
	qp->s_ack_psn = qp->r_psn;
	goto resched;

	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_MIDDLE):
		if (opcode == OP(RDMA_WRITE_MIDDLE) ||
		    opcode == OP(RDMA_WRITE_LAST) ||
		    opcode == OP(RDMA_WRITE_LAST_WITH_IMMEDIATE))
			break;
		goto nack_inv;

	case OP(RDMA_READ_REQUEST):
	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD):
		/*
		 * Drop all new requests until a response has been sent.  A
		 * new request then ACKs the RDMA response we sent.  Relaxed
		 * ordering would allow new requests to be processed but we
		 * would need to keep a queue of rwqe's for all that are in
		 * progress.  Note that we can't RNR NAK this request since
		 * the RDMA READ or atomic response is already queued to be
		 * sent (unless we implement a response send queue).
		 */
		goto done;

	default:
		if (opcode == OP(SEND_MIDDLE) ||
		    opcode == OP(SEND_LAST) ||
		    opcode == OP(SEND_LAST_WITH_IMMEDIATE) ||
		    opcode == OP(RDMA_WRITE_MIDDLE) ||
		    opcode == OP(RDMA_WRITE_LAST) ||
		    opcode == OP(RDMA_WRITE_LAST_WITH_IMMEDIATE))
			goto nack_inv;
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
			spin_lock(&qp->s_lock);
			if (qp->s_ack_state >=
			    OP(RDMA_READ_REQUEST) &&
			    qp->s_ack_state != IB_OPCODE_ACKNOWLEDGE) {
				spin_unlock(&qp->s_lock);
				goto done;
			}
			qp->s_ack_state = OP(SEND_ONLY);
			qp->s_nak_state = IB_RNR_NAK | qp->s_min_rnr_timer;
			qp->s_ack_psn = qp->r_psn;
			goto resched;
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
		atomic_inc(&qp->msn);
		if (opcode == OP(RDMA_WRITE_LAST) ||
		    opcode == OP(RDMA_WRITE_ONLY))
			break;
		wc.wr_id = qp->r_wr_id;
		wc.status = IB_WC_SUCCESS;
		wc.opcode = IB_WC_RECV;
		wc.vendor_err = 0;
		wc.qp_num = qp->ibqp.qp_num;
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
			ok = ipath_rkey_ok(dev, &qp->r_sge,
					   qp->r_len, vaddr, rkey,
					   IB_ACCESS_REMOTE_WRITE);
			if (unlikely(!ok)) {
			nack_acc:
				/*
				 * A NAK will ACK earlier sends and RDMA
				 * writes.  Don't queue the NAK if a RDMA
				 * read, atomic, or NAK is pending though.
				 */
				spin_lock(&qp->s_lock);
				if (qp->s_ack_state >=
				    OP(RDMA_READ_REQUEST) &&
				    qp->s_ack_state !=
				    IB_OPCODE_ACKNOWLEDGE) {
					spin_unlock(&qp->s_lock);
					goto done;
				}
				/* XXX Flush WQEs */
				qp->state = IB_QPS_ERR;
				qp->s_ack_state = OP(RDMA_WRITE_ONLY);
				qp->s_nak_state =
					IB_NAK_REMOTE_ACCESS_ERROR;
				qp->s_ack_psn = qp->r_psn;
				goto resched;
			}
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

	case OP(RDMA_READ_REQUEST):
		/* RETH comes after BTH */
		if (!header_in_data)
			reth = &ohdr->u.rc.reth;
		else {
			reth = (struct ib_reth *)data;
			data += sizeof(*reth);
		}
		spin_lock(&qp->s_lock);
		if (qp->s_ack_state != OP(ACKNOWLEDGE) &&
		    qp->s_ack_state >= IB_OPCODE_RDMA_READ_REQUEST) {
			spin_unlock(&qp->s_lock);
			goto done;
		}
		qp->s_rdma_len = be32_to_cpu(reth->length);
		if (qp->s_rdma_len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/* Check rkey & NAK */
			ok = ipath_rkey_ok(dev, &qp->s_rdma_sge,
					   qp->s_rdma_len, vaddr, rkey,
					   IB_ACCESS_REMOTE_READ);
			if (unlikely(!ok)) {
				spin_unlock(&qp->s_lock);
				goto nack_acc;
			}
			/*
			 * Update the next expected PSN.  We add 1 later
			 * below, so only add the remainder here.
			 */
			if (qp->s_rdma_len > pmtu)
				qp->r_psn += (qp->s_rdma_len - 1) / pmtu;
		} else {
			qp->s_rdma_sge.sg_list = NULL;
			qp->s_rdma_sge.num_sge = 0;
			qp->s_rdma_sge.sge.mr = NULL;
			qp->s_rdma_sge.sge.vaddr = NULL;
			qp->s_rdma_sge.sge.length = 0;
			qp->s_rdma_sge.sge.sge_length = 0;
		}
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_READ)))
			goto nack_acc;
		/*
		 * We need to increment the MSN here instead of when we
		 * finish sending the result since a duplicate request would
		 * increment it more than once.
		 */
		atomic_inc(&qp->msn);
		qp->s_ack_state = opcode;
		qp->s_nak_state = 0;
		qp->s_ack_psn = psn;
		qp->r_psn++;
		qp->r_state = opcode;
		goto rdmadone;

	case OP(COMPARE_SWAP):
	case OP(FETCH_ADD): {
		struct ib_atomic_eth *ateth;
		u64 vaddr;
		u64 sdata;
		u32 rkey;

		if (!header_in_data)
			ateth = &ohdr->u.atomic_eth;
		else {
			ateth = (struct ib_atomic_eth *)data;
			data += sizeof(*ateth);
		}
		vaddr = be64_to_cpu(ateth->vaddr);
		if (unlikely(vaddr & (sizeof(u64) - 1)))
			goto nack_inv;
		rkey = be32_to_cpu(ateth->rkey);
		/* Check rkey & NAK */
		if (unlikely(!ipath_rkey_ok(dev, &qp->r_sge,
					    sizeof(u64), vaddr, rkey,
					    IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_acc;
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_ATOMIC)))
			goto nack_acc;
		/* Perform atomic OP and save result. */
		sdata = be64_to_cpu(ateth->swap_data);
		spin_lock(&dev->pending_lock);
		qp->r_atomic_data = *(u64 *) qp->r_sge.sge.vaddr;
		if (opcode == OP(FETCH_ADD))
			*(u64 *) qp->r_sge.sge.vaddr =
				qp->r_atomic_data + sdata;
		else if (qp->r_atomic_data ==
			 be64_to_cpu(ateth->compare_data))
			*(u64 *) qp->r_sge.sge.vaddr = sdata;
		spin_unlock(&dev->pending_lock);
		atomic_inc(&qp->msn);
		qp->r_atomic_psn = psn & IPS_PSN_MASK;
		psn |= 1 << 31;
		break;
	}

	default:
		/* Drop packet for unknown opcodes. */
		goto done;
	}
	qp->r_psn++;
	qp->r_state = opcode;
	/* Send an ACK if requested or required. */
	if (psn & (1 << 31)) {
		/*
		 * Coalesce ACKs unless there is a RDMA READ or
		 * ATOMIC pending.
		 */
		spin_lock(&qp->s_lock);
		if (qp->s_ack_state == OP(ACKNOWLEDGE) ||
		    qp->s_ack_state < IB_OPCODE_RDMA_READ_REQUEST) {
			qp->s_ack_state = opcode;
			qp->s_nak_state = 0;
			qp->s_ack_psn = psn;
			qp->s_ack_atomic = qp->r_atomic_data;
			goto resched;
		}
		spin_unlock(&qp->s_lock);
	}
done:
	spin_unlock_irqrestore(&qp->r_rq.lock, flags);
	goto bail;

resched:
	/*
	 * Try to send ACK right away but not if ipath_do_rc_send() is
	 * active.
	 */
	if (qp->s_hdrwords == 0 &&
	    (qp->s_ack_state < IB_OPCODE_RDMA_READ_REQUEST ||
	     qp->s_ack_state >= IB_OPCODE_COMPARE_SWAP))
		send_rc_ack(qp);

rdmadone:
	spin_unlock(&qp->s_lock);
	spin_unlock_irqrestore(&qp->r_rq.lock, flags);

	/* Call ipath_do_rc_send() in another thread. */
	tasklet_hi_schedule(&qp->s_task);

bail:
	return;
}
