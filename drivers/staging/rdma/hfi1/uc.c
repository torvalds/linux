/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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

#include "hfi.h"
#include "sdma.h"
#include "qp.h"

/* cut down ridiculously long IB macro names */
#define OP(x) IB_OPCODE_UC_##x

/**
 * hfi1_make_uc_req - construct a request packet (SEND, RDMA write)
 * @qp: a pointer to the QP
 *
 * Return 1 if constructed; otherwise, return 0.
 */
int hfi1_make_uc_req(struct hfi1_qp *qp)
{
	struct hfi1_other_headers *ohdr;
	struct hfi1_swqe *wqe;
	unsigned long flags;
	u32 hwords = 5;
	u32 bth0 = 0;
	u32 len;
	u32 pmtu = qp->pmtu;
	int ret = 0;
	int middle = 0;

	spin_lock_irqsave(&qp->s_lock, flags);

	if (!(ib_hfi1_state_ops[qp->state] & HFI1_PROCESS_SEND_OK)) {
		if (!(ib_hfi1_state_ops[qp->state] & HFI1_FLUSH_SEND))
			goto bail;
		/* We are in the error state, flush the work request. */
		if (qp->s_last == qp->s_head)
			goto bail;
		/* If DMAs are in progress, we can't flush immediately. */
		if (atomic_read(&qp->s_iowait.sdma_busy)) {
			qp->s_flags |= HFI1_S_WAIT_DMA;
			goto bail;
		}
		clear_ahg(qp);
		wqe = get_swqe_ptr(qp, qp->s_last);
		hfi1_send_complete(qp, wqe, IB_WC_WR_FLUSH_ERR);
		goto done;
	}

	ohdr = &qp->s_hdr->ibh.u.oth;
	if (qp->remote_ah_attr.ah_flags & IB_AH_GRH)
		ohdr = &qp->s_hdr->ibh.u.l.oth;

	/* Get the next send request. */
	wqe = get_swqe_ptr(qp, qp->s_cur);
	qp->s_wqe = NULL;
	switch (qp->s_state) {
	default:
		if (!(ib_hfi1_state_ops[qp->state] &
		    HFI1_PROCESS_NEXT_SEND_OK))
			goto bail;
		/* Check if send work queue is empty. */
		if (qp->s_cur == qp->s_head) {
			clear_ahg(qp);
			goto bail;
		}
		/*
		 * Start a new request.
		 */
		wqe->psn = qp->s_next_psn;
		qp->s_psn = qp->s_next_psn;
		qp->s_sge.sge = wqe->sg_list[0];
		qp->s_sge.sg_list = wqe->sg_list + 1;
		qp->s_sge.num_sge = wqe->wr.num_sge;
		qp->s_sge.total_len = wqe->length;
		len = wqe->length;
		qp->s_len = len;
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
				ohdr->u.imm_data = wqe->wr.ex.imm_data;
				hwords += 1;
			}
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= IB_BTH_SOLICITED;
			qp->s_wqe = wqe;
			if (++qp->s_cur >= qp->s_size)
				qp->s_cur = 0;
			break;

		case IB_WR_RDMA_WRITE:
		case IB_WR_RDMA_WRITE_WITH_IMM:
			ohdr->u.rc.reth.vaddr =
				cpu_to_be64(wqe->rdma_wr.remote_addr);
			ohdr->u.rc.reth.rkey =
				cpu_to_be32(wqe->rdma_wr.rkey);
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
				ohdr->u.rc.imm_data = wqe->wr.ex.imm_data;
				hwords += 1;
				if (wqe->wr.send_flags & IB_SEND_SOLICITED)
					bth0 |= IB_BTH_SOLICITED;
			}
			qp->s_wqe = wqe;
			if (++qp->s_cur >= qp->s_size)
				qp->s_cur = 0;
			break;

		default:
			goto bail;
		}
		break;

	case OP(SEND_FIRST):
		qp->s_state = OP(SEND_MIDDLE);
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			middle = HFI1_CAP_IS_KSET(SDMA_AHG);
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
		qp->s_wqe = wqe;
		if (++qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		break;

	case OP(RDMA_WRITE_FIRST):
		qp->s_state = OP(RDMA_WRITE_MIDDLE);
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		len = qp->s_len;
		if (len > pmtu) {
			len = pmtu;
			middle = HFI1_CAP_IS_KSET(SDMA_AHG);
			break;
		}
		if (wqe->wr.opcode == IB_WR_RDMA_WRITE)
			qp->s_state = OP(RDMA_WRITE_LAST);
		else {
			qp->s_state =
				OP(RDMA_WRITE_LAST_WITH_IMMEDIATE);
			/* Immediate data comes after the BTH */
			ohdr->u.imm_data = wqe->wr.ex.imm_data;
			hwords += 1;
			if (wqe->wr.send_flags & IB_SEND_SOLICITED)
				bth0 |= IB_BTH_SOLICITED;
		}
		qp->s_wqe = wqe;
		if (++qp->s_cur >= qp->s_size)
			qp->s_cur = 0;
		break;
	}
	qp->s_len -= len;
	qp->s_hdrwords = hwords;
	qp->s_cur_sge = &qp->s_sge;
	qp->s_cur_size = len;
	hfi1_make_ruc_header(qp, ohdr, bth0 | (qp->s_state << 24),
			     mask_psn(qp->s_next_psn++), middle);
