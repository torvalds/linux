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
#define OP(x) IB_OPCODE_UC_##x

static void complete_last_send(struct ipath_qp *qp, struct ipath_swqe *wqe,
			       struct ib_wc *wc)
{
	if (++qp->s_last == qp->s_size)
		qp->s_last = 0;
	if (!test_bit(IPATH_S_SIGNAL_REQ_WR, &qp->s_flags) ||
	    (wqe->wr.send_flags & IB_SEND_SIGNALED)) {
		wc->wr_id = wqe->wr.wr_id;
		wc->status = IB_WC_SUCCESS;
		wc->opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		wc->vendor_err = 0;
		wc->byte_len = wqe->length;
		wc->qp_num = qp->ibqp.qp_num;
		wc->src_qp = qp->remote_qpn;
		wc->pkey_index = 0;
		wc->slid = qp->remote_ah_attr.dlid;
		wc->sl = qp->remote_ah_attr.sl;
		wc->dlid_path_bits = 0;
		wc->port_num = 0;
		ipath_cq_enter(to_icq(qp->ibqp.send_cq), wc, 0);
	}
	wqe = get_swqe_ptr(qp, qp->s_last);
}

/**
 * ipath_do_uc_send - do a send on a UC queue
 * @data: contains a pointer to the QP to send on
 *
 * Process entries in the send work queue until the queue is exhausted.
 * Only allow one CPU to send a packet per QP (tasklet).
 * Otherwise, after we drop the QP lock, two threads could send
 * packets out of order.
 * This is similar to ipath_do_rc_send() below except we don't have
 * timeouts or resends.
 */
