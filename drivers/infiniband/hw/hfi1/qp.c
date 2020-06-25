/*
 * Copyright(c) 2015 - 2020 Intel Corporation.
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

#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/hash.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_qp.h>
#include <rdma/ib_verbs.h>

#include "hfi.h"
#include "qp.h"
#include "trace.h"
#include "verbs_txreq.h"

unsigned int hfi1_qp_table_size = 256;
module_param_named(qp_table_size, hfi1_qp_table_size, uint, S_IRUGO);
MODULE_PARM_DESC(qp_table_size, "QP table size");

static void flush_tx_list(struct rvt_qp *qp);
static int iowait_sleep(
	struct sdma_engine *sde,
	struct iowait_work *wait,
	struct sdma_txreq *stx,
	unsigned int seq,
	bool pkts_sent);
static void iowait_wakeup(struct iowait *wait, int reason);
static void iowait_sdma_drained(struct iowait *wait);
static void qp_pio_drain(struct rvt_qp *qp);

const struct rvt_operation_params hfi1_post_parms[RVT_OPERATION_MAX] = {
[IB_WR_RDMA_WRITE] = {
	.length = sizeof(struct ib_rdma_wr),
	.qpt_support = BIT(IB_QPT_UC) | BIT(IB_QPT_RC),
},

[IB_WR_RDMA_READ] = {
	.length = sizeof(struct ib_rdma_wr),
	.qpt_support = BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_ATOMIC,
},

[IB_WR_ATOMIC_CMP_AND_SWP] = {
	.length = sizeof(struct ib_atomic_wr),
	.qpt_support = BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_ATOMIC | RVT_OPERATION_ATOMIC_SGE,
},

[IB_WR_ATOMIC_FETCH_AND_ADD] = {
	.length = sizeof(struct ib_atomic_wr),
	.qpt_support = BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_ATOMIC | RVT_OPERATION_ATOMIC_SGE,
},

[IB_WR_RDMA_WRITE_WITH_IMM] = {
	.length = sizeof(struct ib_rdma_wr),
	.qpt_support = BIT(IB_QPT_UC) | BIT(IB_QPT_RC),
},

[IB_WR_SEND] = {
	.length = sizeof(struct ib_send_wr),
	.qpt_support = BIT(IB_QPT_UD) | BIT(IB_QPT_SMI) | BIT(IB_QPT_GSI) |
		       BIT(IB_QPT_UC) | BIT(IB_QPT_RC),
},

[IB_WR_SEND_WITH_IMM] = {
	.length = sizeof(struct ib_send_wr),
	.qpt_support = BIT(IB_QPT_UD) | BIT(IB_QPT_SMI) | BIT(IB_QPT_GSI) |
		       BIT(IB_QPT_UC) | BIT(IB_QPT_RC),
},

[IB_WR_REG_MR] = {
	.length = sizeof(struct ib_reg_wr),
	.qpt_support = BIT(IB_QPT_UC) | BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_LOCAL,
},

[IB_WR_LOCAL_INV] = {
	.length = sizeof(struct ib_send_wr),
	.qpt_support = BIT(IB_QPT_UC) | BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_LOCAL,
},

[IB_WR_SEND_WITH_INV] = {
	.length = sizeof(struct ib_send_wr),
	.qpt_support = BIT(IB_QPT_RC),
},

[IB_WR_OPFN] = {
	.length = sizeof(struct ib_atomic_wr),
	.qpt_support = BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_USE_RESERVE,
},

[IB_WR_TID_RDMA_WRITE] = {
	.length = sizeof(struct ib_rdma_wr),
	.qpt_support = BIT(IB_QPT_RC),
	.flags = RVT_OPERATION_IGN_RNR_CNT,
},

};

static void flush_list_head(struct list_head *l)
{
	while (!list_empty(l)) {
		struct sdma_txreq *tx;

		tx = list_first_entry(
			l,
			struct sdma_txreq,
			list);
		list_del_init(&tx->list);
		hfi1_put_txreq(
			container_of(tx, struct verbs_txreq, txreq));
	}
}

static void flush_tx_list(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	flush_list_head(&iowait_get_ib_work(&priv->s_iowait)->tx_head);
	flush_list_head(&iowait_get_tid_work(&priv->s_iowait)->tx_head);
}

static void flush_iowait(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	unsigned long flags;
	seqlock_t *lock = priv->s_iowait.lock;

	if (!lock)
		return;
	write_seqlock_irqsave(lock, flags);
	if (!list_empty(&priv->s_iowait.list)) {
		list_del_init(&priv->s_iowait.list);
		priv->s_iowait.lock = NULL;
		rvt_put_qp(qp);
	}
	write_sequnlock_irqrestore(lock, flags);
}

/**
 * This function is what we would push to the core layer if we wanted to be a
 * "first class citizen".  Instead we hide this here and rely on Verbs ULPs
 * to blindly pass the MTU enum value from the PathRecord to us.
 */
