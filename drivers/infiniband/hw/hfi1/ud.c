/*
 * Copyright(c) 2015, 2016 Intel Corporation.
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

#include <linux/net.h>
#include <rdma/ib_smi.h>

#include "hfi.h"
#include "mad.h"
#include "verbs_txreq.h"
#include "qp.h"

/**
 * ud_loopback - handle send on loopback QPs
 * @sqp: the sending QP
 * @swqe: the send work request
 *
 * This is called from hfi1_make_ud_req() to forward a WQE addressed
 * to the same HFI.
 * Note that the receive interrupt handler may be calling hfi1_ud_rcv()
 * while this is being called.
 */
static void ud_loopback(struct rvt_qp *sqp, struct rvt_swqe *swqe)
{
	struct hfi1_ibport *ibp = to_iport(sqp->ibqp.device, sqp->port_num);
	struct hfi1_pportdata *ppd;
	struct rvt_qp *qp;
	struct ib_ah_attr *ah_attr;
	unsigned long flags;
	struct rvt_sge_state ssge;
	struct rvt_sge *sge;
	struct ib_wc wc;
	u32 length;
	enum ib_qp_type sqptype, dqptype;

	rcu_read_lock();

	qp = rvt_lookup_qpn(ib_to_rvt(sqp->ibqp.device), &ibp->rvp,
			    swqe->ud_wr.remote_qpn);
	if (!qp) {
		ibp->rvp.n_pkt_drops++;
		rcu_read_unlock();
		return;
	}

	sqptype = sqp->ibqp.qp_type == IB_QPT_GSI ?
			IB_QPT_UD : sqp->ibqp.qp_type;
	dqptype = qp->ibqp.qp_type == IB_QPT_GSI ?
			IB_QPT_UD : qp->ibqp.qp_type;

	if (dqptype != sqptype ||
	    !(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK)) {
		ibp->rvp.n_pkt_drops++;
		goto drop;
	}

	ah_attr = &ibah_to_rvtah(swqe->ud_wr.ah)->attr;
	ppd = ppd_from_ibp(ibp);

	if (qp->ibqp.qp_num > 1) {
		u16 pkey;
		u16 slid;
		u8 sc5 = ibp->sl_to_sc[ah_attr->sl];

		pkey = hfi1_get_pkey(ibp, sqp->s_pkey_index);
		slid = ppd->lid | (ah_attr->src_path_bits &
				   ((1 << ppd->lmc) - 1));
		if (unlikely(ingress_pkey_check(ppd, pkey, sc5,
						qp->s_pkey_index, slid))) {
			hfi1_bad_pqkey(ibp, OPA_TRAP_BAD_P_KEY, pkey,
				       ah_attr->sl,
				       sqp->ibqp.qp_num, qp->ibqp.qp_num,
				       slid, ah_attr->dlid);
			goto drop;
		}
	}

	/*
	 * Check that the qkey matches (except for QP0, see 9.6.1.4.1).
	 * Qkeys with the high order bit set mean use the
	 * qkey from the QP context instead of the WR (see 10.2.5).
	 */
	if (qp->ibqp.qp_num) {
		u32 qkey;

		qkey = (int)swqe->ud_wr.remote_qkey < 0 ?
			sqp->qkey : swqe->ud_wr.remote_qkey;
		if (unlikely(qkey != qp->qkey)) {
			u16 lid;

			lid = ppd->lid | (ah_attr->src_path_bits &
					  ((1 << ppd->lmc) - 1));
			hfi1_bad_pqkey(ibp, OPA_TRAP_BAD_Q_KEY, qkey,
				       ah_attr->sl,
				       sqp->ibqp.qp_num, qp->ibqp.qp_num,
				       lid,
				       ah_attr->dlid);
			goto drop;
		}
	}

	/*
	 * A GRH is expected to precede the data even if not
	 * present on the wire.
	 */
	length = swqe->length;
	memset(&wc, 0, sizeof(wc));
	wc.byte_len = length + sizeof(struct ib_grh);

	if (swqe->wr.opcode == IB_WR_SEND_WITH_IMM) {
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.ex.imm_data = swqe->wr.ex.imm_data;
	}

	spin_lock_irqsave(&qp->r_lock, flags);

	/*
	 * Get the next work request entry to find where to put the data.
	 */
	if (qp->r_flags & RVT_R_REUSE_SGE) {
		qp->r_flags &= ~RVT_R_REUSE_SGE;
	} else {
		int ret;

		ret = hfi1_rvt_get_rwqe(qp, 0);
		if (ret < 0) {
			rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
			goto bail_unlock;
		}
		if (!ret) {
			if (qp->ibqp.qp_num == 0)
				ibp->rvp.n_vl15_dropped++;
			goto bail_unlock;
		}
	}
	/* Silently drop packets which are too big. */
	if (unlikely(wc.byte_len > qp->r_len)) {
		qp->r_flags |= RVT_R_REUSE_SGE;
		ibp->rvp.n_pkt_drops++;
		goto bail_unlock;
	}

	if (ah_attr->ah_flags & IB_AH_GRH) {
		struct ib_grh grh;
		struct ib_global_route grd = ah_attr->grh;

		hfi1_make_grh(ibp, &grh, &grd, 0, 0);
		hfi1_copy_sge(&qp->r_sge, &grh,
			      sizeof(grh), true, false);
		wc.wc_flags |= IB_WC_GRH;
	} else {
		hfi1_skip_sge(&qp->r_sge, sizeof(struct ib_grh), true);
	}
	ssge.sg_list = swqe->sg_list + 1;
	ssge.sge = *swqe->sg_list;
	ssge.num_sge = swqe->wr.num_sge;
	sge = &ssge.sge;
	while (length) {
		u32 len = sge->length;

		if (len > length)
			len = length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		WARN_ON_ONCE(len == 0);
		hfi1_copy_sge(&qp->r_sge, sge->vaddr, len, true, false);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--ssge.num_sge)
				*sge = *ssge.sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= RVT_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}
		length -= len;
	}
	rvt_put_ss(&qp->r_sge);
	if (!test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags))
		goto bail_unlock;
	wc.wr_id = qp->r_wr_id;
	wc.status = IB_WC_SUCCESS;
	wc.opcode = IB_WC_RECV;
	wc.qp = &qp->ibqp;
	wc.src_qp = sqp->ibqp.qp_num;
	if (qp->ibqp.qp_type == IB_QPT_GSI || qp->ibqp.qp_type == IB_QPT_SMI) {
		if (sqp->ibqp.qp_type == IB_QPT_GSI ||
		    sqp->ibqp.qp_type == IB_QPT_SMI)
			wc.pkey_index = swqe->ud_wr.pkey_index;
		else
			wc.pkey_index = sqp->s_pkey_index;
	} else {
		wc.pkey_index = 0;
	}
	wc.slid = ppd->lid | (ah_attr->src_path_bits & ((1 << ppd->lmc) - 1));
	/* Check for loopback when the port lid is not set */
	if (wc.slid == 0 && sqp->ibqp.qp_type == IB_QPT_GSI)
		wc.slid = be16_to_cpu(IB_LID_PERMISSIVE);
	wc.sl = ah_attr->sl;
	wc.dlid_path_bits = ah_attr->dlid & ((1 << ppd->lmc) - 1);
	wc.port_num = qp->port_num;
	/* Signal completion event if the solicited bit is set. */
	rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc,
		     swqe->wr.send_flags & IB_SEND_SOLICITED);
	ibp->rvp.n_loop_pkts++;
