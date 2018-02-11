/*
 * Copyright(c) 2015 - 2017 Intel Corporation.
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

#include <linux/spinlock.h>

#include "hfi.h"
#include "mad.h"
#include "qp.h"
#include "verbs_txreq.h"
#include "trace.h"

/*
 * Validate a RWQE and fill in the SGE state.
 * Return 1 if OK.
 */
static int init_sge(struct rvt_qp *qp, struct rvt_rwqe *wqe)
{
	int i, j, ret;
	struct ib_wc wc;
	struct rvt_lkey_table *rkt;
	struct rvt_pd *pd;
	struct rvt_sge_state *ss;

	rkt = &to_idev(qp->ibqp.device)->rdi.lkey_table;
	pd = ibpd_to_rvtpd(qp->ibqp.srq ? qp->ibqp.srq->pd : qp->ibqp.pd);
	ss = &qp->r_sge;
	ss->sg_list = qp->r_sg_list;
	qp->r_len = 0;
	for (i = j = 0; i < wqe->num_sge; i++) {
		if (wqe->sg_list[i].length == 0)
			continue;
		/* Check LKEY */
		ret = rvt_lkey_ok(rkt, pd, j ? &ss->sg_list[j - 1] : &ss->sge,
				  NULL, &wqe->sg_list[i],
				  IB_ACCESS_LOCAL_WRITE);
		if (unlikely(ret <= 0))
			goto bad_lkey;
		qp->r_len += wqe->sg_list[i].length;
		j++;
	}
	ss->num_sge = j;
	ss->total_len = qp->r_len;
	ret = 1;
	goto bail;

bad_lkey:
	while (j) {
		struct rvt_sge *sge = --j ? &ss->sg_list[j - 1] : &ss->sge;

		rvt_put_mr(sge->mr);
	}
	ss->num_sge = 0;
	memset(&wc, 0, sizeof(wc));
	wc.wr_id = wqe->wr_id;
	wc.status = IB_WC_LOC_PROT_ERR;
	wc.opcode = IB_WC_RECV;
	wc.qp = &qp->ibqp;
	/* Signal solicited completion event. */
	rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc, 1);
	ret = 0;
bail:
	return ret;
}

/**
 * hfi1_rvt_get_rwqe - copy the next RWQE into the QP's RWQE
 * @qp: the QP
 * @wr_id_only: update qp->r_wr_id only, not qp->r_sge
 *
 * Return -1 if there is a local error, 0 if no RWQE is available,
 * otherwise return 1.
 *
 * Can be called from interrupt level.
 */
int hfi1_rvt_get_rwqe(struct rvt_qp *qp, int wr_id_only)
{
	unsigned long flags;
	struct rvt_rq *rq;
	struct rvt_rwq *wq;
	struct rvt_srq *srq;
	struct rvt_rwqe *wqe;
	void (*handler)(struct ib_event *, void *);
	u32 tail;
	int ret;

	if (qp->ibqp.srq) {
		srq = ibsrq_to_rvtsrq(qp->ibqp.srq);
		handler = srq->ibsrq.event_handler;
		rq = &srq->rq;
	} else {
		srq = NULL;
		handler = NULL;
		rq = &qp->r_rq;
	}

	spin_lock_irqsave(&rq->lock, flags);
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK)) {
		ret = 0;
		goto unlock;
	}

	wq = rq->wq;
	tail = wq->tail;
	/* Validate tail before using it since it is user writable. */
	if (tail >= rq->size)
		tail = 0;
	if (unlikely(tail == wq->head)) {
		ret = 0;
		goto unlock;
	}
	/* Make sure entry is read after head index is read. */
	smp_rmb();
	wqe = rvt_get_rwqe_ptr(rq, tail);
	/*
	 * Even though we update the tail index in memory, the verbs
	 * consumer is not supposed to post more entries until a
	 * completion is generated.
	 */
	if (++tail >= rq->size)
		tail = 0;
	wq->tail = tail;
	if (!wr_id_only && !init_sge(qp, wqe)) {
		ret = -1;
		goto unlock;
	}
	qp->r_wr_id = wqe->wr_id;

	ret = 1;
	set_bit(RVT_R_WRID_VALID, &qp->r_aflags);
	if (handler) {
		u32 n;

		/*
		 * Validate head pointer value and compute
		 * the number of remaining WQEs.
		 */
		n = wq->head;
		if (n >= rq->size)
			n = 0;
		if (n < tail)
			n += rq->size - tail;
		else
			n -= tail;
		if (n < srq->limit) {
			struct ib_event ev;

			srq->limit = 0;
			spin_unlock_irqrestore(&rq->lock, flags);
			ev.device = qp->ibqp.device;
			ev.element.srq = qp->ibqp.srq;
			ev.event = IB_EVENT_SRQ_LIMIT_REACHED;
			handler(&ev, srq->ibsrq.srq_context);
			goto bail;
		}
	}