done:
	ret = 1;
	goto unlock;

bail:
	qp->s_flags &= ~HFI1_S_BUSY;
unlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

/**
 * hfi1_uc_rcv - handle an incoming UC packet
 * @ibp: the port the packet came in on
 * @hdr: the header of the packet
 * @rcv_flags: flags relevant to rcv processing
 * @data: the packet data
 * @tlen: the length of the packet
 * @qp: the QP for this packet.
 *
 * This is called from qp_rcv() to process an incoming UC packet
 * for the given QP.
 * Called at interrupt level.
 */
void hfi1_uc_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp = &packet->rcd->ppd->ibport_data;
	struct hfi1_ib_header *hdr = packet->hdr;
	u32 rcv_flags = packet->rcv_flags;
	void *data = packet->ebuf;
	u32 tlen = packet->tlen;
	struct hfi1_qp *qp = packet->qp;
	struct hfi1_other_headers *ohdr = packet->ohdr;
	u32 bth0, opcode;
	u32 hdrsize = packet->hlen;
	u32 psn;
	u32 pad;
	struct ib_wc wc;
	u32 pmtu = qp->pmtu;
	struct ib_reth *reth;
	int has_grh = rcv_flags & HFI1_HAS_GRH;
	int ret;
	u32 bth1;

	bth0 = be32_to_cpu(ohdr->bth[0]);
	if (hfi1_ruc_check_hdr(ibp, hdr, has_grh, qp, bth0))
		return;

	bth1 = be32_to_cpu(ohdr->bth[1]);
	if (unlikely(bth1 & (HFI1_BECN_SMASK | HFI1_FECN_SMASK))) {
		if (bth1 & HFI1_BECN_SMASK) {
			struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
			u32 rqpn, lqpn;
			u16 rlid = be16_to_cpu(hdr->lrh[3]);
			u8 sl, sc5;

			lqpn = bth1 & HFI1_QPN_MASK;
			rqpn = qp->remote_qpn;

			sc5 = ibp->sl_to_sc[qp->remote_ah_attr.sl];
			sl = ibp->sc_to_sl[sc5];

			process_becn(ppd, sl, rlid, lqpn, rqpn,
					IB_CC_SVCTYPE_UC);
		}

		if (bth1 & HFI1_FECN_SMASK) {
			struct ib_grh *grh = NULL;
			u16 pkey = (u16)be32_to_cpu(ohdr->bth[0]);
			u16 slid = be16_to_cpu(hdr->lrh[3]);
			u16 dlid = be16_to_cpu(hdr->lrh[1]);
			u32 src_qp = qp->remote_qpn;
			u8 sc5;

			sc5 = ibp->sl_to_sc[qp->remote_ah_attr.sl];
			if (has_grh)
				grh = &hdr->u.l.grh;

			return_cnp(ibp, qp, src_qp, pkey, dlid, slid, sc5,
				   grh);
		}
	}

	psn = be32_to_cpu(ohdr->bth[2]);
	opcode = (bth0 >> 24) & 0xff;

	/* Compare the PSN verses the expected PSN. */
	if (unlikely(cmp_psn(psn, qp->r_psn) != 0)) {
		/*
		 * Handle a sequence error.
		 * Silently drop any current message.
		 */
		qp->r_psn = psn;
inv:
		if (qp->r_state == OP(SEND_FIRST) ||
		    qp->r_state == OP(SEND_MIDDLE)) {
			set_bit(HFI1_R_REWIND_SGE, &qp->r_aflags);
			qp->r_sge.num_sge = 0;
		} else
			hfi1_put_ss(&qp->r_sge);
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
			goto drop;
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

	if (qp->state == IB_QPS_RTR && !(qp->r_flags & HFI1_R_COMM_EST))
		qp_comm_est(qp);

	/* OK, process the packet. */
	switch (opcode) {
	case OP(SEND_FIRST):
	case OP(SEND_ONLY):
	case OP(SEND_ONLY_WITH_IMMEDIATE):
send_first:
		if (test_and_clear_bit(HFI1_R_REWIND_SGE, &qp->r_aflags))
			qp->r_sge = qp->s_rdma_read_sge;
		else {
			ret = hfi1_get_rwqe(qp, 0);
			if (ret < 0)
				goto op_err;
			if (!ret)
				goto drop;
			/*
			 * qp->s_rdma_read_sge will be the owner
			 * of the mr references.
			 */
			qp->s_rdma_read_sge = qp->r_sge;
		}
		qp->r_rcv_len = 0;
		if (opcode == OP(SEND_ONLY))
			goto no_immediate_data;
		else if (opcode == OP(SEND_ONLY_WITH_IMMEDIATE))
			goto send_last_imm;
		/* FALLTHROUGH */
	case OP(SEND_MIDDLE):
		/* Check for invalid length PMTU or posted rwqe len. */
		if (unlikely(tlen != (hdrsize + pmtu + 4)))
			goto rewind;
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len))
			goto rewind;
		hfi1_copy_sge(&qp->r_sge, data, pmtu, 0);
		break;

	case OP(SEND_LAST_WITH_IMMEDIATE):