static inline int verbs_mtu_enum_to_int(struct ib_device *dev, enum ib_mtu mtu)
{
	/* Constraining 10KB packets to 8KB packets */
	if (mtu == (enum ib_mtu)OPA_MTU_10240)
		mtu = OPA_MTU_8192;
	return opa_mtu_enum_to_int((enum opa_mtu)mtu);
}

int hfi1_check_modify_qp(struct rvt_qp *qp, struct ib_qp_attr *attr,
			 int attr_mask, struct ib_udata *udata)
{
	struct ib_qp *ibqp = &qp->ibqp;
	struct hfi1_ibdev *dev = to_idev(ibqp->device);
	struct hfi1_devdata *dd = dd_from_dev(dev);
	u8 sc;

	if (attr_mask & IB_QP_AV) {
		sc = ah_to_sc(ibqp->device, &attr->ah_attr);
		if (sc == 0xf)
			return -EINVAL;

		if (!qp_to_sdma_engine(qp, sc) &&
		    dd->flags & HFI1_HAS_SEND_DMA)
			return -EINVAL;

		if (!qp_to_send_context(qp, sc))
			return -EINVAL;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		sc = ah_to_sc(ibqp->device, &attr->alt_ah_attr);
		if (sc == 0xf)
			return -EINVAL;

		if (!qp_to_sdma_engine(qp, sc) &&
		    dd->flags & HFI1_HAS_SEND_DMA)
			return -EINVAL;

		if (!qp_to_send_context(qp, sc))
			return -EINVAL;
	}

	return 0;
}

/*
 * qp_set_16b - Set the hdr_type based on whether the slid or the
 * dlid in the connection is extended. Only applicable for RC and UC
 * QPs. UD QPs determine this on the fly from the ah in the wqe
 */
static inline void qp_set_16b(struct rvt_qp *qp)
{
	struct hfi1_pportdata *ppd;
	struct hfi1_ibport *ibp;
	struct hfi1_qp_priv *priv = qp->priv;

	/* Update ah_attr to account for extended LIDs */
	hfi1_update_ah_attr(qp->ibqp.device, &qp->remote_ah_attr);

	/* Create 32 bit LIDs */
	hfi1_make_opa_lid(&qp->remote_ah_attr);

	if (!(rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH))
		return;

	ibp = to_iport(qp->ibqp.device, qp->port_num);
	ppd = ppd_from_ibp(ibp);
	priv->hdr_type = hfi1_get_hdr_type(ppd->lid, &qp->remote_ah_attr);
}

