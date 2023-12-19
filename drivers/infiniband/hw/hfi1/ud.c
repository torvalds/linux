// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright(c) 2015 - 2019 Intel Corporation.
 */

#include <linux/net.h>
#include <rdma/ib_smi.h>

#include "hfi.h"
#include "mad.h"
#include "verbs_txreq.h"
#include "trace_ibhdrs.h"
#include "qp.h"

/* We support only two types - 9B and 16B for now */
static const hfi1_make_req hfi1_make_ud_req_tbl[2] = {
	[HFI1_PKT_TYPE_9B] = &hfi1_make_ud_req_9B,
	[HFI1_PKT_TYPE_16B] = &hfi1_make_ud_req_16B
};

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
	struct hfi1_qp_priv *priv = sqp->priv;
	struct rvt_qp *qp;
	struct rdma_ah_attr *ah_attr;
	unsigned long flags;
	struct rvt_sge_state ssge;
	struct rvt_sge *sge;
	struct ib_wc wc;
	u32 length;
	enum ib_qp_type sqptype, dqptype;

	rcu_read_lock();

	qp = rvt_lookup_qpn(ib_to_rvt(sqp->ibqp.device), &ibp->rvp,
			    rvt_get_swqe_remote_qpn(swqe));
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

	ah_attr = rvt_get_swqe_ah_attr(swqe);
	ppd = ppd_from_ibp(ibp);

	if (qp->ibqp.qp_num > 1) {
		u16 pkey;
		u32 slid;
		u8 sc5 = ibp->sl_to_sc[rdma_ah_get_sl(ah_attr)];

		pkey = hfi1_get_pkey(ibp, sqp->s_pkey_index);
		slid = ppd->lid | (rdma_ah_get_path_bits(ah_attr) &
				   ((1 << ppd->lmc) - 1));
		if (unlikely(ingress_pkey_check(ppd, pkey, sc5,
						qp->s_pkey_index,
						slid, false))) {
			hfi1_bad_pkey(ibp, pkey,
				      rdma_ah_get_sl(ah_attr),
				      sqp->ibqp.qp_num, qp->ibqp.qp_num,
				      slid, rdma_ah_get_dlid(ah_attr));
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

		qkey = (int)rvt_get_swqe_remote_qkey(swqe) < 0 ?
			sqp->qkey : rvt_get_swqe_remote_qkey(swqe);
		if (unlikely(qkey != qp->qkey))
			goto drop; /* silently drop per IBTA spec */
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

		ret = rvt_get_rwqe(qp, false);
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

	if (rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH) {
		struct ib_grh grh;
		struct ib_global_route grd = *(rdma_ah_read_grh(ah_attr));

		/*
		 * For loopback packets with extended LIDs, the
		 * sgid_index in the GRH is 0 and the dgid is
		 * OPA GID of the sender. While creating a response
		 * to the loopback packet, IB core creates the new
		 * sgid_index from the DGID and that will be the
		 * OPA_GID_INDEX. The new dgid is from the sgid
		 * index and that will be in the IB GID format.
		 *
		 * We now have a case where the sent packet had a
		 * different sgid_index and dgid compared to the
		 * one that was received in response.
		 *
		 * Fix this inconsistency.
		 */
		if (priv->hdr_type == HFI1_PKT_TYPE_16B) {
			if (grd.sgid_index == 0)
				grd.sgid_index = OPA_GID_INDEX;

			if (ib_is_opa_gid(&grd.dgid))
				grd.dgid.global.interface_id =
				cpu_to_be64(ppd->guids[HFI1_PORT_GUID_INDEX]);
		}

		hfi1_make_grh(ibp, &grh, &grd, 0, 0);
		rvt_copy_sge(qp, &qp->r_sge, &grh,
			     sizeof(grh), true, false);
		wc.wc_flags |= IB_WC_GRH;
	} else {
		rvt_skip_sge(&qp->r_sge, sizeof(struct ib_grh), true);
	}
	ssge.sg_list = swqe->sg_list + 1;
	ssge.sge = *swqe->sg_list;
	ssge.num_sge = swqe->wr.num_sge;
	sge = &ssge.sge;
	while (length) {
		u32 len = rvt_get_sge_length(sge, length);

		WARN_ON_ONCE(len == 0);
		rvt_copy_sge(qp, &qp->r_sge, sge->vaddr, len, true, false);
		rvt_update_sge(&ssge, len, false);
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
			wc.pkey_index = rvt_get_swqe_pkey_index(swqe);
		else
			wc.pkey_index = sqp->s_pkey_index;
	} else {
		wc.pkey_index = 0;
	}
	wc.slid = (ppd->lid | (rdma_ah_get_path_bits(ah_attr) &
				   ((1 << ppd->lmc) - 1))) & U16_MAX;
	/* Check for loopback when the port lid is not set */
	if (wc.slid == 0 && sqp->ibqp.qp_type == IB_QPT_GSI)
		wc.slid = be16_to_cpu(IB_LID_PERMISSIVE);
	wc.sl = rdma_ah_get_sl(ah_attr);
	wc.dlid_path_bits = rdma_ah_get_dlid(ah_attr) & ((1 << ppd->lmc) - 1);
	wc.port_num = qp->port_num;
	/* Signal completion event if the solicited bit is set. */
	rvt_recv_cq(qp, &wc, swqe->wr.send_flags & IB_SEND_SOLICITED);
	ibp->rvp.n_loop_pkts++;
bail_unlock:
	spin_unlock_irqrestore(&qp->r_lock, flags);
drop:
	rcu_read_unlock();
}

static void hfi1_make_bth_deth(struct rvt_qp *qp, struct rvt_swqe *wqe,
			       struct ib_other_headers *ohdr,
			       u16 *pkey, u32 extra_bytes, bool bypass)
{
	u32 bth0;
	struct hfi1_ibport *ibp;

	ibp = to_iport(qp->ibqp.device, qp->port_num);
	if (wqe->wr.opcode == IB_WR_SEND_WITH_IMM) {
		ohdr->u.ud.imm_data = wqe->wr.ex.imm_data;
		bth0 = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE << 24;
	} else {
		bth0 = IB_OPCODE_UD_SEND_ONLY << 24;
	}

	if (wqe->wr.send_flags & IB_SEND_SOLICITED)
		bth0 |= IB_BTH_SOLICITED;
	bth0 |= extra_bytes << 20;
	if (qp->ibqp.qp_type == IB_QPT_GSI || qp->ibqp.qp_type == IB_QPT_SMI)
		*pkey = hfi1_get_pkey(ibp, rvt_get_swqe_pkey_index(wqe));
	else
		*pkey = hfi1_get_pkey(ibp, qp->s_pkey_index);
	if (!bypass)
		bth0 |= *pkey;
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(rvt_get_swqe_remote_qpn(wqe));
	ohdr->bth[2] = cpu_to_be32(mask_psn(wqe->psn));
	/*
	 * Qkeys with the high order bit set mean use the
	 * qkey from the QP context instead of the WR (see 10.2.5).
	 */
	ohdr->u.ud.deth[0] =
		cpu_to_be32((int)rvt_get_swqe_remote_qkey(wqe) < 0 ? qp->qkey :
			    rvt_get_swqe_remote_qkey(wqe));
	ohdr->u.ud.deth[1] = cpu_to_be32(qp->ibqp.qp_num);
}

void hfi1_make_ud_req_9B(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			 struct rvt_swqe *wqe)
{
	u32 nwords, extra_bytes;
	u16 len, slid, dlid, pkey;
	u16 lrh0 = 0;
	u8 sc5;
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_other_headers *ohdr;
	struct rdma_ah_attr *ah_attr;
	struct hfi1_pportdata *ppd;
	struct hfi1_ibport *ibp;
	struct ib_grh *grh;

	ibp = to_iport(qp->ibqp.device, qp->port_num);
	ppd = ppd_from_ibp(ibp);
	ah_attr = rvt_get_swqe_ah_attr(wqe);

	extra_bytes = -wqe->length & 3;
	nwords = ((wqe->length + extra_bytes) >> 2) + SIZE_OF_CRC;
	/* header size in dwords LRH+BTH+DETH = (8+12+8)/4. */
	ps->s_txreq->hdr_dwords = 7;
	if (wqe->wr.opcode == IB_WR_SEND_WITH_IMM)
		ps->s_txreq->hdr_dwords++;

	if (rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH) {
		grh = &ps->s_txreq->phdr.hdr.ibh.u.l.grh;
		ps->s_txreq->hdr_dwords +=
			hfi1_make_grh(ibp, grh, rdma_ah_read_grh(ah_attr),
				      ps->s_txreq->hdr_dwords - LRH_9B_DWORDS,
				      nwords);
		lrh0 = HFI1_LRH_GRH;
		ohdr = &ps->s_txreq->phdr.hdr.ibh.u.l.oth;
	} else {
		lrh0 = HFI1_LRH_BTH;
		ohdr = &ps->s_txreq->phdr.hdr.ibh.u.oth;
	}

	sc5 = ibp->sl_to_sc[rdma_ah_get_sl(ah_attr)];
	lrh0 |= (rdma_ah_get_sl(ah_attr) & 0xf) << 4;
	if (qp->ibqp.qp_type == IB_QPT_SMI) {
		lrh0 |= 0xF000; /* Set VL (see ch. 13.5.3.1) */
		priv->s_sc = 0xf;
	} else {
		lrh0 |= (sc5 & 0xf) << 12;
		priv->s_sc = sc5;
	}

	dlid = opa_get_lid(rdma_ah_get_dlid(ah_attr), 9B);
	if (dlid == be16_to_cpu(IB_LID_PERMISSIVE)) {
		slid = be16_to_cpu(IB_LID_PERMISSIVE);
	} else {
		u16 lid = (u16)ppd->lid;

		if (lid) {
			lid |= rdma_ah_get_path_bits(ah_attr) &
				((1 << ppd->lmc) - 1);
			slid = lid;
		} else {
			slid = be16_to_cpu(IB_LID_PERMISSIVE);
		}
	}
	hfi1_make_bth_deth(qp, wqe, ohdr, &pkey, extra_bytes, false);
	len = ps->s_txreq->hdr_dwords + nwords;

	/* Setup the packet */
	ps->s_txreq->phdr.hdr.hdr_type = HFI1_PKT_TYPE_9B;
	hfi1_make_ib_hdr(&ps->s_txreq->phdr.hdr.ibh,
			 lrh0, len, dlid, slid);
}

void hfi1_make_ud_req_16B(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			  struct rvt_swqe *wqe)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_other_headers *ohdr;
	struct rdma_ah_attr *ah_attr;
	struct hfi1_pportdata *ppd;
	struct hfi1_ibport *ibp;
	u32 dlid, slid, nwords, extra_bytes;
	u32 dest_qp = rvt_get_swqe_remote_qpn(wqe);
	u32 src_qp = qp->ibqp.qp_num;
	u16 len, pkey;
	u8 l4, sc5;
	bool is_mgmt = false;

	ibp = to_iport(qp->ibqp.device, qp->port_num);
	ppd = ppd_from_ibp(ibp);
	ah_attr = rvt_get_swqe_ah_attr(wqe);

	/*
	 * Build 16B Management Packet if either the destination
	 * or source queue pair number is 0 or 1.
	 */
	if (dest_qp == 0 || src_qp == 0 || dest_qp == 1 || src_qp == 1) {
		/* header size in dwords 16B LRH+L4_FM = (16+8)/4. */
		ps->s_txreq->hdr_dwords = 6;
		is_mgmt = true;
	} else {
		/* header size in dwords 16B LRH+BTH+DETH = (16+12+8)/4. */
		ps->s_txreq->hdr_dwords = 9;
		if (wqe->wr.opcode == IB_WR_SEND_WITH_IMM)
			ps->s_txreq->hdr_dwords++;
	}

	/* SW provides space for CRC and LT for bypass packets. */
	extra_bytes = hfi1_get_16b_padding((ps->s_txreq->hdr_dwords << 2),
					   wqe->length);
	nwords = ((wqe->length + extra_bytes + SIZE_OF_LT) >> 2) + SIZE_OF_CRC;

	if ((rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH) &&
	    hfi1_check_mcast(rdma_ah_get_dlid(ah_attr))) {
		struct ib_grh *grh;
		struct ib_global_route *grd = rdma_ah_retrieve_grh(ah_attr);
		/*
		 * Ensure OPA GIDs are transformed to IB gids
		 * before creating the GRH.
		 */
		if (grd->sgid_index == OPA_GID_INDEX) {
			dd_dev_warn(ppd->dd, "Bad sgid_index. sgid_index: %d\n",
				    grd->sgid_index);
			grd->sgid_index = 0;
		}
		grh = &ps->s_txreq->phdr.hdr.opah.u.l.grh;
		ps->s_txreq->hdr_dwords += hfi1_make_grh(
			ibp, grh, grd,
			ps->s_txreq->hdr_dwords - LRH_16B_DWORDS,
			nwords);
		ohdr = &ps->s_txreq->phdr.hdr.opah.u.l.oth;
		l4 = OPA_16B_L4_IB_GLOBAL;
	} else {
		ohdr = &ps->s_txreq->phdr.hdr.opah.u.oth;
		l4 = OPA_16B_L4_IB_LOCAL;
	}

	sc5 = ibp->sl_to_sc[rdma_ah_get_sl(ah_attr)];
	if (qp->ibqp.qp_type == IB_QPT_SMI)
		priv->s_sc = 0xf;
	else
		priv->s_sc = sc5;

	dlid = opa_get_lid(rdma_ah_get_dlid(ah_attr), 16B);
	if (!ppd->lid)
		slid = be32_to_cpu(OPA_LID_PERMISSIVE);
	else
		slid = ppd->lid | (rdma_ah_get_path_bits(ah_attr) &
			   ((1 << ppd->lmc) - 1));

	if (is_mgmt) {
		l4 = OPA_16B_L4_FM;
		pkey = hfi1_get_pkey(ibp, rvt_get_swqe_pkey_index(wqe));
		hfi1_16B_set_qpn(&ps->s_txreq->phdr.hdr.opah.u.mgmt,
				 dest_qp, src_qp);
	} else {
		hfi1_make_bth_deth(qp, wqe, ohdr, &pkey, extra_bytes, true);
	}
	/* Convert dwords to flits */
	len = (ps->s_txreq->hdr_dwords + nwords) >> 1;

	/* Setup the packet */
	ps->s_txreq->phdr.hdr.hdr_type = HFI1_PKT_TYPE_16B;
	hfi1_make_16b_hdr(&ps->s_txreq->phdr.hdr.opah,
			  slid, dlid, len, pkey, 0, 0, l4, priv->s_sc);
}