unlock:
	spin_unlock_irqrestore(&rq->lock, flags);
bail:
	return ret;
}

static int gid_ok(union ib_gid *gid, __be64 gid_prefix, __be64 id)
{
	return (gid->global.interface_id == id &&
		(gid->global.subnet_prefix == gid_prefix ||
		 gid->global.subnet_prefix == IB_DEFAULT_GID_PREFIX));
}

/*
 *
 * This should be called with the QP r_lock held.
 *
 * The s_lock will be acquired around the hfi1_migrate_qp() call.
 */
int hfi1_ruc_check_hdr(struct hfi1_ibport *ibp, struct hfi1_packet *packet)
{
	__be64 guid;
	unsigned long flags;
	struct rvt_qp *qp = packet->qp;
	u8 sc5 = ibp->sl_to_sc[rdma_ah_get_sl(&qp->remote_ah_attr)];
	u32 dlid = packet->dlid;
	u32 slid = packet->slid;
	u32 sl = packet->sl;
	int migrated;
	u32 bth0, bth1;
	u16 pkey;

	bth0 = be32_to_cpu(packet->ohdr->bth[0]);
	bth1 = be32_to_cpu(packet->ohdr->bth[1]);
	if (packet->etype == RHF_RCV_TYPE_BYPASS) {
		pkey = hfi1_16B_get_pkey(packet->hdr);
		migrated = bth1 & OPA_BTH_MIG_REQ;
	} else {
		pkey = ib_bth_get_pkey(packet->ohdr);
		migrated = bth0 & IB_BTH_MIG_REQ;
	}

	if (qp->s_mig_state == IB_MIG_ARMED && migrated) {
		if (!packet->grh) {
			if ((rdma_ah_get_ah_flags(&qp->alt_ah_attr) &
			     IB_AH_GRH) &&
			    (packet->etype != RHF_RCV_TYPE_BYPASS))
				return 1;
		} else {
			const struct ib_global_route *grh;

			if (!(rdma_ah_get_ah_flags(&qp->alt_ah_attr) &
			      IB_AH_GRH))
				return 1;
			grh = rdma_ah_read_grh(&qp->alt_ah_attr);
			guid = get_sguid(ibp, grh->sgid_index);
			if (!gid_ok(&packet->grh->dgid, ibp->rvp.gid_prefix,
				    guid))
				return 1;
			if (!gid_ok(
				&packet->grh->sgid,
				grh->dgid.global.subnet_prefix,
				grh->dgid.global.interface_id))
				return 1;
		}
		if (unlikely(rcv_pkey_check(ppd_from_ibp(ibp), pkey,
					    sc5, slid))) {
			hfi1_bad_pkey(ibp, pkey, sl, 0, qp->ibqp.qp_num,
				      slid, dlid);
			return 1;
		}
		/* Validate the SLID. See Ch. 9.6.1.5 and 17.2.8 */
		if (slid != rdma_ah_get_dlid(&qp->alt_ah_attr) ||
		    ppd_from_ibp(ibp)->port !=
			rdma_ah_get_port_num(&qp->alt_ah_attr))
			return 1;
		spin_lock_irqsave(&qp->s_lock, flags);
		hfi1_migrate_qp(qp);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	} else {
		if (!packet->grh) {
			if ((rdma_ah_get_ah_flags(&qp->remote_ah_attr) &
			     IB_AH_GRH) &&
			    (packet->etype != RHF_RCV_TYPE_BYPASS))
				return 1;
		} else {
			const struct ib_global_route *grh;

			if (!(rdma_ah_get_ah_flags(&qp->remote_ah_attr) &
						   IB_AH_GRH))
				return 1;
			grh = rdma_ah_read_grh(&qp->remote_ah_attr);
			guid = get_sguid(ibp, grh->sgid_index);
			if (!gid_ok(&packet->grh->dgid, ibp->rvp.gid_prefix,
				    guid))
				return 1;
			if (!gid_ok(
			     &packet->grh->sgid,
			     grh->dgid.global.subnet_prefix,
			     grh->dgid.global.interface_id))
				return 1;
		}
		if (unlikely(rcv_pkey_check(ppd_from_ibp(ibp), pkey,
					    sc5, slid))) {
			hfi1_bad_pkey(ibp, pkey, sl, 0, qp->ibqp.qp_num,
				      slid, dlid);
			return 1;
		}
		/* Validate the SLID. See Ch. 9.6.1.5 */
		if ((slid != rdma_ah_get_dlid(&qp->remote_ah_attr)) ||
		    ppd_from_ibp(ibp)->port != qp->port_num)
			return 1;
		if (qp->s_mig_state == IB_MIG_REARM && !migrated)
			qp->s_mig_state = IB_MIG_ARMED;
	}

	return 0;
}