void hfi1_modify_qp(struct rvt_qp *qp, struct ib_qp_attr *attr,
		    int attr_mask, struct ib_udata *udata)
{
	struct ib_qp *ibqp = &qp->ibqp;
	struct hfi1_qp_priv *priv = qp->priv;

	if (attr_mask & IB_QP_AV) {
		priv->s_sc = ah_to_sc(ibqp->device, &qp->remote_ah_attr);
		priv->s_sde = qp_to_sdma_engine(qp, priv->s_sc);
		priv->s_sendcontext = qp_to_send_context(qp, priv->s_sc);
		qp_set_16b(qp);
	}

	if (attr_mask & IB_QP_PATH_MIG_STATE &&
	    attr->path_mig_state == IB_MIG_MIGRATED &&
	    qp->s_mig_state == IB_MIG_ARMED) {
		qp->s_flags |= HFI1_S_AHG_CLEAR;
		priv->s_sc = ah_to_sc(ibqp->device, &qp->remote_ah_attr);
		priv->s_sde = qp_to_sdma_engine(qp, priv->s_sc);
		priv->s_sendcontext = qp_to_send_context(qp, priv->s_sc);
		qp_set_16b(qp);
	}

	opfn_qp_init(qp, attr, attr_mask);
}

/**
 * hfi1_setup_wqe - set up the wqe
 * @qp - The qp
 * @wqe - The built wqe
 * @call_send - Determine if the send should be posted or scheduled.
 *
 * Perform setup of the wqe.  This is called
 * prior to inserting the wqe into the ring but after
 * the wqe has been setup by RDMAVT. This function
 * allows the driver the opportunity to perform
 * validation and additional setup of the wqe.
 *
 * Returns 0 on success, -EINVAL on failure
 *
 */
int hfi1_setup_wqe(struct rvt_qp *qp, struct rvt_swqe *wqe, bool *call_send)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct rvt_ah *ah;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;

	switch (qp->ibqp.qp_type) {
	case IB_QPT_RC:
		hfi1_setup_tid_rdma_wqe(qp, wqe);
		/* fall through */
	case IB_QPT_UC:
		if (wqe->length > 0x80000000U)
			return -EINVAL;
		if (wqe->length > qp->pmtu)
			*call_send = false;
		break;
	case IB_QPT_SMI:
		/*
		 * SM packets should exclusively use VL15 and their SL is
		 * ignored (IBTA v1.3, Section 3.5.8.2). Therefore, when ah
		 * is created, SL is 0 in most cases and as a result some
		 * fields (vl and pmtu) in ah may not be set correctly,
		 * depending on the SL2SC and SC2VL tables at the time.
		 */
		ppd = ppd_from_ibp(ibp);
		dd = dd_from_ppd(ppd);
		if (wqe->length > dd->vld[15].mtu)
			return -EINVAL;
		break;
	case IB_QPT_GSI:
	case IB_QPT_UD:
		ah = rvt_get_swqe_ah(wqe);
		if (wqe->length > (1 << ah->log_pmtu))
			return -EINVAL;
		if (ibp->sl_to_sc[rdma_ah_get_sl(&ah->attr)] == 0xf)
			return -EINVAL;
	default:
		break;
	}

	/*
	 * System latency between send and schedule is large enough that
	 * forcing call_send to true for piothreshold packets is necessary.
	 */
	if (wqe->length <= piothreshold)
		*call_send = true;
	return 0;
}

/**
 * _hfi1_schedule_send - schedule progress
 * @qp: the QP
 *
 * This schedules qp progress w/o regard to the s_flags.
 *
 * It is only used in the post send, which doesn't hold
 * the s_lock.
 */
bool _hfi1_schedule_send(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ibport *ibp =
		to_iport(qp->ibqp.device, qp->port_num);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);

	return iowait_schedule(&priv->s_iowait, ppd->hfi1_wq,
			       priv->s_sde ?
			       priv->s_sde->cpu :
			       cpumask_first(cpumask_of_node(dd->node)));
}

static void qp_pio_drain(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	if (!priv->s_sendcontext)
		return;
	while (iowait_pio_pending(&priv->s_iowait)) {
		write_seqlock_irq(&priv->s_sendcontext->waitlock);
		hfi1_sc_wantpiobuf_intr(priv->s_sendcontext, 1);
		write_sequnlock_irq(&priv->s_sendcontext->waitlock);
		iowait_pio_drain(&priv->s_iowait);
		write_seqlock_irq(&priv->s_sendcontext->waitlock);
		hfi1_sc_wantpiobuf_intr(priv->s_sendcontext, 0);
		write_sequnlock_irq(&priv->s_sendcontext->waitlock);
	}
}