/**
 * hfi1_make_ud_req - construct a UD request packet
 * @qp: the QP
 * @ps: the current packet state
 *
 * Assume s_lock is held.
 *
 * Return 1 if constructed; otherwise, return 0.
 */
int hfi1_make_ud_req(struct rvt_qp *qp, struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct rdma_ah_attr *ah_attr;
	struct hfi1_pportdata *ppd;
	struct hfi1_ibport *ibp;
	struct rvt_swqe *wqe;
	int next_cur;
	u32 lid;

	ps->s_txreq = get_txreq(ps->dev, qp);
	if (!ps->s_txreq)
		goto bail_no_tx;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_NEXT_SEND_OK)) {
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
		wqe = rvt_get_swqe_ptr(qp, qp->s_last);
		rvt_send_complete(qp, wqe, IB_WC_WR_FLUSH_ERR);
		goto done_free_tx;
	}

	/* see post_one_send() */
	if (qp->s_cur == READ_ONCE(qp->s_head))
		goto bail;

	wqe = rvt_get_swqe_ptr(qp, qp->s_cur);
	next_cur = qp->s_cur + 1;
	if (next_cur >= qp->s_size)
		next_cur = 0;

	/* Construct the header. */
	ibp = to_iport(qp->ibqp.device, qp->port_num);
	ppd = ppd_from_ibp(ibp);
	ah_attr = rvt_get_swqe_ah_attr(wqe);
	priv->hdr_type = hfi1_get_hdr_type(ppd->lid, ah_attr);
	if ((!hfi1_check_mcast(rdma_ah_get_dlid(ah_attr))) ||
	    (rdma_ah_get_dlid(ah_attr) == be32_to_cpu(OPA_LID_PERMISSIVE))) {
		lid = rdma_ah_get_dlid(ah_attr) & ~((1 << ppd->lmc) - 1);
		if (unlikely(!loopback &&
			     ((lid == ppd->lid) ||
			      ((lid == be32_to_cpu(OPA_LID_PERMISSIVE)) &&
			       (qp->ibqp.qp_type == IB_QPT_GSI))))) {
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
			rvt_send_complete(qp, wqe, IB_WC_SUCCESS);
			goto done_free_tx;
		}
	}

	qp->s_cur = next_cur;
	ps->s_txreq->s_cur_size = wqe->length;
	ps->s_txreq->ss = &qp->s_sge;
	qp->s_srate = rdma_ah_get_static_rate(ah_attr);
	qp->srate_mbps = ib_rate_to_mbps(qp->s_srate);
	qp->s_wqe = wqe;
	qp->s_sge.sge = wqe->sg_list[0];
	qp->s_sge.sg_list = wqe->sg_list + 1;
	qp->s_sge.num_sge = wqe->wr.num_sge;
	qp->s_sge.total_len = wqe->length;

	/* Make the appropriate header */
	hfi1_make_ud_req_tbl[priv->hdr_type](qp, ps, qp->s_wqe);
	priv->s_sde = qp_to_sdma_engine(qp, priv->s_sc);
	ps->s_txreq->sde = priv->s_sde;
	priv->s_sendcontext = qp_to_send_context(qp, priv->s_sc);
	ps->s_txreq->psc = priv->s_sendcontext;
	/* disarm any ahg */
	priv->s_ahg->ahgcount = 0;
	priv->s_ahg->ahgidx = 0;
	priv->s_ahg->tx_flags = 0;

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