/**
 * ruc_loopback - handle UC and RC loopback requests
 * @sqp: the sending QP
 *
 * This is called from hfi1_do_send() to
 * forward a WQE addressed to the same HFI.
 * Note that although we are single threaded due to the send engine, we still
 * have to protect against post_send().  We don't have to worry about
 * receive interrupts since this is a connected protocol and all packets
 * will pass through here.
 */
static void ruc_loopback(struct rvt_qp *sqp)
{
	struct hfi1_ibport *ibp = to_iport(sqp->ibqp.device, sqp->port_num);
	struct rvt_qp *qp;
	struct rvt_swqe *wqe;
	struct rvt_sge *sge;
	unsigned long flags;
	struct ib_wc wc;
	u64 sdata;
	atomic64_t *maddr;
	enum ib_wc_status send_status;
	bool release;
	int ret;
	bool copy_last = false;
	int local_ops = 0;

	rcu_read_lock();

	/*
	 * Note that we check the responder QP state after
	 * checking the requester's state.
	 */
	qp = rvt_lookup_qpn(ib_to_rvt(sqp->ibqp.device), &ibp->rvp,
			    sqp->remote_qpn);

	spin_lock_irqsave(&sqp->s_lock, flags);

	/* Return if we are already busy processing a work request. */
	if ((sqp->s_flags & (RVT_S_BUSY | RVT_S_ANY_WAIT)) ||
	    !(ib_rvt_state_ops[sqp->state] & RVT_PROCESS_OR_FLUSH_SEND))
		goto unlock;

	sqp->s_flags |= RVT_S_BUSY;

again:
	smp_read_barrier_depends(); /* see post_one_send() */
	if (sqp->s_last == READ_ONCE(sqp->s_head))
		goto clr_busy;
	wqe = rvt_get_swqe_ptr(sqp, sqp->s_last);

	/* Return if it is not OK to start a new work request. */
	if (!(ib_rvt_state_ops[sqp->state] & RVT_PROCESS_NEXT_SEND_OK)) {
		if (!(ib_rvt_state_ops[sqp->state] & RVT_FLUSH_SEND))
			goto clr_busy;
		/* We are in the error state, flush the work request. */
		send_status = IB_WC_WR_FLUSH_ERR;
		goto flush_send;
	}

	/*
	 * We can rely on the entry not changing without the s_lock
	 * being held until we update s_last.
	 * We increment s_cur to indicate s_last is in progress.
	 */
	if (sqp->s_last == sqp->s_cur) {
		if (++sqp->s_cur >= sqp->s_size)
			sqp->s_cur = 0;
	}
	spin_unlock_irqrestore(&sqp->s_lock, flags);

	if (!qp || !(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) ||
	    qp->ibqp.qp_type != sqp->ibqp.qp_type) {
		ibp->rvp.n_pkt_drops++;
		/*
		 * For RC, the requester would timeout and retry so
		 * shortcut the timeouts and just signal too many retries.
		 */
		if (sqp->ibqp.qp_type == IB_QPT_RC)
			send_status = IB_WC_RETRY_EXC_ERR;
		else
			send_status = IB_WC_SUCCESS;
		goto serr;
	}

	memset(&wc, 0, sizeof(wc));
	send_status = IB_WC_SUCCESS;

	release = true;
	sqp->s_sge.sge = wqe->sg_list[0];
	sqp->s_sge.sg_list = wqe->sg_list + 1;
	sqp->s_sge.num_sge = wqe->wr.num_sge;
	sqp->s_len = wqe->length;
	switch (wqe->wr.opcode) {
	case IB_WR_REG_MR:
		goto send_comp;

	case IB_WR_LOCAL_INV:
		if (!(wqe->wr.send_flags & RVT_SEND_COMPLETION_ONLY)) {
			if (rvt_invalidate_rkey(sqp,
						wqe->wr.ex.invalidate_rkey))
				send_status = IB_WC_LOC_PROT_ERR;
			local_ops = 1;
		}
		goto send_comp;

	case IB_WR_SEND_WITH_INV:
		if (!rvt_invalidate_rkey(qp, wqe->wr.ex.invalidate_rkey)) {
			wc.wc_flags = IB_WC_WITH_INVALIDATE;
			wc.ex.invalidate_rkey = wqe->wr.ex.invalidate_rkey;
		}
		goto send;

	case IB_WR_SEND_WITH_IMM:
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.ex.imm_data = wqe->wr.ex.imm_data;
		/* FALLTHROUGH */
	case IB_WR_SEND:
send:
		ret = hfi1_rvt_get_rwqe(qp, 0);
		if (ret < 0)
			goto op_err;
		if (!ret)
			goto rnr_nak;
		break;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
			goto inv_err;
		wc.wc_flags = IB_WC_WITH_IMM;
		wc.ex.imm_data = wqe->wr.ex.imm_data;
		ret = hfi1_rvt_get_rwqe(qp, 1);
		if (ret < 0)
			goto op_err;
		if (!ret)
			goto rnr_nak;
		/* skip copy_last set and qp_access_flags recheck */
		goto do_write;
	case IB_WR_RDMA_WRITE:
		copy_last = rvt_is_user_qp(qp);
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
			goto inv_err;
do_write:
		if (wqe->length == 0)
			break;
		if (unlikely(!rvt_rkey_ok(qp, &qp->r_sge.sge, wqe->length,
					  wqe->rdma_wr.remote_addr,
					  wqe->rdma_wr.rkey,
					  IB_ACCESS_REMOTE_WRITE)))
			goto acc_err;
		qp->r_sge.sg_list = NULL;
		qp->r_sge.num_sge = 1;
		qp->r_sge.total_len = wqe->length;
		break;

	case IB_WR_RDMA_READ:
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_READ)))
			goto inv_err;
		if (unlikely(!rvt_rkey_ok(qp, &sqp->s_sge.sge, wqe->length,
					  wqe->rdma_wr.remote_addr,
					  wqe->rdma_wr.rkey,
					  IB_ACCESS_REMOTE_READ)))
			goto acc_err;
		release = false;
		sqp->s_sge.sg_list = NULL;
		sqp->s_sge.num_sge = 1;
		qp->r_sge.sge = wqe->sg_list[0];
		qp->r_sge.sg_list = wqe->sg_list + 1;
		qp->r_sge.num_sge = wqe->wr.num_sge;
		qp->r_sge.total_len = wqe->length;
		break;

	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC)))
			goto inv_err;
		if (unlikely(!rvt_rkey_ok(qp, &qp->r_sge.sge, sizeof(u64),
					  wqe->atomic_wr.remote_addr,
					  wqe->atomic_wr.rkey,
					  IB_ACCESS_REMOTE_ATOMIC)))
			goto acc_err;
		/* Perform atomic OP and save result. */
		maddr = (atomic64_t *)qp->r_sge.sge.vaddr;
		sdata = wqe->atomic_wr.compare_add;
		*(u64 *)sqp->s_sge.sge.vaddr =
			(wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) ?
			(u64)atomic64_add_return(sdata, maddr) - sdata :
			(u64)cmpxchg((u64 *)qp->r_sge.sge.vaddr,
				      sdata, wqe->atomic_wr.swap);
		rvt_put_mr(qp->r_sge.sge.mr);
		qp->r_sge.num_sge = 0;
		goto send_comp;

	default:
		send_status = IB_WC_LOC_QP_OP_ERR;
		goto serr;
	}

	sge = &sqp->s_sge.sge;
	while (sqp->s_len) {
		u32 len = sqp->s_len;

		if (len > sge->length)
			len = sge->length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		WARN_ON_ONCE(len == 0);
		hfi1_copy_sge(&qp->r_sge, sge->vaddr, len, release, copy_last);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (!release)
				rvt_put_mr(sge->mr);
			if (--sqp->s_sge.num_sge)
				*sge = *sqp->s_sge.sg_list++;
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
		sqp->s_len -= len;
	}
	if (release)
		rvt_put_ss(&qp->r_sge);

	if (!test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags))
		goto send_comp;

	if (wqe->wr.opcode == IB_WR_RDMA_WRITE_WITH_IMM)
		wc.opcode = IB_WC_RECV_RDMA_WITH_IMM;
	else
		wc.opcode = IB_WC_RECV;
	wc.wr_id = qp->r_wr_id;
	wc.status = IB_WC_SUCCESS;
	wc.byte_len = wqe->length;
	wc.qp = &qp->ibqp;
	wc.src_qp = qp->remote_qpn;
	wc.slid = rdma_ah_get_dlid(&qp->remote_ah_attr) & U16_MAX;
	wc.sl = rdma_ah_get_sl(&qp->remote_ah_attr);
	wc.port_num = 1;
	/* Signal completion event if the solicited bit is set. */
	rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc,
		     wqe->wr.send_flags & IB_SEND_SOLICITED);