/**
 * hfi1_schedule_send - schedule progress
 * @qp: the QP
 *
 * This schedules qp progress and caller should hold
 * the s_lock.
 * @return true if the first leg is scheduled;
 * false if the first leg is not scheduled.
 */
bool hfi1_schedule_send(struct rvt_qp *qp)
{
	lockdep_assert_held(&qp->s_lock);
	if (hfi1_send_ok(qp)) {
		_hfi1_schedule_send(qp);
		return true;
	}
	if (qp->s_flags & HFI1_S_ANY_WAIT_IO)
		iowait_set_flag(&((struct hfi1_qp_priv *)qp->priv)->s_iowait,
				IOWAIT_PENDING_IB);
	return false;
}

static void hfi1_qp_schedule(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	bool ret;

	if (iowait_flag_set(&priv->s_iowait, IOWAIT_PENDING_IB)) {
		ret = hfi1_schedule_send(qp);
		if (ret)
			iowait_clear_flag(&priv->s_iowait, IOWAIT_PENDING_IB);
	}
	if (iowait_flag_set(&priv->s_iowait, IOWAIT_PENDING_TID)) {
		ret = hfi1_schedule_tid_send(qp);
		if (ret)
			iowait_clear_flag(&priv->s_iowait, IOWAIT_PENDING_TID);
	}
}

void hfi1_qp_wakeup(struct rvt_qp *qp, u32 flag)
{
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (qp->s_flags & flag) {
		qp->s_flags &= ~flag;
		trace_hfi1_qpwakeup(qp, flag);
		hfi1_qp_schedule(qp);
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	/* Notify hfi1_destroy_qp() if it is waiting. */
	rvt_put_qp(qp);
}

void hfi1_qp_unbusy(struct rvt_qp *qp, struct iowait_work *wait)
{
	struct hfi1_qp_priv *priv = qp->priv;

	if (iowait_set_work_flag(wait) == IOWAIT_IB_SE) {
		qp->s_flags &= ~RVT_S_BUSY;
		/*
		 * If we are sending a first-leg packet from the second leg,
		 * we need to clear the busy flag from priv->s_flags to
		 * avoid a race condition when the qp wakes up before
		 * the call to hfi1_verbs_send() returns to the second
		 * leg. In that case, the second leg will terminate without
		 * being re-scheduled, resulting in failure to send TID RDMA
		 * WRITE DATA and TID RDMA ACK packets.
		 */
		if (priv->s_flags & HFI1_S_TID_BUSY_SET) {
			priv->s_flags &= ~(HFI1_S_TID_BUSY_SET |
					   RVT_S_BUSY);
			iowait_set_flag(&priv->s_iowait, IOWAIT_PENDING_TID);
		}
	} else {
		priv->s_flags &= ~RVT_S_BUSY;
	}
}

static int iowait_sleep(
	struct sdma_engine *sde,
	struct iowait_work *wait,
	struct sdma_txreq *stx,
	uint seq,
	bool pkts_sent)
{
	struct verbs_txreq *tx = container_of(stx, struct verbs_txreq, txreq);
	struct rvt_qp *qp;
	struct hfi1_qp_priv *priv;
	unsigned long flags;
	int ret = 0;

	qp = tx->qp;
	priv = qp->priv;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		/*
		 * If we couldn't queue the DMA request, save the info
		 * and try again later rather than destroying the
		 * buffer and undoing the side effects of the copy.
		 */
		/* Make a common routine? */
		list_add_tail(&stx->list, &wait->tx_head);
		write_seqlock(&sde->waitlock);
		if (sdma_progress(sde, seq, stx))
			goto eagain;
		if (list_empty(&priv->s_iowait.list)) {
			struct hfi1_ibport *ibp =
				to_iport(qp->ibqp.device, qp->port_num);

			ibp->rvp.n_dmawait++;
			qp->s_flags |= RVT_S_WAIT_DMA_DESC;
			iowait_get_priority(&priv->s_iowait);
			iowait_queue(pkts_sent, &priv->s_iowait,
				     &sde->dmawait);
			priv->s_iowait.lock = &sde->waitlock;
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_DMA_DESC);
			rvt_get_qp(qp);
		}
		write_sequnlock(&sde->waitlock);
		hfi1_qp_unbusy(qp, wait);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		ret = -EBUSY;
	} else {
		spin_unlock_irqrestore(&qp->s_lock, flags);
		hfi1_put_txreq(tx);
	}
	return ret;