bail_unlock:
	spin_unlock_irqrestore(&qp->r_lock, flags);
drop:
	rcu_read_unlock();
}

/**
 * hfi1_make_ud_req - construct a UD request packet
 * @qp: the QP
 *
 * Assume s_lock is held.
 *
 * Return 1 if constructed; otherwise, return 0.
 */
int hfi1_make_ud_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_other_headers *ohdr;
	struct ib_ah_attr *ah_attr;
	struct hfi1_pportdata *ppd;
	struct hfi1_ibport *ibp;
	struct rvt_swqe *wqe;
	u32 nwords;
	u32 extra_bytes;
	u32 bth0;
	u16 lrh0;
	u16 lid;
	int next_cur;
	u8 sc5;

	ps->s_txreq = get_txreq(ps->dev, qp);
	if (IS_ERR(ps->s_txreq))
		goto bail_no_tx;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_NEXT_SEND_OK)) {
		if (!(ib_rvt_state_ops[qp->state] & RVT_FLUSH_SEND))
			goto bail;
		/* We are in the error state, flush the work request. */
		smp_read_barrier_depends(); /* see post_one_send */
		if (qp->s_last == ACCESS_ONCE(qp->s_head))
			goto bail;
		/* If DMAs are in progress, we can't flush immediately. */
		if (iowait_sdma_pending(&priv->s_iowait)) {
			qp->s_flags |= RVT_S_WAIT_DMA;
			goto bail;
		}
		wqe = rvt_get_swqe_ptr(qp, qp->s_last);
		hfi1_send_complete(qp, wqe, IB_WC_WR_FLUSH_ERR);
		goto done_free_tx;
	}

	/* see post_one_send() */
	smp_read_barrier_depends();
	if (qp->s_cur == ACCESS_ONCE(qp->s_head))
		goto bail;

	wqe = rvt_get_swqe_ptr(qp, qp->s_cur);
	next_cur = qp->s_cur + 1;
	if (next_cur >= qp->s_size)
		next_cur = 0;

	/* Construct the header. */
	ibp = to_iport(qp->ibqp.device, qp->port_num);
	ppd = ppd_from_ibp(ibp);
	ah_attr = &ibah_to_rvtah(wqe->ud_wr.ah)->attr;
	if (ah_attr->dlid < be16_to_cpu(IB_MULTICAST_LID_BASE) ||
	    ah_attr->dlid == be16_to_cpu(IB_LID_PERMISSIVE)) {
		lid = ah_attr->dlid & ~((1 << ppd->lmc) - 1);
		if (unlikely(!loopback &&
			     (lid == ppd->lid ||
			      (lid == be16_to_cpu(IB_LID_PERMISSIVE) &&
			      qp->ibqp.qp_type == IB_QPT_GSI)))) {
			unsigned long tflags = ps->flags;
			/*
			 * If DMAs are in progress, we can't generate
			 * a completion for the loopback packet since
			 * it would be out of order.
			 * Instead of waiting, we could queue a
			 * zero length descriptor so we get a callback.
			 */
			if (iowait_sdma_pending(&priv->s_iowait)) {
				qp->s_flags |= RVT_S_WAIT_DMA;
				goto bail;
			}
			qp->s_cur = next_cur;
			spin_unlock_irqrestore(&qp->s_lock, tflags);
			ud_loopback(qp, wqe);
			spin_lock_irqsave(&qp->s_lock, tflags);
			ps->flags = tflags;
			hfi1_send_complete(qp, wqe, IB_WC_SUCCESS);
			goto done_free_tx;
		}
	}

	qp->s_cur = next_cur;
	extra_bytes = -wqe->length & 3;
	nwords = (wqe->length + extra_bytes) >> 2;

	/* header size in 32-bit words LRH+BTH+DETH = (8+12+8)/4. */
	qp->s_hdrwords = 7;
	ps->s_txreq->s_cur_size = wqe->length;
	ps->s_txreq->ss = &qp->s_sge;
	qp->s_srate = ah_attr->static_rate;
	qp->srate_mbps = ib_rate_to_mbps(qp->s_srate);
	qp->s_wqe = wqe;
	qp->s_sge.sge = wqe->sg_list[0];
	qp->s_sge.sg_list = wqe->sg_list + 1;
	qp->s_sge.num_sge = wqe->wr.num_sge;
	qp->s_sge.total_len = wqe->length;

	if (ah_attr->ah_flags & IB_AH_GRH) {
		/* Header size in 32-bit words. */
		qp->s_hdrwords += hfi1_make_grh(ibp,
						&ps->s_txreq->phdr.hdr.u.l.grh,
						&ah_attr->grh,
						qp->s_hdrwords, nwords);
		lrh0 = HFI1_LRH_GRH;
		ohdr = &ps->s_txreq->phdr.hdr.u.l.oth;
		/*
		 * Don't worry about sending to locally attached multicast
		 * QPs.  It is unspecified by the spec. what happens.
		 */
	} else {
		/* Header size in 32-bit words. */
		lrh0 = HFI1_LRH_BTH;
		ohdr = &ps->s_txreq->phdr.hdr.u.oth;
	}
	if (wqe->wr.opcode == IB_WR_SEND_WITH_IMM) {
		qp->s_hdrwords++;
		ohdr->u.ud.imm_data = wqe->wr.ex.imm_data;
		bth0 = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE << 24;
	} else {
		bth0 = IB_OPCODE_UD_SEND_ONLY << 24;
	}
	sc5 = ibp->sl_to_sc[ah_attr->sl];
	lrh0 |= (ah_attr->sl & 0xf) << 4;
	if (qp->ibqp.qp_type == IB_QPT_SMI) {
		lrh0 |= 0xF000; /* Set VL (see ch. 13.5.3.1) */
		priv->s_sc = 0xf;
	} else {
		lrh0 |= (sc5 & 0xf) << 12;
		priv->s_sc = sc5;
	}
	priv->s_sde = qp_to_sdma_engine(qp, priv->s_sc);
	ps->s_txreq->sde = priv->s_sde;
	priv->s_sendcontext = qp_to_send_context(qp, priv->s_sc);
	ps->s_txreq->psc = priv->s_sendcontext;
	ps->s_txreq->phdr.hdr.lrh[0] = cpu_to_be16(lrh0);
	ps->s_txreq->phdr.hdr.lrh[1] = cpu_to_be16(ah_attr->dlid);
	ps->s_txreq->phdr.hdr.lrh[2] =
		cpu_to_be16(qp->s_hdrwords + nwords + SIZE_OF_CRC);
	if (ah_attr->dlid == be16_to_cpu(IB_LID_PERMISSIVE)) {
		ps->s_txreq->phdr.hdr.lrh[3] = IB_LID_PERMISSIVE;
	} else {
		lid = ppd->lid;
		if (lid) {
			lid |= ah_attr->src_path_bits & ((1 << ppd->lmc) - 1);
			ps->s_txreq->phdr.hdr.lrh[3] = cpu_to_be16(lid);
		} else {
			ps->s_txreq->phdr.hdr.lrh[3] = IB_LID_PERMISSIVE;
		}
	}
	if (wqe->wr.send_flags & IB_SEND_SOLICITED)
		bth0 |= IB_BTH_SOLICITED;
	bth0 |= extra_bytes << 20;
	if (qp->ibqp.qp_type == IB_QPT_GSI || qp->ibqp.qp_type == IB_QPT_SMI)
		bth0 |= hfi1_get_pkey(ibp, wqe->ud_wr.pkey_index);
	else
		bth0 |= hfi1_get_pkey(ibp, qp->s_pkey_index);
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(wqe->ud_wr.remote_qpn);
	ohdr->bth[2] = cpu_to_be32(mask_psn(wqe->psn));
	/*
	 * Qkeys with the high order bit set mean use the
	 * qkey from the QP context instead of the WR (see 10.2.5).
	 */
	ohdr->u.ud.deth[0] = cpu_to_be32((int)wqe->ud_wr.remote_qkey < 0 ?
					 qp->qkey : wqe->ud_wr.remote_qkey);
	ohdr->u.ud.deth[1] = cpu_to_be32(qp->ibqp.qp_num);
	/* disarm any ahg */
	priv->s_ahg->ahgcount = 0;
	priv->s_ahg->ahgidx = 0;
	priv->s_ahg->tx_flags = 0;
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