send_comp:
	spin_lock_irqsave(&sqp->s_lock, flags);
	ibp->rvp.n_loop_pkts++;
flush_send:
	sqp->s_rnr_retry = sqp->s_rnr_retry_cnt;
	hfi1_send_complete(sqp, wqe, send_status);
	if (local_ops) {
		atomic_dec(&sqp->local_ops_pending);
		local_ops = 0;
	}
	goto again;

rnr_nak:
	/* Handle RNR NAK */
	if (qp->ibqp.qp_type == IB_QPT_UC)
		goto send_comp;
	ibp->rvp.n_rnr_naks++;
	/*
	 * Note: we don't need the s_lock held since the BUSY flag
	 * makes this single threaded.
	 */
	if (sqp->s_rnr_retry == 0) {
		send_status = IB_WC_RNR_RETRY_EXC_ERR;
		goto serr;
	}
	if (sqp->s_rnr_retry_cnt < 7)
		sqp->s_rnr_retry--;
	spin_lock_irqsave(&sqp->s_lock, flags);
	if (!(ib_rvt_state_ops[sqp->state] & RVT_PROCESS_RECV_OK))
		goto clr_busy;
	rvt_add_rnr_timer(sqp, qp->r_min_rnr_timer <<
				IB_AETH_CREDIT_SHIFT);
	goto clr_busy;