eagain:
	write_sequnlock(&sde->waitlock);
	spin_unlock_irqrestore(&qp->s_lock, flags);
	list_del_init(&stx->list);
	return -EAGAIN;
}

static void iowait_wakeup(struct iowait *wait, int reason)
{
	struct rvt_qp *qp = iowait_to_qp(wait);

	WARN_ON(reason != SDMA_AVAIL_REASON);
	hfi1_qp_wakeup(qp, RVT_S_WAIT_DMA_DESC);
}

static void iowait_sdma_drained(struct iowait *wait)
{
	struct rvt_qp *qp = iowait_to_qp(wait);
	unsigned long flags;

	/*
	 * This happens when the send engine notes
	 * a QP in the error state and cannot
	 * do the flush work until that QP's
	 * sdma work has finished.
	 */
	spin_lock_irqsave(&qp->s_lock, flags);
	if (qp->s_flags & RVT_S_WAIT_DMA) {
		qp->s_flags &= ~RVT_S_WAIT_DMA;
		hfi1_schedule_send(qp);
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
}

static void hfi1_init_priority(struct iowait *w)
{
	struct rvt_qp *qp = iowait_to_qp(w);
	struct hfi1_qp_priv *priv = qp->priv;

	if (qp->s_flags & RVT_S_ACK_PENDING)
		w->priority++;
	if (priv->s_flags & RVT_S_ACK_PENDING)
		w->priority++;
}

/**
 * qp_to_sdma_engine - map a qp to a send engine
 * @qp: the QP
 * @sc5: the 5 bit sc
 *
 * Return:
 * A send engine for the qp or NULL for SMI type qp.
 */
struct sdma_engine *qp_to_sdma_engine(struct rvt_qp *qp, u8 sc5)
{
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct sdma_engine *sde;

	if (!(dd->flags & HFI1_HAS_SEND_DMA))
		return NULL;
	switch (qp->ibqp.qp_type) {
	case IB_QPT_SMI:
		return NULL;
	default:
		break;
	}
	sde = sdma_select_engine_sc(dd, qp->ibqp.qp_num >> dd->qos_shift, sc5);
	return sde;
}

/*
 * qp_to_send_context - map a qp to a send context
 * @qp: the QP
 * @sc5: the 5 bit sc
 *
 * Return:
 * A send context for the qp
 */
struct send_context *qp_to_send_context(struct rvt_qp *qp, u8 sc5)
{
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);

	switch (qp->ibqp.qp_type) {
	case IB_QPT_SMI:
		/* SMA packets to VL15 */
		return dd->vld[15].sc;
	default:
		break;
	}

	return pio_select_send_context_sc(dd, qp->ibqp.qp_num >> dd->qos_shift,
					  sc5);
}

static const char * const qp_type_str[] = {
	"SMI", "GSI", "RC", "UC", "UD",
};

static int qp_idle(struct rvt_qp *qp)
{
	return
		qp->s_last == qp->s_acked &&
		qp->s_acked == qp->s_cur &&
		qp->s_cur == qp->s_tail &&
		qp->s_tail == qp->s_head;
}

