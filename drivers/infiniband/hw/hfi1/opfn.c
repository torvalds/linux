// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#include "hfi.h"
#include "trace.h"
#include "qp.h"
#include "opfn.h"

#define IB_BTHE_E                 BIT(IB_BTHE_E_SHIFT)

#define OPFN_CODE(code) BIT((code) - 1)
#define OPFN_MASK(code) OPFN_CODE(STL_VERBS_EXTD_##code)

struct hfi1_opfn_type {
	bool (*request)(struct rvt_qp *qp, u64 *data);
	bool (*response)(struct rvt_qp *qp, u64 *data);
	bool (*reply)(struct rvt_qp *qp, u64 data);
	void (*error)(struct rvt_qp *qp);
};

static struct hfi1_opfn_type hfi1_opfn_handlers[STL_VERBS_EXTD_MAX] = {
	[STL_VERBS_EXTD_TID_RDMA] = {
		.request = tid_rdma_conn_req,
		.response = tid_rdma_conn_resp,
		.reply = tid_rdma_conn_reply,
		.error = tid_rdma_conn_error,
	},
};

static struct workqueue_struct *opfn_wq;

static void opfn_schedule_conn_request(struct rvt_qp *qp);

static bool hfi1_opfn_extended(u32 bth1)
{
	return !!(bth1 & IB_BTHE_E);
}

static void opfn_conn_request(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_atomic_wr wr;
	u16 mask, capcode;
	struct hfi1_opfn_type *extd;
	u64 data;
	unsigned long flags;
	int ret = 0;

	trace_hfi1_opfn_state_conn_request(qp);
	spin_lock_irqsave(&priv->opfn.lock, flags);
	/*
	 * Exit if the extended bit is not set, or if nothing is requested, or
	 * if we have completed all requests, or if a previous request is in
	 * progress
	 */
	if (!priv->opfn.extended || !priv->opfn.requested ||
	    priv->opfn.requested == priv->opfn.completed || priv->opfn.curr)
		goto done;

	mask = priv->opfn.requested & ~priv->opfn.completed;
	capcode = ilog2(mask & ~(mask - 1)) + 1;
	if (capcode >= STL_VERBS_EXTD_MAX) {
		priv->opfn.completed |= OPFN_CODE(capcode);
		goto done;
	}

	extd = &hfi1_opfn_handlers[capcode];
	if (!extd || !extd->request || !extd->request(qp, &data)) {
		/*
		 * Either there is no handler for this capability or the request
		 * packet could not be generated. Either way, mark it as done so
		 * we don't keep attempting to complete it.
		 */
		priv->opfn.completed |= OPFN_CODE(capcode);
		goto done;
	}

	trace_hfi1_opfn_data_conn_request(qp, capcode, data);
	data = (data & ~0xf) | capcode;

	memset(&wr, 0, sizeof(wr));
	wr.wr.opcode = IB_WR_OPFN;
	wr.remote_addr = HFI1_VERBS_E_ATOMIC_VADDR;
	wr.compare_add = data;

	priv->opfn.curr = capcode;	/* A new request is now in progress */
	/* Drop opfn.lock before calling ib_post_send() */
	spin_unlock_irqrestore(&priv->opfn.lock, flags);

	ret = ib_post_send(&qp->ibqp, &wr.wr, NULL);
	if (ret)
		goto err;
	trace_hfi1_opfn_state_conn_request(qp);
	return;
err:
	trace_hfi1_msg_opfn_conn_request(qp, "ib_ost_send failed: ret = ",
					 (u64)ret);
	spin_lock_irqsave(&priv->opfn.lock, flags);
	/*
	 * In case of an unexpected error return from ib_post_send
	 * clear opfn.curr and reschedule to try again
	 */
	priv->opfn.curr = STL_VERBS_EXTD_NONE;
	opfn_schedule_conn_request(qp);
done:
	spin_unlock_irqrestore(&priv->opfn.lock, flags);
}

void opfn_send_conn_request(struct work_struct *work)
{
	struct hfi1_opfn_data *od;
	struct hfi1_qp_priv *qpriv;

	od = container_of(work, struct hfi1_opfn_data, opfn_work);
	qpriv = container_of(od, struct hfi1_qp_priv, opfn);

	opfn_conn_request(qpriv->owner);
}

/*
 * When QP s_lock is held in the caller, the OPFN request must be scheduled
 * to a different workqueue to avoid double locking QP s_lock in call to
 * ib_post_send in opfn_conn_request
 */
static void opfn_schedule_conn_request(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	trace_hfi1_opfn_state_sched_conn_request(qp);
	queue_work(opfn_wq, &priv->opfn.opfn_work);
}

void opfn_conn_response(struct rvt_qp *qp, struct rvt_ack_entry *e,
			struct ib_atomic_eth *ateth)
{
	struct hfi1_qp_priv *priv = qp->priv;
	u64 data = be64_to_cpu(ateth->compare_data);
	struct hfi1_opfn_type *extd;
	u8 capcode;
	unsigned long flags;

	trace_hfi1_opfn_state_conn_response(qp);
	capcode = data & 0xf;
	trace_hfi1_opfn_data_conn_response(qp, capcode, data);
	if (!capcode || capcode >= STL_VERBS_EXTD_MAX)
		return;

	extd = &hfi1_opfn_handlers[capcode];

	if (!extd || !extd->response) {
		e->atomic_data = capcode;
		return;
	}

	spin_lock_irqsave(&priv->opfn.lock, flags);
	if (priv->opfn.completed & OPFN_CODE(capcode)) {
		/*
		 * We are receiving a request for a feature that has already
		 * been negotiated. This may mean that the other side has reset
		 */
		priv->opfn.completed &= ~OPFN_CODE(capcode);
		if (extd->error)
			extd->error(qp);
	}