op_err:
	send_status = IB_WC_REM_OP_ERR;
	wc.status = IB_WC_LOC_QP_OP_ERR;
	goto err;

inv_err:
	send_status = IB_WC_REM_INV_REQ_ERR;
	wc.status = IB_WC_LOC_QP_OP_ERR;
	goto err;

acc_err:
	send_status = IB_WC_REM_ACCESS_ERR;
	wc.status = IB_WC_LOC_PROT_ERR;
err:
	/* responder goes to error state */
	rvt_rc_error(qp, wc.status);

serr:
	spin_lock_irqsave(&sqp->s_lock, flags);
	hfi1_send_complete(sqp, wqe, send_status);
	if (sqp->ibqp.qp_type == IB_QPT_RC) {
		int lastwqe = rvt_error_qp(sqp, IB_WC_WR_FLUSH_ERR);

		sqp->s_flags &= ~RVT_S_BUSY;
		spin_unlock_irqrestore(&sqp->s_lock, flags);
		if (lastwqe) {
			struct ib_event ev;

			ev.device = sqp->ibqp.device;
			ev.element.qp = &sqp->ibqp;
			ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
			sqp->ibqp.event_handler(&ev, sqp->ibqp.qp_context);
		}
		goto done;
	}
clr_busy:
	sqp->s_flags &= ~RVT_S_BUSY;
unlock:
	spin_unlock_irqrestore(&sqp->s_lock, flags);
done:
	rcu_read_unlock();
}

/**
 * hfi1_make_grh - construct a GRH header
 * @ibp: a pointer to the IB port
 * @hdr: a pointer to the GRH header being constructed
 * @grh: the global route address to send to
 * @hwords: size of header after grh being sent in dwords
 * @nwords: the number of 32 bit words of data being sent
 *
 * Return the size of the header in 32 bit words.
 */
u32 hfi1_make_grh(struct hfi1_ibport *ibp, struct ib_grh *hdr,
		  const struct ib_global_route *grh, u32 hwords, u32 nwords)
{
	hdr->version_tclass_flow =
		cpu_to_be32((IB_GRH_VERSION << IB_GRH_VERSION_SHIFT) |
			    (grh->traffic_class << IB_GRH_TCLASS_SHIFT) |
			    (grh->flow_label << IB_GRH_FLOW_SHIFT));
	hdr->paylen = cpu_to_be16((hwords + nwords) << 2);
	/* next_hdr is defined by C8-7 in ch. 8.4.1 */
	hdr->next_hdr = IB_GRH_NEXT_HDR;
	hdr->hop_limit = grh->hop_limit;
	/* The SGID is 32-bit aligned. */
	hdr->sgid.global.subnet_prefix = ibp->rvp.gid_prefix;
	hdr->sgid.global.interface_id =
		grh->sgid_index < HFI1_GUIDS_PER_PORT ?
		get_sguid(ibp, grh->sgid_index) :
		get_sguid(ibp, HFI1_PORT_GUID_INDEX);
	hdr->dgid = grh->dgid;

	/* GRH header size in 32-bit words. */
	return sizeof(struct ib_grh) / sizeof(u32);
}

#define BTH2_OFFSET (offsetof(struct hfi1_sdma_header, \
			      hdr.ibh.u.oth.bth[2]) / 4)

/**
 * build_ahg - create ahg in s_ahg
 * @qp: a pointer to QP
 * @npsn: the next PSN for the request/response
 *
 * This routine handles the AHG by allocating an ahg entry and causing the
 * copy of the first middle.
 *
 * Subsequent middles use the copied entry, editing the
 * PSN with 1 or 2 edits.
 */