void return_cnp_16B(struct hfi1_ibport *ibp, struct rvt_qp *qp,
		    u32 remote_qpn, u16 pkey, u32 slid, u32 dlid,
		    u8 sc5, const struct ib_grh *old_grh)
{
	u64 pbc, pbc_flags = 0;
	u32 bth0, plen, vl, hwords = 7;
	u16 len;
	u8 l4;
	struct hfi1_opa_header hdr;
	struct ib_other_headers *ohdr;
	struct pio_buf *pbuf;
	struct send_context *ctxt = qp_to_send_context(qp, sc5);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	u32 nwords;

	hdr.hdr_type = HFI1_PKT_TYPE_16B;
	/* Populate length */
	nwords = ((hfi1_get_16b_padding(hwords << 2, 0) +
		   SIZE_OF_LT) >> 2) + SIZE_OF_CRC;
	if (old_grh) {
		struct ib_grh *grh = &hdr.opah.u.l.grh;

		grh->version_tclass_flow = old_grh->version_tclass_flow;
		grh->paylen = cpu_to_be16(
			(hwords - LRH_16B_DWORDS + nwords) << 2);
		grh->hop_limit = 0xff;
		grh->sgid = old_grh->dgid;
		grh->dgid = old_grh->sgid;
		ohdr = &hdr.opah.u.l.oth;
		l4 = OPA_16B_L4_IB_GLOBAL;
		hwords += sizeof(struct ib_grh) / sizeof(u32);
	} else {
		ohdr = &hdr.opah.u.oth;
		l4 = OPA_16B_L4_IB_LOCAL;
	}

	/* BIT 16 to 19 is TVER. Bit 20 to 22 is pad cnt */
	bth0 = (IB_OPCODE_CNP << 24) | (1 << 16) |
	       (hfi1_get_16b_padding(hwords << 2, 0) << 20);
	ohdr->bth[0] = cpu_to_be32(bth0);

	ohdr->bth[1] = cpu_to_be32(remote_qpn);
	ohdr->bth[2] = 0; /* PSN 0 */

	/* Convert dwords to flits */
	len = (hwords + nwords) >> 1;
	hfi1_make_16b_hdr(&hdr.opah, slid, dlid, len, pkey, 1, 0, l4, sc5);

	plen = 2 /* PBC */ + hwords + nwords;
	pbc_flags |= PBC_PACKET_BYPASS | PBC_INSERT_BYPASS_ICRC;
	vl = sc_to_vlt(ppd->dd, sc5);
	pbc = create_pbc(ppd, pbc_flags, qp->srate_mbps, vl, plen);
	if (ctxt) {
		pbuf = sc_buffer_alloc(ctxt, plen, NULL, NULL);
		if (!IS_ERR_OR_NULL(pbuf)) {
			trace_pio_output_ibhdr(ppd->dd, &hdr, sc5);
			ppd->dd->pio_inline_send(ppd->dd, pbuf, pbc,
						 &hdr, hwords);
		}
	}
}