/**
 * qp_iter_print - print the qp information to seq_file
 * @s: the seq_file to emit the qp information on
 * @iter: the iterator for the qp hash list
 */
void qp_iter_print(struct seq_file *s, struct rvt_qp_iter *iter)
{
	struct rvt_swqe *wqe;
	struct rvt_qp *qp = iter->qp;
	struct hfi1_qp_priv *priv = qp->priv;
	struct sdma_engine *sde;
	struct send_context *send_context;
	struct rvt_ack_entry *e = NULL;
	struct rvt_srq *srq = qp->ibqp.srq ?
		ibsrq_to_rvtsrq(qp->ibqp.srq) : NULL;

	sde = qp_to_sdma_engine(qp, priv->s_sc);
	wqe = rvt_get_swqe_ptr(qp, qp->s_last);
	send_context = qp_to_send_context(qp, priv->s_sc);
	if (qp->s_ack_queue)
		e = &qp->s_ack_queue[qp->s_tail_ack_queue];
	seq_printf(s,
		   "N %d %s QP %x R %u %s %u %u f=%x %u %u %u %u %u %u SPSN %x %x %x %x %x RPSN %x S(%u %u %u %u %u %u %u) R(%u %u %u) RQP %x LID %x SL %u MTU %u %u %u %u %u SDE %p,%u SC %p,%u SCQ %u %u PID %d OS %x %x E %x %x %x RNR %d %s %d\n",
		   iter->n,
		   qp_idle(qp) ? "I" : "B",
		   qp->ibqp.qp_num,
		   atomic_read(&qp->refcount),
		   qp_type_str[qp->ibqp.qp_type],
		   qp->state,
		   wqe ? wqe->wr.opcode : 0,
		   qp->s_flags,
		   iowait_sdma_pending(&priv->s_iowait),
		   iowait_pio_pending(&priv->s_iowait),
		   !list_empty(&priv->s_iowait.list),
		   qp->timeout,
		   wqe ? wqe->ssn : 0,
		   qp->s_lsn,
		   qp->s_last_psn,
		   qp->s_psn, qp->s_next_psn,
		   qp->s_sending_psn, qp->s_sending_hpsn,
		   qp->r_psn,
		   qp->s_last, qp->s_acked, qp->s_cur,
		   qp->s_tail, qp->s_head, qp->s_size,
		   qp->s_avail,
		   /* ack_queue ring pointers, size */
		   qp->s_tail_ack_queue, qp->r_head_ack_queue,
		   rvt_max_atomic(&to_idev(qp->ibqp.device)->rdi),
		   /* remote QP info  */
		   qp->remote_qpn,
		   rdma_ah_get_dlid(&qp->remote_ah_attr),
		   rdma_ah_get_sl(&qp->remote_ah_attr),
		   qp->pmtu,
		   qp->s_retry,
		   qp->s_retry_cnt,
		   qp->s_rnr_retry_cnt,
		   qp->s_rnr_retry,
		   sde,
		   sde ? sde->this_idx : 0,
		   send_context,
		   send_context ? send_context->sw_index : 0,
		   ib_cq_head(qp->ibqp.send_cq),
		   ib_cq_tail(qp->ibqp.send_cq),
		   qp->pid,
		   qp->s_state,
		   qp->s_ack_state,
		   /* ack queue information */
		   e ? e->opcode : 0,
		   e ? e->psn : 0,
		   e ? e->lpsn : 0,
		   qp->r_min_rnr_timer,
		   srq ? "SRQ" : "RQ",
		   srq ? srq->rq.size : qp->r_rq.size
		);
}

void *qp_priv_alloc(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv;

	priv = kzalloc_node(sizeof(*priv), GFP_KERNEL, rdi->dparms.node);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->owner = qp;

	priv->s_ahg = kzalloc_node(sizeof(*priv->s_ahg), GFP_KERNEL,
				   rdi->dparms.node);
	if (!priv->s_ahg) {
		kfree(priv);
		return ERR_PTR(-ENOMEM);
	}
	iowait_init(
		&priv->s_iowait,
		1,
		_hfi1_do_send,
		_hfi1_do_tid_send,
		iowait_sleep,
		iowait_wakeup,
		iowait_sdma_drained,
		hfi1_init_priority);
	/* Init to a value to start the running average correctly */
	priv->s_running_pkt_size = piothreshold / 2;
	return priv;
}