static inline void build_ahg(struct rvt_qp *qp, u32 npsn)
{
	struct hfi1_qp_priv *priv = qp->priv;

	if (unlikely(qp->s_flags & RVT_S_AHG_CLEAR))
		clear_ahg(qp);
	if (!(qp->s_flags & RVT_S_AHG_VALID)) {
		/* first middle that needs copy  */
		if (qp->s_ahgidx < 0)
			qp->s_ahgidx = sdma_ahg_alloc(priv->s_sde);
		if (qp->s_ahgidx >= 0) {
			qp->s_ahgpsn = npsn;
			priv->s_ahg->tx_flags |= SDMA_TXREQ_F_AHG_COPY;
			/* save to protect a change in another thread */
			priv->s_ahg->ahgidx = qp->s_ahgidx;
			qp->s_flags |= RVT_S_AHG_VALID;
		}
	} else {
		/* subsequent middle after valid */
		if (qp->s_ahgidx >= 0) {
			priv->s_ahg->tx_flags |= SDMA_TXREQ_F_USE_AHG;
			priv->s_ahg->ahgidx = qp->s_ahgidx;
			priv->s_ahg->ahgcount++;
			priv->s_ahg->ahgdesc[0] =
				sdma_build_ahg_descriptor(
					(__force u16)cpu_to_be16((u16)npsn),
					BTH2_OFFSET,
					16,
					16);
			if ((npsn & 0xffff0000) !=
					(qp->s_ahgpsn & 0xffff0000)) {
				priv->s_ahg->ahgcount++;
				priv->s_ahg->ahgdesc[1] =
					sdma_build_ahg_descriptor(
						(__force u16)cpu_to_be16(
							(u16)(npsn >> 16)),
						BTH2_OFFSET,
						0,
						16);
			}
		}
	}
}

static inline void hfi1_make_ruc_bth(struct rvt_qp *qp,
				     struct ib_other_headers *ohdr,
				     u32 bth0, u32 bth1, u32 bth2)
{
	bth1 |= qp->remote_qpn;
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(bth1);
	ohdr->bth[2] = cpu_to_be32(bth2);
}

static inline void hfi1_make_ruc_header_16B(struct rvt_qp *qp,
					    struct ib_other_headers *ohdr,
					    u32 bth0, u32 bth2, int middle,
					    struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ibport *ibp = ps->ibp;
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	u32 bth1 = 0;
	u32 slid;
	u16 pkey = hfi1_get_pkey(ibp, qp->s_pkey_index);
	u8 l4 = OPA_16B_L4_IB_LOCAL;
	u8 extra_bytes = hfi1_get_16b_padding((qp->s_hdrwords << 2),
				   ps->s_txreq->s_cur_size);
	u32 nwords = SIZE_OF_CRC + ((ps->s_txreq->s_cur_size +
				 extra_bytes + SIZE_OF_LT) >> 2);
	u8 becn = 0;

	if (unlikely(rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH) &&
	    hfi1_check_mcast(rdma_ah_get_dlid(&qp->remote_ah_attr))) {
		struct ib_grh *grh;
		struct ib_global_route *grd =
			rdma_ah_retrieve_grh(&qp->remote_ah_attr);
		int hdrwords;

		/*
		 * Ensure OPA GIDs are transformed to IB gids
		 * before creating the GRH.
		 */
		if (grd->sgid_index == OPA_GID_INDEX)
			grd->sgid_index = 0;
		grh = &ps->s_txreq->phdr.hdr.opah.u.l.grh;
		l4 = OPA_16B_L4_IB_GLOBAL;
		hdrwords = qp->s_hdrwords - 4;
		qp->s_hdrwords += hfi1_make_grh(ibp, grh, grd,
						hdrwords, nwords);
		middle = 0;
	}

	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth1 |= OPA_BTH_MIG_REQ;
	else
		middle = 0;

	if (middle)
		build_ahg(qp, bth2);
	else
		qp->s_flags &= ~RVT_S_AHG_VALID;

	bth0 |= pkey;
	bth0 |= extra_bytes << 20;
	if (qp->s_flags & RVT_S_ECN) {
		qp->s_flags &= ~RVT_S_ECN;
		/* we recently received a FECN, so return a BECN */
		becn = 1;
	}
	hfi1_make_ruc_bth(qp, ohdr, bth0, bth1, bth2);

	if (!ppd->lid)
		slid = be32_to_cpu(OPA_LID_PERMISSIVE);
	else
		slid = ppd->lid |
			(rdma_ah_get_path_bits(&qp->remote_ah_attr) &
			((1 << ppd->lmc) - 1));

	hfi1_make_16b_hdr(&ps->s_txreq->phdr.hdr.opah,
			  slid,
			  opa_get_lid(rdma_ah_get_dlid(&qp->remote_ah_attr),
				      16B),
			  (qp->s_hdrwords + nwords) >> 1,
			  pkey, becn, 0, l4, priv->s_sc);
}