	if (extd->response(qp, &data))
		priv->opfn.completed |= OPFN_CODE(capcode);
	e->atomic_data = (data & ~0xf) | capcode;
	trace_hfi1_opfn_state_conn_response(qp);
	spin_unlock_irqrestore(&priv->opfn.lock, flags);
}

void opfn_conn_reply(struct rvt_qp *qp, u64 data)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_opfn_type *extd;
	u8 capcode;
	unsigned long flags;

	trace_hfi1_opfn_state_conn_reply(qp);
	capcode = data & 0xf;
	trace_hfi1_opfn_data_conn_reply(qp, capcode, data);
	if (!capcode || capcode >= STL_VERBS_EXTD_MAX)
		return;

	spin_lock_irqsave(&priv->opfn.lock, flags);
	/*
	 * Either there is no previous request or the reply is not for the
	 * current request
	 */
	if (!priv->opfn.curr || capcode != priv->opfn.curr)
		goto done;

	extd = &hfi1_opfn_handlers[capcode];

	if (!extd || !extd->reply)
		goto clear;

	if (extd->reply(qp, data))
		priv->opfn.completed |= OPFN_CODE(capcode);
clear:
	/*
	 * Clear opfn.curr to indicate that the previous request is no longer in
	 * progress
	 */
	priv->opfn.curr = STL_VERBS_EXTD_NONE;
	trace_hfi1_opfn_state_conn_reply(qp);
done:
	spin_unlock_irqrestore(&priv->opfn.lock, flags);
}

void opfn_conn_error(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_opfn_type *extd = NULL;
	unsigned long flags;
	u16 capcode;

	trace_hfi1_opfn_state_conn_error(qp);
	trace_hfi1_msg_opfn_conn_error(qp, "error. qp state ", (u64)qp->state);
	/*
	 * The QP has gone into the Error state. We have to invalidate all
	 * negotiated feature, including the one in progress (if any). The RC
	 * QP handling will clean the WQE for the connection request.
	 */
	spin_lock_irqsave(&priv->opfn.lock, flags);
	while (priv->opfn.completed) {
		capcode = priv->opfn.completed & ~(priv->opfn.completed - 1);
		extd = &hfi1_opfn_handlers[ilog2(capcode) + 1];
		if (extd->error)
			extd->error(qp);
		priv->opfn.completed &= ~OPFN_CODE(capcode);
	}
	priv->opfn.extended = 0;
	priv->opfn.requested = 0;
	priv->opfn.curr = STL_VERBS_EXTD_NONE;
	spin_unlock_irqrestore(&priv->opfn.lock, flags);
}

void opfn_qp_init(struct rvt_qp *qp, struct ib_qp_attr *attr, int attr_mask)
{
	struct ib_qp *ibqp = &qp->ibqp;
	struct hfi1_qp_priv *priv = qp->priv;
	unsigned long flags;

	if (attr_mask & IB_QP_RETRY_CNT)
		priv->s_retry = attr->retry_cnt;

	spin_lock_irqsave(&priv->opfn.lock, flags);
	if (ibqp->qp_type == IB_QPT_RC && HFI1_CAP_IS_KSET(TID_RDMA)) {
		struct tid_rdma_params *local = &priv->tid_rdma.local;

		if (attr_mask & IB_QP_TIMEOUT)
			priv->tid_retry_timeout_jiffies = qp->timeout_jiffies;
		if (qp->pmtu == enum_to_mtu(OPA_MTU_4096) ||
		    qp->pmtu == enum_to_mtu(OPA_MTU_8192)) {
			tid_rdma_opfn_init(qp, local);
			/*
			 * We only want to set the OPFN requested bit when the
			 * QP transitions to RTS.
			 */
			if (attr_mask & IB_QP_STATE &&
			    attr->qp_state == IB_QPS_RTS) {
				priv->opfn.requested |= OPFN_MASK(TID_RDMA);
				/*
				 * If the QP is transitioning to RTS and the
				 * opfn.completed for TID RDMA has already been
				 * set, the QP is being moved *back* into RTS.
				 * We can now renegotiate the TID RDMA
				 * parameters.
				 */
				if (priv->opfn.completed &
				    OPFN_MASK(TID_RDMA)) {
					priv->opfn.completed &=
						~OPFN_MASK(TID_RDMA);
					/*
					 * Since the opfn.completed bit was
					 * already set, it is safe to assume
					 * that the opfn.extended is also set.
					 */
					opfn_schedule_conn_request(qp);
				}
			}
		} else {
			memset(local, 0, sizeof(*local));
		}
	}
	spin_unlock_irqrestore(&priv->opfn.lock, flags);
}

void opfn_trigger_conn_request(struct rvt_qp *qp, u32 bth1)
{
	struct hfi1_qp_priv *priv = qp->priv;

	if (!priv->opfn.extended && hfi1_opfn_extended(bth1) &&
	    HFI1_CAP_IS_KSET(OPFN)) {
		priv->opfn.extended = 1;
		if (qp->state == IB_QPS_RTS)
			opfn_conn_request(qp);
	}
}

int opfn_init(void)
{
	opfn_wq = alloc_workqueue("hfi_opfn",
				  WQ_SYSFS | WQ_HIGHPRI | WQ_CPU_INTENSIVE |
				  WQ_MEM_RECLAIM,
				  HFI1_MAX_ACTIVE_WORKQUEUE_ENTRIES);
	if (!opfn_wq)
		return -ENOMEM;

	return 0;
}

void opfn_exit(void)
{
	if (opfn_wq) {
		destroy_workqueue(opfn_wq);
		opfn_wq = NULL;
	}
}