/*
 * Hardware can't check this so we do it here.
 *
 * This is a slightly different algorithm than the standard pkey check.  It
 * special cases the management keys and allows for 0x7fff and 0xffff to be in
 * the table at the same time.
 *
 * @returns the index found or -1 if not found
 */
int hfi1_lookup_pkey_idx(struct hfi1_ibport *ibp, u16 pkey)
{
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	unsigned i;

	if (pkey == FULL_MGMT_P_KEY || pkey == LIM_MGMT_P_KEY) {
		unsigned lim_idx = -1;

		for (i = 0; i < ARRAY_SIZE(ppd->pkeys); ++i) {
			/* here we look for an exact match */
			if (ppd->pkeys[i] == pkey)
				return i;
			if (ppd->pkeys[i] == LIM_MGMT_P_KEY)
				lim_idx = i;
		}

		/* did not find 0xffff return 0x7fff idx if found */
		if (pkey == FULL_MGMT_P_KEY)
			return lim_idx;

		/* no match...  */
		return -1;
	}

	pkey &= 0x7fff; /* remove limited/full membership bit */

	for (i = 0; i < ARRAY_SIZE(ppd->pkeys); ++i)
		if ((ppd->pkeys[i] & 0x7fff) == pkey)
			return i;

	/*
	 * Should not get here, this means hardware failed to validate pkeys.
	 */
	return -1;
}