void return_cnp(struct hfi1_ibport *ibp, struct rvt_qp *qp, u32 remote_qpn,
		u16 pkey, u32 slid, u32 dlid, u8 sc5,
		const struct ib_grh *old_grh)
{
	u64 pbc, pbc_flags = 0;
	u32 bth0, plen, vl, hwords = 5;
	u16 lrh0;
	u8 sl = ibp->sc_to_sl[sc5];
	struct hfi1_opa_header hdr;
	struct ib_other_headers *ohdr;
	struct pio_buf *pbuf;
	struct send_context *ctxt = qp_to_send_context(qp, sc5);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

	hdr.hdr_type = HFI1_PKT_TYPE_9B;
	if (old_grh) {
		struct ib_grh *grh = &hdr.ibh.u.l.grh;

		grh->version_tclass_flow = old_grh->version_tclass_flow;
		grh->paylen = cpu_to_be16(
			(hwords - LRH_9B_DWORDS + SIZE_OF_CRC) << 2);
		grh->hop_limit = 0xff;
		grh->sgid = old_grh->dgid;
		grh->dgid = old_grh->sgid;
		ohdr = &hdr.ibh.u.l.oth;
		lrh0 = HFI1_LRH_GRH;
		hwords += sizeof(struct ib_grh) / sizeof(u32);
	} else {
		ohdr = &hdr.ibh.u.oth;
		lrh0 = HFI1_LRH_BTH;
	}

	lrh0 |= (sc5 & 0xf) << 12 | sl << 4;

	bth0 = pkey | (IB_OPCODE_CNP << 24);
	ohdr->bth[0] = cpu_to_be32(bth0);

	ohdr->bth[1] = cpu_to_be32(remote_qpn | (1 << IB_BECN_SHIFT));
	ohdr->bth[2] = 0; /* PSN 0 */

	hfi1_make_ib_hdr(&hdr.ibh, lrh0, hwords + SIZE_OF_CRC, dlid, slid);
	plen = 2 /* PBC */ + hwords;
	pbc_flags |= (ib_is_sc5(sc5) << PBC_DC_INFO_SHIFT);
	vl = sc_to_vlt(ppd->dd, sc5);
	pbc = create_pbc(ppd, pbc_flags, qp->srate_mbps, vl, plen);
	if (ctxt) {
		pbuf = sc_buffer_alloc(ctxt, plen, NULL, NULL);
		if (!IS_ERR_OR_NULL(pbuf)) {
			trace_pio_output_ibhdr(ppd->dd, &hdr, sc5);
			ppd->dd->pio_inline_send(ppd->dd, pbuf, pbc,
						 &hdr, hwords);
		}
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
 * @packet: the packet structure
 *
 * This is called from qp_rcv() to process an incoming UD packet
 * for the given QP.
 * Called at interrupt level.
 */
void hfi1_ud_rcv(struct hfi1_packet *packet)
{
	u32 hdrsize = packet->hlen;
	struct ib_wc wc;
	u32 src_qp;
	u16 pkey;
	int mgmt_pkey_idx = -1;
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	void *data = packet->payload;
	u32 tlen = packet->tlen;
	struct rvt_qp *qp = packet->qp;
	u8 sc5 = packet->sc;
	u8 sl_from_sc;
	u8 opcode = packet->opcode;
	u8 sl = packet->sl;
	u32 dlid = packet->dlid;
	u32 slid = packet->slid;
	u8 extra_bytes;
	u8 l4 = 0;
	bool dlid_is_permissive;
	bool slid_is_permissive;
	bool solicited = false;

	extra_bytes = packet->pad + packet->extra_byte + (SIZE_OF_CRC << 2);

	if (packet->etype == RHF_RCV_TYPE_BYPASS) {
		u32 permissive_lid =
			opa_get_lid(be32_to_cpu(OPA_LID_PERMISSIVE), 16B);

		l4 = hfi1_16B_get_l4(packet->hdr);
		pkey = hfi1_16B_get_pkey(packet->hdr);
		dlid_is_permissive = (dlid == permissive_lid);
		slid_is_permissive = (slid == permissive_lid);
	} else {
		pkey = ib_bth_get_pkey(packet->ohdr);
		dlid_is_permissive = (dlid == be16_to_cpu(IB_LID_PERMISSIVE));
		slid_is_permissive = (slid == be16_to_cpu(IB_LID_PERMISSIVE));
	}
	sl_from_sc = ibp->sc_to_sl[sc5];

	if (likely(l4 != OPA_16B_L4_FM)) {
		src_qp = ib_get_sqpn(packet->ohdr);
		solicited = ib_bth_is_solicited(packet->ohdr);
	} else {
		src_qp = hfi1_16B_get_src_qpn(packet->mgmt);
	}

	process_ecn(qp, packet);
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
		if (unlikely(dlid_is_permissive || slid_is_permissive))
			goto drop;
		if (qp->ibqp.qp_num > 1) {
			if (unlikely(rcv_pkey_check(ppd, pkey, sc5, slid))) {
				/*
				 * Traps will not be sent for packets dropped
				 * by the HW. This is fine, as sending trap
				 * for invalid pkeys is optional according to
				 * IB spec (release 1.3, section 10.9.4)
				 */
				hfi1_bad_pkey(ibp,
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
		if (unlikely(l4 != OPA_16B_L4_FM &&
			     ib_get_qkey(packet->ohdr) != qp->qkey))
			return; /* Silent drop */

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
		if ((dlid_is_permissive || slid_is_permissive) &&
		    smp->mgmt_class != IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
			goto drop;

		/* look up SMI pkey */
		mgmt_pkey_idx = hfi1_lookup_pkey_idx(ibp, pkey);
		if (mgmt_pkey_idx < 0)
			goto drop;
	}

	if (qp->ibqp.qp_num > 1 &&
	    opcode == IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE) {
		wc.ex.imm_data = packet->ohdr->u.ud.imm_data;
		wc.wc_flags = IB_WC_WITH_IMM;
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

		ret = rvt_get_rwqe(qp, false);
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
	if (packet->grh) {
		rvt_copy_sge(qp, &qp->r_sge, packet->grh,
			     sizeof(struct ib_grh), true, false);
		wc.wc_flags |= IB_WC_GRH;
	} else if (packet->etype == RHF_RCV_TYPE_BYPASS) {
		struct ib_grh grh;
		/*
		 * Assuming we only created 16B on the send side
		 * if we want to use large LIDs, since GRH was stripped
		 * out when creating 16B, add back the GRH here.
		 */
		hfi1_make_ext_grh(packet, &grh, slid, dlid);
		rvt_copy_sge(qp, &qp->r_sge, &grh,
			     sizeof(struct ib_grh), true, false);
		wc.wc_flags |= IB_WC_GRH;
	} else {
		rvt_skip_sge(&qp->r_sge, sizeof(struct ib_grh), true);
	}
	rvt_copy_sge(qp, &qp->r_sge, data, wc.byte_len - sizeof(struct ib_grh),
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
	if (slid_is_permissive)
		slid = be32_to_cpu(OPA_LID_PERMISSIVE);
	wc.slid = slid & U16_MAX;
	wc.sl = sl_from_sc;

	/*
	 * Save the LMC lower bits if the destination LID is a unicast LID.
	 */
	wc.dlid_path_bits = hfi1_check_mcast(dlid) ? 0 :
		dlid & ((1 << ppd_from_ibp(ibp)->lmc) - 1);
	wc.port_num = qp->port_num;
	/* Signal completion event if the solicited bit is set. */
	rvt_recv_cq(qp, &wc, solicited);
	return;

drop:
	ibp->rvp.n_pkt_drops++;
}