static inline void hfi1_make_ruc_header_9B(struct rvt_qp *qp,
					   struct ib_other_headers *ohdr,
					   u32 bth0, u32 bth2, int middle,
					   struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ibport *ibp = ps->ibp;
	u32 bth1 = 0;
	u16 pkey = hfi1_get_pkey(ibp, qp->s_pkey_index);
	u16 lrh0 = HFI1_LRH_BTH;
	u8 extra_bytes = -ps->s_txreq->s_cur_size & 3;
	u32 nwords = SIZE_OF_CRC + ((ps->s_txreq->s_cur_size +
					 extra_bytes) >> 2);

	if (unlikely(rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH)) {
		struct ib_grh *grh = &ps->s_txreq->phdr.hdr.ibh.u.l.grh;
		int hdrwords = qp->s_hdrwords - 2;

		lrh0 = HFI1_LRH_GRH;
		qp->s_hdrwords +=
			hfi1_make_grh(ibp, grh,
				      rdma_ah_read_grh(&qp->remote_ah_attr),
				      hdrwords, nwords);
		middle = 0;
	}
	lrh0 |= (priv->s_sc & 0xf) << 12 |
		(rdma_ah_get_sl(&qp->remote_ah_attr) & 0xf) << 4;

	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth0 |= IB_BTH_MIG_REQ;
	else
		middle = 0;

	if (middle)
		build_ahg(qp, bth2);
	else
		qp->s_flags &= ~RVT_S_AHG_VALID;

	bth0 |= pkey;
	bth0 |= extra_bytes << 20;
	if (qp->s_flags & RVT_S_ECN) {
		qp->s_flags &= ~RVT_S_ECN;
		/* we recently received a FECN, so return a BECN */
		bth1 |= (IB_BECN_MASK << IB_BECN_SHIFT);
	}
	hfi1_make_ruc_bth(qp, ohdr, bth0, bth1, bth2);
	hfi1_make_ib_hdr(&ps->s_txreq->phdr.hdr.ibh,
			 lrh0,
			 qp->s_hdrwords + nwords,
			 opa_get_lid(rdma_ah_get_dlid(&qp->remote_ah_attr), 9B),
			 ppd_from_ibp(ibp)->lid |
				rdma_ah_get_path_bits(&qp->remote_ah_attr));
}

typedef void (*hfi1_make_ruc_hdr)(struct rvt_qp *qp,
				  struct ib_other_headers *ohdr,
				  u32 bth0, u32 bth2, int middle,
				  struct hfi1_pkt_state *ps);

/* We support only two types - 9B and 16B for now */
static const hfi1_make_ruc_hdr hfi1_ruc_header_tbl[2] = {
	[HFI1_PKT_TYPE_9B] = &hfi1_make_ruc_header_9B,
	[HFI1_PKT_TYPE_16B] = &hfi1_make_ruc_header_16B
};

void hfi1_make_ruc_header(struct rvt_qp *qp, struct ib_other_headers *ohdr,
			  u32 bth0, u32 bth2, int middle,
			  struct hfi1_pkt_state *ps)
{
	struct hfi1_qp_priv *priv = qp->priv;

	/*
	 * reset s_ahg/AHG fields
	 *
	 * This insures that the ahgentry/ahgcount
	 * are at a non-AHG default to protect
	 * build_verbs_tx_desc() from using
	 * an include ahgidx.
	 *
	 * build_ahg() will modify as appropriate
	 * to use the AHG feature.
	 */
	priv->s_ahg->tx_flags = 0;
	priv->s_ahg->ahgcount = 0;
	priv->s_ahg->ahgidx = 0;

	/* Make the appropriate header */
	hfi1_ruc_header_tbl[priv->hdr_type](qp, ohdr, bth0, bth2, middle, ps);
}

/* when sending, force a reschedule every one of these periods */
#define SEND_RESCHED_TIMEOUT (5 * HZ)  /* 5s in jiffies */

/**
 * schedule_send_yield - test for a yield required for QP send engine
 * @timeout: Final time for timeout slice for jiffies
 * @qp: a pointer to QP
 * @ps: a pointer to a structure with commonly lookup values for
 *      the the send engine progress
 *
 * This routine checks if the time slice for the QP has expired
 * for RC QPs, if so an additional work entry is queued. At this
 * point, other QPs have an opportunity to be scheduled. It
 * returns true if a yield is required, otherwise, false
 * is returned.
 */
static bool schedule_send_yield(struct rvt_qp *qp,
				struct hfi1_pkt_state *ps)
{
	ps->pkts_sent = true;

	if (unlikely(time_after(jiffies, ps->timeout))) {
		if (!ps->in_thread ||
		    workqueue_congested(ps->cpu, ps->ppd->hfi1_wq)) {
			spin_lock_irqsave(&qp->s_lock, ps->flags);
			qp->s_flags &= ~RVT_S_BUSY;
			hfi1_schedule_send(qp);
			spin_unlock_irqrestore(&qp->s_lock, ps->flags);
			this_cpu_inc(*ps->ppd->dd->send_schedule);
			trace_hfi1_rc_expired_time_slice(qp, true);
			return true;
		}

		cond_resched();
		this_cpu_inc(*ps->ppd->dd->send_schedule);
		ps->timeout = jiffies + ps->timeout_int;
	}

	trace_hfi1_rc_expired_time_slice(qp, false);
	return false;
}

void hfi1_do_send_from_rvt(struct rvt_qp *qp)
{
	hfi1_do_send(qp, false);
}

void _hfi1_do_send(struct work_struct *work)
{
	struct iowait *wait = container_of(work, struct iowait, iowork);
	struct rvt_qp *qp = iowait_to_qp(wait);

	hfi1_do_send(qp, true);
}