void return_cnp(struct hfi1_ibport *ibp, struct rvt_qp *qp, u32 remote_qpn,
		u32 pkey, u32 slid, u32 dlid, u8 sc5,
		const struct ib_grh *old_grh)
{
	u64 pbc, pbc_flags = 0;
	u32 bth0, plen, vl, hwords = 5;
	u16 lrh0;
	u8 sl = ibp->sc_to_sl[sc5];
	struct ib_header hdr;
	struct ib_other_headers *ohdr;
	struct pio_buf *pbuf;
	struct send_context *ctxt = qp_to_send_context(qp, sc5);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

	if (old_grh) {
		struct ib_grh *grh = &hdr.u.l.grh;

		grh->version_tclass_flow = old_grh->version_tclass_flow;
		grh->paylen = cpu_to_be16((hwords - 2 + SIZE_OF_CRC) << 2);
		grh->hop_limit = 0xff;
		grh->sgid = old_grh->dgid;
		grh->dgid = old_grh->sgid;
		ohdr = &hdr.u.l.oth;
		lrh0 = HFI1_LRH_GRH;
		hwords += sizeof(struct ib_grh) / sizeof(u32);
	} else {
		ohdr = &hdr.u.oth;
		lrh0 = HFI1_LRH_BTH;
	}

	lrh0 |= (sc5 & 0xf) << 12 | sl << 4;

	bth0 = pkey | (IB_OPCODE_CNP << 24);
	ohdr->bth[0] = cpu_to_be32(bth0);

	ohdr->bth[1] = cpu_to_be32(remote_qpn | (1 << HFI1_BECN_SHIFT));
	ohdr->bth[2] = 0; /* PSN 0 */

	hdr.lrh[0] = cpu_to_be16(lrh0);
	hdr.lrh[1] = cpu_to_be16(dlid);
	hdr.lrh[2] = cpu_to_be16(hwords + SIZE_OF_CRC);
	hdr.lrh[3] = cpu_to_be16(slid);

	plen = 2 /* PBC */ + hwords;
	pbc_flags |= (!!(sc5 & 0x10)) << PBC_DC_INFO_SHIFT;
	vl = sc_to_vlt(ppd->dd, sc5);
	pbc = create_pbc(ppd, pbc_flags, qp->srate_mbps, vl, plen);
	if (ctxt) {
		pbuf = sc_buffer_alloc(ctxt, plen, NULL, NULL);
		if (pbuf)
			ppd->dd->pio_inline_send(ppd->dd, pbuf, pbc,
						 &hdr, hwords);
	}
}