void qp_priv_free(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	hfi1_qp_priv_tid_free(rdi, qp);
	kfree(priv->s_ahg);
	kfree(priv);
}

unsigned free_all_qps(struct rvt_dev_info *rdi)
{
	struct hfi1_ibdev *verbs_dev = container_of(rdi,
						    struct hfi1_ibdev,
						    rdi);
	struct hfi1_devdata *dd = container_of(verbs_dev,
					       struct hfi1_devdata,
					       verbs_dev);
	int n;
	unsigned qp_inuse = 0;

	for (n = 0; n < dd->num_pports; n++) {
		struct hfi1_ibport *ibp = &dd->pport[n].ibport_data;

		rcu_read_lock();
		if (rcu_dereference(ibp->rvp.qp[0]))
			qp_inuse++;
		if (rcu_dereference(ibp->rvp.qp[1]))
			qp_inuse++;
		rcu_read_unlock();
	}

	return qp_inuse;
}

void flush_qp_waiters(struct rvt_qp *qp)
{
	lockdep_assert_held(&qp->s_lock);
	flush_iowait(qp);
	hfi1_tid_rdma_flush_wait(qp);
}

void stop_send_queue(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	iowait_cancel_work(&priv->s_iowait);
	if (cancel_work_sync(&priv->tid_rdma.trigger_work))
		rvt_put_qp(qp);
}

void quiesce_qp(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	hfi1_del_tid_reap_timer(qp);
	hfi1_del_tid_retry_timer(qp);
	iowait_sdma_drain(&priv->s_iowait);
	qp_pio_drain(qp);
	flush_tx_list(qp);
}

void notify_qp_reset(struct rvt_qp *qp)
{
	hfi1_qp_kern_exp_rcv_clear_all(qp);
	qp->r_adefered = 0;
	clear_ahg(qp);

	/* Clear any OPFN state */
	if (qp->ibqp.qp_type == IB_QPT_RC)
		opfn_conn_error(qp);
}

/*
 * Switch to alternate path.
 * The QP s_lock should be held and interrupts disabled.
 */
void hfi1_migrate_qp(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_event ev;

	qp->s_mig_state = IB_MIG_MIGRATED;
	qp->remote_ah_attr = qp->alt_ah_attr;
	qp->port_num = rdma_ah_get_port_num(&qp->alt_ah_attr);
	qp->s_pkey_index = qp->s_alt_pkey_index;
	qp->s_flags |= HFI1_S_AHG_CLEAR;
	priv->s_sc = ah_to_sc(qp->ibqp.device, &qp->remote_ah_attr);
	priv->s_sde = qp_to_sdma_engine(qp, priv->s_sc);
	qp_set_16b(qp);

	ev.device = qp->ibqp.device;
	ev.element.qp = &qp->ibqp;
	ev.event = IB_EVENT_PATH_MIG;
	qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
}

int mtu_to_path_mtu(u32 mtu)
{
	return mtu_to_enum(mtu, OPA_MTU_8192);
}

u32 mtu_from_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp, u32 pmtu)
{
	u32 mtu;
	struct hfi1_ibdev *verbs_dev = container_of(rdi,
						    struct hfi1_ibdev,
						    rdi);
	struct hfi1_devdata *dd = container_of(verbs_dev,
					       struct hfi1_devdata,
					       verbs_dev);
	struct hfi1_ibport *ibp;
	u8 sc, vl;

	ibp = &dd->pport[qp->port_num - 1].ibport_data;
	sc = ibp->sl_to_sc[rdma_ah_get_sl(&qp->remote_ah_attr)];
	vl = sc_to_vlt(dd, sc);

	mtu = verbs_mtu_enum_to_int(qp->ibqp.device, pmtu);
	if (vl < PER_VL_SEND_CONTEXTS)
		mtu = min_t(u32, mtu, dd->vld[vl].mtu);
	return mtu;
}