send_last_imm:
		wc.ex.imm_data = ohdr->u.imm_data;
		wc.wc_flags = IB_WC_WITH_IMM;
		goto send_last;
	case OP(SEND_LAST):
no_immediate_data:
		wc.ex.imm_data = 0;
		wc.wc_flags = 0;
send_last:
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4)))
			goto rewind;
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		wc.byte_len = tlen + qp->r_rcv_len;
		if (unlikely(wc.byte_len > qp->r_len))
			goto rewind;
		wc.opcode = IB_WC_RECV;
		hfi1_copy_sge(&qp->r_sge, data, tlen, 0);
		hfi1_put_ss(&qp->s_rdma_read_sge);
last_imm:
		wc.wr_id = qp->r_wr_id;
		wc.status = IB_WC_SUCCESS;
		wc.qp = &qp->ibqp;
		wc.src_qp = qp->remote_qpn;
		wc.slid = qp->remote_ah_attr.dlid;
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
		wc.sl = qp->remote_ah_attr.sl;
		/* zero fields that are N/A */
		wc.vendor_err = 0;
		wc.pkey_index = 0;
		wc.dlid_path_bits = 0;
		wc.port_num = 0;
		/* Signal completion event if the solicited bit is set. */
		hfi1_cq_enter(to_icq(qp->ibqp.recv_cq), &wc,
			      (ohdr->bth[0] &
				cpu_to_be32(IB_BTH_SOLICITED)) != 0);
		break;

	case OP(RDMA_WRITE_FIRST):
	case OP(RDMA_WRITE_ONLY):
	case OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE): /* consume RWQE */