/*
 * opa_smp_check() - Do the regular pkey checking, and the additional
 * checks for SMPs specified in OPAv1 rev 1.0, 9/19/2016 update, section
 * 9.10.25 ("SMA Packet Checks").
 *
 * Note that:
 *   - Checks are done using the pkey directly from the packet's BTH,
 *     and specifically _not_ the pkey that we attach to the completion,
 *     which may be different.
 *   - These checks are specifically for "non-local" SMPs (i.e., SMPs
 *     which originated on another node). SMPs which are sent from, and
 *     destined to this node are checked in opa_local_smp_check().
 *
 * At the point where opa_smp_check() is called, we know:
 *   - destination QP is QP0
 *
 * opa_smp_check() returns 0 if all checks succeed, 1 otherwise.
 */
static int opa_smp_check(struct hfi1_ibport *ibp, u16 pkey, u8 sc5,
			 struct rvt_qp *qp, u16 slid, struct opa_smp *smp)
{
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

	/*
	 * I don't think it's possible for us to get here with sc != 0xf,
	 * but check it to be certain.
	 */
	if (sc5 != 0xf)
		return 1;

	if (rcv_pkey_check(ppd, pkey, sc5, slid))
		return 1;

	/*
	 * At this point we know (and so don't need to check again) that
	 * the pkey is either LIM_MGMT_P_KEY, or FULL_MGMT_P_KEY
	 * (see ingress_pkey_check).
	 */
	if (smp->mgmt_class != IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE &&
	    smp->mgmt_class != IB_MGMT_CLASS_SUBN_LID_ROUTED) {
		ingress_pkey_table_fail(ppd, pkey, slid);
		return 1;
	}