void ipath_do_uc_send(unsigned long data)
{
	struct ipath_qp *qp = (struct ipath_qp *)data;
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	struct ipath_swqe *wqe;
	unsigned long flags;
	u16 lrh0;
	u32 hwords;
	u32 nwords;
	u32 extra_bytes;
	u32 bth0;
	u32 bth2;
	u32 pmtu = ib_mtu_enum_to_int(qp->path_mtu);
	u32 len;
	struct ipath_other_headers *ohdr;
	struct ib_wc wc;

	if (test_and_set_bit(IPATH_S_BUSY, &qp->s_flags))
		goto bail;

	if (unlikely(qp->remote_ah_attr.dlid ==
		     ipath_layer_get_lid(dev->dd))) {
		/* Pass in an uninitialized ib_wc to save stack space. */
		ipath_ruc_loopback(qp, &wc);
		clear_bit(IPATH_S_BUSY, &qp->s_flags);
		goto bail;
	}

	ohdr = &qp->s_hdr.u.oth;
	if (qp->remote_ah_attr.ah_flags & IB_AH_GRH)
		ohdr = &qp->s_hdr.u.l.oth;

again:
	/* Check for a constructed packet to be sent. */
	if (qp->s_hdrwords != 0) {
			/*
			 * If no PIO bufs are available, return.
			 * An interrupt will call ipath_ib_piobufavail()
			 * when one is available.
			 */
			if (ipath_verbs_send(dev->dd, qp->s_hdrwords,
					     (u32 *) &qp->s_hdr,
					     qp->s_cur_size,
					     qp->s_cur_sge)) {
				ipath_no_bufs_available(qp, dev);
				goto bail;
			}
			dev->n_unicast_xmit++;
		/* Record that we sent the packet and s_hdr is empty. */
		qp->s_hdrwords = 0;
	}

	lrh0 = IPS_LRH_BTH;
	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;

	/*
	 * The lock is needed to synchronize between
	 * setting qp->s_ack_state and post_send().
	 */
	spin_lock_irqsave(&qp->s_lock, flags);

	if (!(ib_ipath_state_ops[qp->state] & IPATH_PROCESS_SEND_OK))
		goto done;

	bth0 = ipath_layer_get_pkey(dev->dd, qp->s_pkey_index);

	/* Send a request. */
	wqe = get_swqe_ptr(qp, qp->s_last);
	switch (qp->s_state) {
	default:
		/*
		 * Signal the completion of the last send (if there is
		 * one).
		 */
		if (qp->s_last != qp->s_tail)
			complete_last_send(qp, wqe, &wc);

		/* Check if send work queue is empty. */
		if (qp->s_tail == qp->s_head)
			goto done;
		/*
		 * Start a new request.
		 */
		qp->s_psn = wqe->psn = qp->s_next_psn;
		qp->s_sge.sge = wqe->sg_list[0];
		qp->s_sge.sg_list = wqe->sg_list + 1;
		qp->s_sge.num_sge = wqe->wr.num_sge;
		qp->s_len = len = wqe->length;
		switch (wqe->wr.opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			if (len > pmtu) {
				qp->s_state = OP(SEND_FIRST);
				len = pmtu;
				break;
			}
			if (wqe->wr.opcode == IB_WR_SEND)
				qp->s_state = OP(SEND_ONLY);
			else {
				qp->s_state =
					OP(SEND_ONLY_WITH_IMMEDIATE);
				/* Immediate data comes after the BTH */
				ohdr->u.imm_data = wqe->wr.imm_data;
				hwords += 1;
			}
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= 1 << 23;
			break;

		case IB_WR_RDMA_WRITE:
		case IB_WR_RDMA_WRITE_WITH_IMM:
			ohdr->u.rc.reth.vaddr =
				cpu_to_be64(wqe->wr.wr.rdma.remote_addr);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->wr.wr.rdma.rkey);
			ohdr->u.rc.reth.length = cpu_to_be32(len);
			hwords += sizeof(struct ib_reth) / 4;
			if (len > pmtu) {
				qp->s_state = OP(RDMA_WRITE_FIRST);
				len = pmtu;
				break;
			}
			if (wqe->wr.opcode == IB_WR_RDMA_WRITE)
				qp->s_state = OP(RDMA_WRITE_ONLY);
			else {
				qp->s_state =
					OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE);
				/* Immediate data comes after the RETH */
				ohdr->u.rc.imm_data = wqe->wr.imm_data;
				hwords += 1;
				if (wqe->wr.send_flags & IB_SEND_SOLICITED)
					bth0 |= 1 << 23;
			}
			break;

		default:
			goto done;
		}
		if (++qp->s_tail >= qp->s_size)
			qp->s_tail = 0;
		break;

	case OP(SEND_FIRST):
		qp->s_state = OP(SEND_MIDDLE);
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
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
		break;

	case OP(RDMA_WRITE_FIRST):
		qp->s_state = OP(RDMA_WRITE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			break;
		}
		if (wqe->wr.opcode == IB_WR_RDMA_WRITE)
			qp->s_state = OP(RDMA_WRITE_LAST);
		else {
			qp->s_state =
				OP(RDMA_WRITE_LAST_WITH_IMMEDIATE);
			/* Immediate data comes after the BTH */
			ohdr->u.imm_data = wqe->wr.imm_data;
			hwords += 1;
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= 1 << 23;
		}
		break;
	}
	bth2 = qp->s_next_psn++ & IPS_PSN_MASK;
	qp->s_len -= len;
	bth0 |= qp->s_state << 24;

	spin_unlock_irqrestore(&qp->s_lock, flags);

	/* Construct the header. */
	extra_bytes = (4 - len) & 3;
	nwords = (len + extra_bytes) >> 2;
	if (unlikely(qp->remote_ah_attr.ah_flags & IB_AH_GRH)) {
		/* Header size in 32-bit words. */
		hwords += 10;
		lrh0 = IPS_LRH_GRH;
		qp->s_hdr.u.l.grh.version_tclass_flow =
			cpu_to_be32((6 << 28) |
				    (qp->remote_ah_attr.grh.traffic_class
				     << 20) |
				    qp->remote_ah_attr.grh.flow_label);
		qp->s_hdr.u.l.grh.paylen =
			cpu_to_be16(((hwords - 12) + nwords +
				     SIZE_OF_CRC) << 2);
		/* next_hdr is defined by C8-7 in ch. 8.4.1 */
		qp->s_hdr.u.l.grh.next_hdr = 0x1B;
		qp->s_hdr.u.l.grh.hop_limit =
			qp->remote_ah_attr.grh.hop_limit;
		/* The SGID is 32-bit aligned. */
		qp->s_hdr.u.l.grh.sgid.global.subnet_prefix =
			dev->gid_prefix;
		qp->s_hdr.u.l.grh.sgid.global.interface_id =
			ipath_layer_get_guid(dev->dd);
		qp->s_hdr.u.l.grh.dgid = qp->remote_ah_attr.grh.dgid;
	}
	qp->s_hdrwords = hwords;
	qp->s_cur_sge = &qp->s_sge;
	qp->s_cur_size = len;
	lrh0 |= qp->remote_ah_attr.sl << 4;
	qp->s_hdr.lrh[0] = cpu_to_be16(lrh0);
	/* DEST LID */
	qp->s_hdr.lrh[1] = cpu_to_be16(qp->remote_ah_attr.dlid);
	qp->s_hdr.lrh[2] = cpu_to_be16(hwords + nwords + SIZE_OF_CRC);
	qp->s_hdr.lrh[3] = cpu_to_be16(ipath_layer_get_lid(dev->dd));
	bth0 |= extra_bytes << 20;
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(bth2);

	/* Check for more work to do. */
	goto again;