rdma_first:
		if (unlikely(!(qp->qp_access_flags &
			       IB_ACCESS_REMOTE_WRITE))) {
			goto drop;
		}
		reth = &ohdr->u.rc.reth;
		qp->r_len = be32_to_cpu(reth->length);
		qp->r_rcv_len = 0;
		qp->r_sge.sg_list = NULL;
		if (qp->r_len != 0) {
			u32 rkey = be32_to_cpu(reth->rkey);
			u64 vaddr = be64_to_cpu(reth->vaddr);
			int ok;

			/* Check rkey */
			ok = hfi1_rkey_ok(qp, &qp->r_sge.sge, qp->r_len,
					  vaddr, rkey, IB_ACCESS_REMOTE_WRITE);
			if (unlikely(!ok))
				goto drop;
			qp->r_sge.num_sge = 1;
		} else {
			qp->r_sge.num_sge = 0;
			qp->r_sge.sge.mr = NULL;
			qp->r_sge.sge.vaddr = NULL;
			qp->r_sge.sge.length = 0;
			qp->r_sge.sge.sge_length = 0;
		}
		if (opcode == OP(RDMA_WRITE_ONLY))
			goto rdma_last;
		else if (opcode == OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE)) {
			wc.ex.imm_data = ohdr->u.rc.imm_data;
			goto rdma_last_imm;
		}
		/* FALLTHROUGH */
	case OP(RDMA_WRITE_MIDDLE):
		/* Check for invalid length PMTU or posted rwqe len. */
		if (unlikely(tlen != (hdrsize + pmtu + 4)))
			goto drop;
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len))
			goto drop;
		hfi1_copy_sge(&qp->r_sge, data, pmtu, 1);
		break;

	case OP(RDMA_WRITE_LAST_WITH_IMMEDIATE):
		wc.ex.imm_data = ohdr->u.imm_data;
rdma_last_imm:
		wc.wc_flags = IB_WC_WITH_IMM;

		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4)))
			goto drop;
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		if (unlikely(tlen + qp->r_rcv_len != qp->r_len))
			goto drop;
		if (test_and_clear_bit(HFI1_R_REWIND_SGE, &qp->r_aflags))
			hfi1_put_ss(&qp->s_rdma_read_sge);
		else {
			ret = hfi1_get_rwqe(qp, 1);
			if (ret < 0)
				goto op_err;
			if (!ret)
				goto drop;
		}
		wc.byte_len = qp->r_len;
		wc.opcode = IB_WC_RECV_RDMA_WITH_IMM;
		hfi1_copy_sge(&qp->r_sge, data, tlen, 1);
		hfi1_put_ss(&qp->r_sge);
		goto last_imm;

	case OP(RDMA_WRITE_LAST):
rdma_last:
		/* Get the number of bytes the message was padded by. */
		pad = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4)))
			goto drop;
		/* Don't count the CRC. */
		tlen -= (hdrsize + pad + 4);
		if (unlikely(tlen + qp->r_rcv_len != qp->r_len))
			goto drop;
		hfi1_copy_sge(&qp->r_sge, data, tlen, 1);
		hfi1_put_ss(&qp->r_sge);
		break;

	default:
		/* Drop packet for unknown opcodes. */
		goto drop;
	}
	qp->r_psn++;
	qp->r_state = opcode;
	return;

rewind:
	set_bit(HFI1_R_REWIND_SGE, &qp->r_aflags);
	qp->r_sge.num_sge = 0;
drop:
	ibp->n_pkt_drops++;
	return;

op_err:
	hfi1_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
	return;

}