	/*
	 * SMPs fall into one of four (disjoint) categories:
	 * SMA request, SMA response, SMA trap, or SMA trap repress.
	 * Our response depends, in part, on which type of SMP we're
	 * processing.
	 *
	 * If this is an SMA response, skip the check here.
	 *
	 * If this is an SMA request or SMA trap repress:
	 *   - pkey != FULL_MGMT_P_KEY =>
	 *       increment port recv constraint errors, drop MAD
	 *
	 * Otherwise:
	 *    - accept if the port is running an SM
	 *    - drop MAD if it's an SMA trap
	 *    - pkey == FULL_MGMT_P_KEY =>
	 *        reply with unsupported method
	 *    - pkey != FULL_MGMT_P_KEY =>
	 *	  increment port recv constraint errors, drop MAD
	 */
	switch (smp->method) {
	case IB_MGMT_METHOD_GET_RESP:
	case IB_MGMT_METHOD_REPORT_RESP:
		break;
	case IB_MGMT_METHOD_GET:
	case IB_MGMT_METHOD_SET:
	case IB_MGMT_METHOD_REPORT:
	case IB_MGMT_METHOD_TRAP_REPRESS:
		if (pkey != FULL_MGMT_P_KEY) {
			ingress_pkey_table_fail(ppd, pkey, slid);
			return 1;
		}
		break;
	default:
		if (ibp->rvp.port_cap_flags & IB_PORT_SM)
			return 0;
		if (smp->method == IB_MGMT_METHOD_TRAP)
			return 1;
		if (pkey == FULL_MGMT_P_KEY) {
			smp->status |= IB_SMP_UNSUP_METHOD;
			return 0;
		}
		ingress_pkey_table_fail(ppd, pkey, slid);
		return 1;
	}
	return 0;
}

/**
 * hfi1_ud_rcv - receive an incoming UD packet
 * @ibp: the port the packet came in on
 * @hdr: the packet header
 * @rcv_flags: flags relevant to rcv processing
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP the packet came on
 *
 * This is called from qp_rcv() to process an incoming UD packet
 * for the given QP.
 * Called at interrupt level.
 */