int get_pmtu_from_attr(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		       struct ib_qp_attr *attr)
{
	int mtu, pidx = qp->port_num - 1;
	struct hfi1_ibdev *verbs_dev = container_of(rdi,
						    struct hfi1_ibdev,
						    rdi);
	struct hfi1_devdata *dd = container_of(verbs_dev,
					       struct hfi1_devdata,
					       verbs_dev);
	mtu = verbs_mtu_enum_to_int(qp->ibqp.device, attr->path_mtu);
	if (mtu == -1)
		return -1; /* values less than 0 are error */

	if (mtu > dd->pport[pidx].ibmtu)
		return mtu_to_enum(dd->pport[pidx].ibmtu, IB_MTU_2048);
	else
		return attr->path_mtu;
}

void notify_error_qp(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	seqlock_t *lock = priv->s_iowait.lock;

	if (lock) {
		write_seqlock(lock);
		if (!list_empty(&priv->s_iowait.list) &&
		    !(qp->s_flags & RVT_S_BUSY) &&
		    !(priv->s_flags & RVT_S_BUSY)) {
			qp->s_flags &= ~HFI1_S_ANY_WAIT_IO;
			iowait_clear_flag(&priv->s_iowait, IOWAIT_PENDING_IB);
			iowait_clear_flag(&priv->s_iowait, IOWAIT_PENDING_TID);
			list_del_init(&priv->s_iowait.list);
			priv->s_iowait.lock = NULL;
			rvt_put_qp(qp);
		}
		write_sequnlock(lock);
	}

	if (!(qp->s_flags & RVT_S_BUSY) && !(priv->s_flags & RVT_S_BUSY)) {
		qp->s_hdrwords = 0;
		if (qp->s_rdma_mr) {
			rvt_put_mr(qp->s_rdma_mr);
			qp->s_rdma_mr = NULL;
		}
		flush_tx_list(qp);
	}
}

/**
 * hfi1_qp_iter_cb - callback for iterator
 * @qp - the qp
 * @v - the sl in low bits of v
 *
 * This is called from the iterator callback to work
 * on an individual qp.
 */
static void hfi1_qp_iter_cb(struct rvt_qp *qp, u64 v)
{
	int lastwqe;
	struct ib_event ev;
	struct hfi1_ibport *ibp =
		to_iport(qp->ibqp.device, qp->port_num);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	u8 sl = (u8)v;

	if (qp->port_num != ppd->port ||
	    (qp->ibqp.qp_type != IB_QPT_UC &&
	     qp->ibqp.qp_type != IB_QPT_RC) ||
	    rdma_ah_get_sl(&qp->remote_ah_attr) != sl ||
	    !(ib_rvt_state_ops[qp->state] & RVT_POST_SEND_OK))
		return;

	spin_lock_irq(&qp->r_lock);
	spin_lock(&qp->s_hlock);
	spin_lock(&qp->s_lock);
	lastwqe = rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);
	spin_unlock(&qp->s_lock);
	spin_unlock(&qp->s_hlock);
	spin_unlock_irq(&qp->r_lock);
	if (lastwqe) {
		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
}

/**
 * hfi1_error_port_qps - put a port's RC/UC qps into error state
 * @ibp: the ibport.
 * @sl: the service level.
 *
 * This function places all RC/UC qps with a given service level into error
 * state. It is generally called to force upper lay apps to abandon stale qps
 * after an sl->sc mapping change.
 */
void hfi1_error_port_qps(struct hfi1_ibport *ibp, u8 sl)
{
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct hfi1_ibdev *dev = &ppd->dd->verbs_dev;

	rvt_qp_iter(&dev->rdi, sl, hfi1_qp_iter_cb);
}