/**
 * hfi1_do_send - perform a send on a QP
 * @work: contains a pointer to the QP
 * @in_thread: true if in a workqueue thread
 *
 * Process entries in the send work queue until credit or queue is
 * exhausted.  Only allow one CPU to send a packet per QP.
 * Otherwise, two threads could send packets out of order.
 */
void hfi1_do_send(struct rvt_qp *qp, bool in_thread)
{
	struct hfi1_pkt_state ps;
	struct hfi1_qp_priv *priv = qp->priv;
	int (*make_req)(struct rvt_qp *qp, struct hfi1_pkt_state *ps);

	ps.dev = to_idev(qp->ibqp.device);
	ps.ibp = to_iport(qp->ibqp.device, qp->port_num);
	ps.ppd = ppd_from_ibp(ps.ibp);
	ps.in_thread = in_thread;

	trace_hfi1_rc_do_send(qp, in_thread);

	switch (qp->ibqp.qp_type) {
	case IB_QPT_RC:
		if (!loopback && ((rdma_ah_get_dlid(&qp->remote_ah_attr) &
				   ~((1 << ps.ppd->lmc) - 1)) ==
				  ps.ppd->lid)) {
			ruc_loopback(qp);
			return;
		}
		make_req = hfi1_make_rc_req;
		ps.timeout_int = qp->timeout_jiffies;
		break;
	case IB_QPT_UC:
		if (!loopback && ((rdma_ah_get_dlid(&qp->remote_ah_attr) &
				   ~((1 << ps.ppd->lmc) - 1)) ==
				  ps.ppd->lid)) {
			ruc_loopback(qp);
			return;
		}
		make_req = hfi1_make_uc_req;
		ps.timeout_int = SEND_RESCHED_TIMEOUT;
		break;
	default:
		make_req = hfi1_make_ud_req;
		ps.timeout_int = SEND_RESCHED_TIMEOUT;
	}

	spin_lock_irqsave(&qp->s_lock, ps.flags);

	/* Return if we are already busy processing a work request. */
	if (!hfi1_send_ok(qp)) {
		spin_unlock_irqrestore(&qp->s_lock, ps.flags);
		return;
	}

	qp->s_flags |= RVT_S_BUSY;

	ps.timeout_int = ps.timeout_int / 8;
	ps.timeout = jiffies + ps.timeout_int;
	ps.cpu = priv->s_sde ? priv->s_sde->cpu :
			cpumask_first(cpumask_of_node(ps.ppd->dd->node));
	ps.pkts_sent = false;

	/* insure a pre-built packet is handled  */
	ps.s_txreq = get_waiting_verbs_txreq(qp);
	do {
		/* Check for a constructed packet to be sent. */
		if (qp->s_hdrwords != 0) {
			spin_unlock_irqrestore(&qp->s_lock, ps.flags);
			/*
			 * If the packet cannot be sent now, return and
			 * the send engine will be woken up later.
			 */
			if (hfi1_verbs_send(qp, &ps))
				return;
			/* Record that s_ahg is empty. */
			qp->s_hdrwords = 0;
			/* allow other tasks to run */
			if (schedule_send_yield(qp, &ps))
				return;

			spin_lock_irqsave(&qp->s_lock, ps.flags);
		}
	} while (make_req(qp, &ps));
	iowait_starve_clear(ps.pkts_sent, &priv->s_iowait);
	spin_unlock_irqrestore(&qp->s_lock, ps.flags);
}

/*
 * This should be called with s_lock held.
 */
void hfi1_send_complete(struct rvt_qp *qp, struct rvt_swqe *wqe,
			enum ib_wc_status status)
{
	u32 old_last, last;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_OR_FLUSH_SEND))
		return;

	last = qp->s_last;
	old_last = last;
	trace_hfi1_qp_send_completion(qp, wqe, last);
	if (++last >= qp->s_size)
		last = 0;
	trace_hfi1_qp_send_completion(qp, wqe, last);
	qp->s_last = last;
	/* See post_send() */
	barrier();
	rvt_put_swqe(wqe);
	if (qp->ibqp.qp_type == IB_QPT_UD ||
	    qp->ibqp.qp_type == IB_QPT_SMI ||
	    qp->ibqp.qp_type == IB_QPT_GSI)
		atomic_dec(&ibah_to_rvtah(wqe->ud_wr.ah)->refcount);

	rvt_qp_swqe_complete(qp,
			     wqe,
			     ib_hfi1_wc_opcode[wqe->wr.opcode],
			     status);

	if (qp->s_acked == old_last)
		qp->s_acked = last;
	if (qp->s_cur == old_last)
		qp->s_cur = last;
	if (qp->s_tail == old_last)
		qp->s_tail = last;
	if (qp->state == IB_QPS_SQD && last == qp->s_cur)
		qp->s_draining = 0;
}