void hfi1_ud_rcv(struct hfi1_packet *packet)
{
	struct ib_other_headers *ohdr = packet->ohdr;
	int opcode;
	u32 hdrsize = packet->hlen;
	struct ib_wc wc;
	u32 qkey;
	u32 src_qp;
	u16 dlid, pkey;
	int mgmt_pkey_idx = -1;
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct ib_header *hdr = packet->hdr;
	u32 rcv_flags = packet->rcv_flags;
	void *data = packet->ebuf;
	u32 tlen = packet->tlen;
	struct rvt_qp *qp = packet->qp;
	bool has_grh = rcv_flags & HFI1_HAS_GRH;
	u8 sc5 = hdr2sc(hdr, packet->rhf);
	u32 bth1;
	u8 sl_from_sc, sl;
	u16 slid;
	u8 extra_bytes;

	qkey = be32_to_cpu(ohdr->u.ud.deth[0]);
	src_qp = be32_to_cpu(ohdr->u.ud.deth[1]) & RVT_QPN_MASK;
	dlid = be16_to_cpu(hdr->lrh[1]);
	bth1 = be32_to_cpu(ohdr->bth[1]);
	slid = be16_to_cpu(hdr->lrh[3]);
	pkey = (u16)be32_to_cpu(ohdr->bth[0]);
	sl = (be16_to_cpu(hdr->lrh[0]) >> 4) & 0xf;
	extra_bytes = (be32_to_cpu(ohdr->bth[0]) >> 20) & 3;
	extra_bytes += (SIZE_OF_CRC << 2);
	sl_from_sc = ibp->sc_to_sl[sc5];

	opcode = be32_to_cpu(ohdr->bth[0]) >> 24;
	opcode &= 0xff;

	process_ecn(qp, packet, (opcode != IB_OPCODE_CNP));
	/*
	 * Get the number of bytes the message was padded by
	 * and drop incomplete packets.
	 */
	if (unlikely(tlen < (hdrsize + extra_bytes)))
		goto drop;

	tlen -= hdrsize + extra_bytes;

	/*
	 * Check that the permissive LID is only used on QP0
	 * and the QKEY matches (see 9.6.1.4.1 and 9.6.1.5.1).
	 */
	if (qp->ibqp.qp_num) {
		if (unlikely(hdr->lrh[1] == IB_LID_PERMISSIVE ||
			     hdr->lrh[3] == IB_LID_PERMISSIVE))
			goto drop;
		if (qp->ibqp.qp_num > 1) {
			if (unlikely(rcv_pkey_check(ppd, pkey, sc5, slid))) {
				/*
				 * Traps will not be sent for packets dropped
				 * by the HW. This is fine, as sending trap
				 * for invalid pkeys is optional according to
				 * IB spec (release 1.3, section 10.9.4)
				 */
				hfi1_bad_pqkey(ibp, OPA_TRAP_BAD_P_KEY,
					       pkey, sl,
					       src_qp, qp->ibqp.qp_num,
					       slid, dlid);
				return;
			}
		} else {
			/* GSI packet */
			mgmt_pkey_idx = hfi1_lookup_pkey_idx(ibp, pkey);
			if (mgmt_pkey_idx < 0)
				goto drop;
		}
		if (unlikely(qkey != qp->qkey)) {
			hfi1_bad_pqkey(ibp, OPA_TRAP_BAD_Q_KEY, qkey, sl,
				       src_qp, qp->ibqp.qp_num,
				       slid, dlid);
			return;
		}
		/* Drop invalid MAD packets (see 13.5.3.1). */
		if (unlikely(qp->ibqp.qp_num == 1 &&
			     (tlen > 2048 || (sc5 == 0xF))))
			goto drop;
	} else {
		/* Received on QP0, and so by definition, this is an SMP */
		struct opa_smp *smp = (struct opa_smp *)data;

		if (opa_smp_check(ibp, pkey, sc5, qp, slid, smp))
			goto drop;

		if (tlen > 2048)
			goto drop;
		if ((hdr->lrh[1] == IB_LID_PERMISSIVE ||
		     hdr->lrh[3] == IB_LID_PERMISSIVE) &&
		    smp->mgmt_class != IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
			goto drop;

		/* look up SMI pkey */
		mgmt_pkey_idx = hfi1_lookup_pkey_idx(ibp, pkey);
		if (mgmt_pkey_idx < 0)
			goto drop;
	}

	if (qp->ibqp.qp_num > 1 &&
	    opcode == IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE) {
		wc.ex.imm_data = ohdr->u.ud.imm_data;
		wc.wc_flags = IB_WC_WITH_IMM;
		tlen -= sizeof(u32);
	} else if (opcode == IB_OPCODE_UD_SEND_ONLY) {
		wc.ex.imm_data = 0;
		wc.wc_flags = 0;
	} else {
		goto drop;
	}

	/*
	 * A GRH is expected to precede the data even if not
	 * present on the wire.
	 */
	wc.byte_len = tlen + sizeof(struct ib_grh);

	/*
	 * Get the next work request entry to find where to put the data.
	 */
	if (qp->r_flags & RVT_R_REUSE_SGE) {
		qp->r_flags &= ~RVT_R_REUSE_SGE;
	} else {
		int ret;

		ret = hfi1_rvt_get_rwqe(qp, 0);
		if (ret < 0) {
			rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
			return;
		}
		if (!ret) {
			if (qp->ibqp.qp_num == 0)
				ibp->rvp.n_vl15_dropped++;
			return;
		}
	}
	/* Silently drop packets which are too big. */
	if (unlikely(wc.byte_len > qp->r_len)) {
		qp->r_flags |= RVT_R_REUSE_SGE;
		goto drop;
	}
	if (has_grh) {
		hfi1_copy_sge(&qp->r_sge, &hdr->u.l.grh,
			      sizeof(struct ib_grh), true, false);
		wc.wc_flags |= IB_WC_GRH;
	} else {
		hfi1_skip_sge(&qp->r_sge, sizeof(struct ib_grh), true);
	}
	hfi1_copy_sge(&qp->r_sge, data, wc.byte_len - sizeof(struct ib_grh),
		      true, false);
	rvt_put_ss(&qp->r_sge);
	if (!test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags))
		return;
	wc.wr_id = qp->r_wr_id;
	wc.status = IB_WC_SUCCESS;
	wc.opcode = IB_WC_RECV;
	wc.vendor_err = 0;
	wc.qp = &qp->ibqp;
	wc.src_qp = src_qp;

	if (qp->ibqp.qp_type == IB_QPT_GSI ||
	    qp->ibqp.qp_type == IB_QPT_SMI) {
		if (mgmt_pkey_idx < 0) {
			if (net_ratelimit()) {
				struct hfi1_devdata *dd = ppd->dd;

				dd_dev_err(dd, "QP type %d mgmt_pkey_idx < 0 and packet not dropped???\n",
					   qp->ibqp.qp_type);
				mgmt_pkey_idx = 0;
			}
		}
		wc.pkey_index = (unsigned)mgmt_pkey_idx;
	} else {
		wc.pkey_index = 0;
	}

	wc.slid = slid;
	wc.sl = sl_from_sc;

	/*
	 * Save the LMC lower bits if the destination LID is a unicast LID.
	 */
	wc.dlid_path_bits = dlid >= be16_to_cpu(IB_MULTICAST_LID_BASE) ? 0 :
		dlid & ((1 << ppd_from_ibp(ibp)->lmc) - 1);
	wc.port_num = qp->port_num;
	/* Signal completion event if the solicited bit is set. */
	rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc,
		     (ohdr->bth[0] &
		      cpu_to_be32(IB_BTH_SOLICITED)) != 0);
	return;

drop:
	ibp->rvp.n_pkt_drops++;
}