done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	clear_bit(IPATH_S_BUSY, &qp->s_flags);

bail:
	return;
}

/**
 * ipath_uc_rcv - handle an incoming UC packet
 * @dev: the device the packet came in on
 * @hdr: the header of the packet
 * @has_grh: true if the packet has a GRH
 * @data: the packet data
 * @tlen: the length of the packet
 * @qp: the QP for this packet.
 *
 * This is called from ipath_qp_rcv() to process an incoming UC packet
 * for the given QP.
 * Called at interrupt level.
 */
void ipath_uc_rcv(struct ipath_ibdev *dev, struct ipath_ib_header *hdr,
		  int has_grh, void *data, u32 tlen, struct ipath_qp *qp)
{
	struct ipath_other_headers *ohdr;
	int opcode;
	u32 hdrsize;
	u32 psn;
	u32 pad;
	unsigned long flags;
	struct ib_wc wc;
	u32 pmtu = ib_mtu_enum_to_int(qp->path_mtu);
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
		 * The header with GRH is 60 bytes and the
		 * core driver sets the eager header buffer
		 * size to 56 bytes so the last 4 bytes of
		 * the BTH header (PSN) is in the data buffer.
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

	wc.imm_data = 0;
	wc.wc_flags = 0;

	spin_lock_irqsave(&qp->r_rq.lock, flags);

	/* Compare the PSN verses the expected PSN. */
	if (unlikely(ipath_cmp24(psn, qp->r_psn) != 0)) {
		/*
		 * Handle a sequence error.
		 * Silently drop any current message.
		 */
		qp->r_psn = psn;
	inv:
		qp->r_state = OP(SEND_LAST);
		switch (opcode) {
		case OP(SEND_FIRST):
		case OP(SEND_ONLY):
		case OP(SEND_ONLY_WITH_IMMEDIATE):
			goto send_first;

		case OP(RDMA_WRITE_FIRST):
		case OP(RDMA_WRITE_ONLY):
		case OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE):
			goto rdma_first;

		default:
			dev->n_pkt_drops++;
			goto done;
		}
	}

	/* Check for opcode sequence errors. */
	switch (qp->r_state) {
	case OP(SEND_FIRST):
	case OP(SEND_MIDDLE):
		if (opcode == OP(SEND_MIDDLE) ||
		    opcode == OP(SEND_LAST) ||
		    opcode == OP(SEND_LAST_WITH_IMMEDIATE))
			break;
		goto inv;

	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_MIDDLE):
		if (opcode == OP(RDMA_WRITE_MIDDLE) ||
		    opcode == OP(RDMA_WRITE_LAST) ||
		    opcode == OP(RDMA_WRITE_LAST_WITH_IMMEDIATE))
			break;
		goto inv;

	default:
		if (opcode == OP(SEND_FIRST) ||
		    opcode == OP(SEND_ONLY) ||
		    opcode == OP(SEND_ONLY_WITH_IMMEDIATE) ||
		    opcode == OP(RDMA_WRITE_FIRST) ||
		    opcode == OP(RDMA_WRITE_ONLY) ||
		    opcode == OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE))
			break;
		goto inv;
	}

	/* OK, process the packet. */
	switch (opcode) {
	case OP(SEND_FIRST):
	case OP(SEND_ONLY):
	case OP(SEND_ONLY_WITH_IMMEDIATE):
	send_first:
		if (qp->r_reuse_sge) {
			qp->r_reuse_sge = 0;
			qp->r_sge = qp->s_rdma_sge;
		} else if (!ipath_get_rwqe(qp, 0)) {
			dev->n_pkt_drops++;
			goto done;
		}
		/* Save the WQE so we can reuse it in case of an error. */
		qp->s_rdma_sge = qp->r_sge;
		qp->r_rcv_len = 0;
		if (opcode == OP(SEND_ONLY))
			goto send_last;
		else if (opcode == OP(SEND_ONLY_WITH_IMMEDIATE))
			goto send_last_imm;
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		/* Check for invalid length PMTU or posted rwqe len. */
		if (unlikely(tlen != (hdrsize + pmtu + 4))) {
			qp->r_reuse_sge = 1;
			dev->n_pkt_drops++;
			goto done;
		}
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len)) {
			qp->r_reuse_sge = 1;
			dev->n_pkt_drops++;
			goto done;
		}
		ipath_copy_sge(&qp->r_sge, data, pmtu);
		break;

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
	send_last:
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* XXX LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4))) {
			qp->r_reuse_sge = 1;
			dev->n_pkt_drops++;
			goto done;
		}
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		wc.byte_len = tlen + qp->r_rcv_len;
		if (unlikely(wc.byte_len > qp->r_len)) {
			qp->r_reuse_sge = 1;
			dev->n_pkt_drops++;
			goto done;
		}
		/* XXX Need to free SGEs */
	last_imm:
		ipath_copy_sge(&qp->r_sge, data, tlen);
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
	case OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE): /* consume RWQE */
	rdma_first:
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

			/* Check rkey */
			if (unlikely(!ipath_rkey_ok(
					     dev, &qp->r_sge, qp->r_len,
					     vaddr, rkey,
					     IB_ACCESS_REMOTE_WRITE))) {
				dev->n_pkt_drops++;
				goto done;
			}
		} else {
			qp->r_sge.sg_list = NULL;
			qp->r_sge.sge.mr = NULL;
			qp->r_sge.sge.vaddr = NULL;
			qp->r_sge.sge.length = 0;
			qp->r_sge.sge.sge_length = 0;
		}
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_WRITE))) {
			dev->n_pkt_drops++;
			goto done;
		}
		if (opcode == OP(RDMA_WRITE_ONLY))
			goto rdma_last;
		else if (opcode ==
			 OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE))
			goto rdma_last_imm;
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		/* Check for invalid length PMTU or posted rwqe len. */
		if (unlikely(tlen != (hdrsize + pmtu + 4))) {
			dev->n_pkt_drops++;
			goto done;
		}
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len)) {
			dev->n_pkt_drops++;
			goto done;
		}
		ipath_copy_sge(&qp->r_sge, data, pmtu);
		break;

	case OP(RDMA_WRITE_LAST_WITH_IMMEDIATE):
	rdma_last_imm:
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* XXX LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4))) {
			dev->n_pkt_drops++;
			goto done;
		}
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		if (unlikely(tlen + qp->r_rcv_len != qp->r_len)) {
			dev->n_pkt_drops++;
			goto done;
		}
		if (qp->r_reuse_sge) {
			qp->r_reuse_sge = 0;
		} else if (!ipath_get_rwqe(qp, 1)) {
			dev->n_pkt_drops++;
			goto done;
		}
		if (header_in_data) {
			wc.imm_data = *(__be32 *) data;
			data += sizeof(__be32);
		} else {
			/* Immediate data comes after BTH */
			wc.imm_data = ohdr->u.imm_data;
		}
		hdrsize += 4;
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.byte_len = 0;
		goto last_imm;

	case OP(RDMA_WRITE_LAST):
	rdma_last:
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* XXX LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4))) {
			dev->n_pkt_drops++;
			goto done;
		}
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		if (unlikely(tlen + qp->r_rcv_len != qp->r_len)) {
			dev->n_pkt_drops++;
			goto done;
		}
		ipath_copy_sge(&qp->r_sge, data, tlen);
		break;

	default:
		/* Drop packet for unknown opcodes. */
		spin_unlock_irqrestore(&qp->r_rq.lock, flags);
		dev->n_pkt_drops++;
		goto bail;
	}
	qp->r_psn++;
	qp->r_state = opcode;
done:
	spin_unlock_irqrestore(&qp->r_rq.lock, flags);

bail:
	return;
}
