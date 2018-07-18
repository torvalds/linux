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

#include "hfi.h"
#include "verbs_txreq.h"
#include "qp.h"

/* cut down ridiculously long IB macro names */
#define OP(x) UC_OP(x)

/**
 * hfi1_make_uc_req - construct a request packet (SEND, RDMA write)
 * @qp: a pointer to the QP
 *
 * Assume s_lock is held.
 *
 * Return 1 if constructed; otherwise, return 0.
 */
int hfi1_make_uc_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_other_headers *ohdr;
	struct rvt_swqe *wqe;
	u32 hwords;
	u32 bth0 = 0;
	u32 len;
	u32 pmtu = qp->pmtu;
	int middle = 0;

	ps->s_txreq = get_txreq(ps->dev, qp);
	if (!ps->s_txreq)
		goto bail_no_tx;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_SEND_OK)) {
		if (!(ib_rvt_state_ops[qp->state] & RVT_FLUSH_SEND))
			goto bail;
		/* We are in the error state, flush the work request. */
		smp_read_barrier_depends(); /* see post_one_send() */
		if (qp->s_last == ACCESS_ONCE(qp->s_head))
			goto bail;
		/* If DMAs are in progress, we can't flush immediately. */
		if (iowait_sdma_pending(&priv->s_iowait)) {
			qp->s_flags |= RVT_S_WAIT_DMA;
			goto bail;
		}
		clear_ahg(qp);
		wqe = rvt_get_swqe_ptr(qp, qp->s_last);
		hfi1_send_complete(qp, wqe, IB_WC_WR_FLUSH_ERR);
		goto done_free_tx;
	}

	ps->s_txreq->phdr.hdr.hdr_type = priv->hdr_type;
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

	/* Get the next send request. */
	wqe = rvt_get_swqe_ptr(qp, qp->s_cur);
	qp->s_wqe = NULL;
	switch (qp->s_state) {
	default:
		if (!(ib_rvt_state_ops[qp->state] &
		    RVT_PROCESS_NEXT_SEND_OK))
			goto bail;
		/* Check if send work queue is empty. */
		smp_read_barrier_depends(); /* see post_one_send() */
		if (qp->s_cur == ACCESS_ONCE(qp->s_head)) {
			clear_ahg(qp);
			goto bail;
		}
		/*
		 * Local operations are processed immediately
		 * after all prior requests have completed.
		 */
		if (wqe->wr.opcode == IB_WR_REG_MR ||
		    wqe->wr.opcode == IB_WR_LOCAL_INV) {
			int local_ops = 0;
			int err = 0;

			if (qp->s_last != qp->s_cur)
				goto bail;
			if (++qp->s_cur == qp->s_size)
				qp->s_cur = 0;
			if (!(wqe->wr.send_flags & RVT_SEND_COMPLETION_ONLY)) {
				err = rvt_invalidate_rkey(
					qp, wqe->wr.ex.invalidate_rkey);
				local_ops = 1;
			}
			hfi1_send_complete(qp, wqe, err ? IB_WC_LOC_PROT_ERR
							: IB_WC_SUCCESS);
			if (local_ops)
				atomic_dec(&qp->local_ops_pending);
			qp->s_hdrwords = 0;
			goto done_free_tx;
		}
		/*
		 * Start a new request.
		 */
		qp->s_psn = wqe->psn;
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
			if (wqe->wr.opcode == IB_WR_SEND) {
				qp->s_state = OP(SEND_ONLY);
			} else {
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
			if (wqe->wr.opcode == IB_WR_RDMA_WRITE) {
				qp->s_state = OP(RDMA_WRITE_ONLY);
			} else {
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
		if (wqe->wr.opcode == IB_WR_SEND) {
			qp->s_state = OP(SEND_LAST);
		} else {
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
		if (wqe->wr.opcode == IB_WR_RDMA_WRITE) {
			qp->s_state = OP(RDMA_WRITE_LAST);
		} else {
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
	ps->s_txreq->sde = priv->s_sde;
	ps->s_txreq->ss = &qp->s_sge;
	ps->s_txreq->s_cur_size = len;
	hfi1_make_ruc_header(qp, ohdr, bth0 | (qp->s_state << 24),
			     mask_psn(qp->s_psn++), middle, ps);
	/* pbc */
	ps->s_txreq->hdr_dwords = qp->s_hdrwords + 2;
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
	qp->s_hdrwords = 0;
	return 0;
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
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	void *data = packet->payload;
	u32 tlen = packet->tlen;
	struct rvt_qp *qp = packet->qp;
	struct ib_other_headers *ohdr = packet->ohdr;
	u32 opcode = packet->opcode;
	u32 hdrsize = packet->hlen;
	u32 psn;
	u32 pad = packet->pad;
	struct ib_wc wc;
	u32 pmtu = qp->pmtu;
	struct ib_reth *reth;
	int ret;
	u8 extra_bytes = pad + packet->extra_byte + (SIZE_OF_CRC << 2);

	if (hfi1_ruc_check_hdr(ibp, packet))
		return;

	process_ecn(qp, packet, true);

	psn = ib_bth_get_psn(ohdr);
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
			set_bit(RVT_R_REWIND_SGE, &qp->r_aflags);
			qp->r_sge.num_sge = 0;
		} else {
			rvt_put_ss(&qp->r_sge);
		}
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

	if (qp->state == IB_QPS_RTR && !(qp->r_flags & RVT_R_COMM_EST))
		rvt_comm_est(qp);

	/* OK, process the packet. */
	switch (opcode) {
	case OP(SEND_FIRST):
	case OP(SEND_ONLY):
	case OP(SEND_ONLY_WITH_IMMEDIATE):
send_first:
		if (test_and_clear_bit(RVT_R_REWIND_SGE, &qp->r_aflags)) {
			qp->r_sge = qp->s_rdma_read_sge;
		} else {
			ret = hfi1_rvt_get_rwqe(qp, 0);
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
		/*
		 * There will be no padding for 9B packet but 16B packets
		 * will come in with some padding since we always add
		 * CRC and LT bytes which will need to be flit aligned
		 */
		if (unlikely(tlen != (hdrsize + pmtu + extra_bytes)))
			goto rewind;
		qp->r_rcv_len += pmtu;
		if (unlikely(qp->r_rcv_len > qp->r_len))
			goto rewind;
		hfi1_copy_sge(&qp->r_sge, data, pmtu, false, false);
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
		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + extra_bytes)))
			goto rewind;
		/* Don't count the CRC. */
		tlen -= (hdrsize + extra_bytes);
		wc.byte_len = tlen + qp->r_rcv_len;
		if (unlikely(wc.byte_len > qp->r_len))
			goto rewind;
		wc.opcode = IB_WC_RECV;
		hfi1_copy_sge(&qp->r_sge, data, tlen, false, false);
		rvt_put_ss(&qp->s_rdma_read_sge);
last_imm:
		wc.wr_id = qp->r_wr_id;
		wc.status = IB_WC_SUCCESS;
		wc.qp = &qp->ibqp;
		wc.src_qp = qp->remote_qpn;
		wc.slid = rdma_ah_get_dlid(&qp->remote_ah_attr);
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
			ok = rvt_rkey_ok(qp, &qp->r_sge.sge, qp->r_len,
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
		if (opcode == OP(RDMA_WRITE_ONLY)) {
			goto rdma_last;
		} else if (opcode == OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE)) {
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
		hfi1_copy_sge(&qp->r_sge, data, pmtu, true, false);
		break;

	case OP(RDMA_WRITE_LAST_WITH_IMMEDIATE):
		wc.ex.imm_data = ohdr->u.imm_data;
rdma_last_imm:
		wc.wc_flags = IB_WC_WITH_IMM;

		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4)))
			goto drop;
		/* Don't count the CRC. */
		tlen -= (hdrsize + extra_bytes);
		if (unlikely(tlen + qp->r_rcv_len != qp->r_len))
			goto drop;
		if (test_and_clear_bit(RVT_R_REWIND_SGE, &qp->r_aflags)) {
			rvt_put_ss(&qp->s_rdma_read_sge);
		} else {
			ret = hfi1_rvt_get_rwqe(qp, 1);
			if (ret < 0)
				goto op_err;
			if (!ret)
				goto drop;
		}
		wc.byte_len = qp->r_len;
		wc.opcode = IB_WC_RECV_RDMA_WITH_IMM;
		hfi1_copy_sge(&qp->r_sge, data, tlen, true, false);
		rvt_put_ss(&qp->r_sge);
		goto last_imm;

	case OP(RDMA_WRITE_LAST):
rdma_last:
		/* Check for invalid length. */
		/* LAST len should be >= 1 */
		if (unlikely(tlen < (hdrsize + pad + 4)))
			goto drop;
		/* Don't count the CRC. */
		tlen -= (hdrsize + extra_bytes);
		if (unlikely(tlen + qp->r_rcv_len != qp->r_len))
			goto drop;
		hfi1_copy_sge(&qp->r_sge, data, tlen, true, false);
		rvt_put_ss(&qp->r_sge);
		break;

	default:
		/* Drop packet for unknown opcodes. */
		goto drop;
	}
	qp->r_psn++;
	qp->r_state = opcode;
	return;

rewind:
	set_bit(RVT_R_REWIND_SGE, &qp->r_aflags);
	qp->r_sge.num_sge = 0;
drop:
	ibp->rvp.n_pkt_drops++;
	return;

op_err:
	rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
}
