// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */

#include "hfi.h"
#include "qp.h"
#include "rc.h"
#include "verbs.h"
#include "tid_rdma.h"
#include "exp_rcv.h"
#include "trace.h"

/**
 * DOC: TID RDMA READ protocol
 *
 * This is an end-to-end protocol at the hfi1 level between two nodes that
 * improves performance by avoiding data copy on the requester side. It
 * converts a qualified RDMA READ request into a TID RDMA READ request on
 * the requester side and thereafter handles the request and response
 * differently. To be qualified, the RDMA READ request should meet the
 * following:
 * -- The total data length should be greater than 256K;
 * -- The total data length should be a multiple of 4K page size;
 * -- Each local scatter-gather entry should be 4K page aligned;
 * -- Each local scatter-gather entry should be a multiple of 4K page size;
 */

#define RCV_TID_FLOW_TABLE_CTRL_FLOW_VALID_SMASK BIT_ULL(32)
#define RCV_TID_FLOW_TABLE_CTRL_HDR_SUPP_EN_SMASK BIT_ULL(33)
#define RCV_TID_FLOW_TABLE_CTRL_KEEP_AFTER_SEQ_ERR_SMASK BIT_ULL(34)
#define RCV_TID_FLOW_TABLE_CTRL_KEEP_ON_GEN_ERR_SMASK BIT_ULL(35)
#define RCV_TID_FLOW_TABLE_STATUS_SEQ_MISMATCH_SMASK BIT_ULL(37)
#define RCV_TID_FLOW_TABLE_STATUS_GEN_MISMATCH_SMASK BIT_ULL(38)

/* Maximum number of packets within a flow generation. */
#define MAX_TID_FLOW_PSN BIT(HFI1_KDETH_BTH_SEQ_SHIFT)

#define GENERATION_MASK 0xFFFFF

static u32 mask_generation(u32 a)
{
	return a & GENERATION_MASK;
}

/* Reserved generation value to set to unused flows for kernel contexts */
#define KERN_GENERATION_RESERVED mask_generation(U32_MAX)

/*
 * J_KEY for kernel contexts when TID RDMA is used.
 * See generate_jkey() in hfi.h for more information.
 */
#define TID_RDMA_JKEY                   32
#define HFI1_KERNEL_MIN_JKEY HFI1_ADMIN_JKEY_RANGE
#define HFI1_KERNEL_MAX_JKEY (2 * HFI1_ADMIN_JKEY_RANGE - 1)

/* Maximum number of segments in flight per QP request. */
#define TID_RDMA_MAX_READ_SEGS_PER_REQ  6
#define TID_RDMA_MAX_WRITE_SEGS_PER_REQ 4
#define MAX_REQ max_t(u16, TID_RDMA_MAX_READ_SEGS_PER_REQ, \
			TID_RDMA_MAX_WRITE_SEGS_PER_REQ)
#define MAX_FLOWS roundup_pow_of_two(MAX_REQ + 1)

#define MAX_EXPECTED_PAGES     (MAX_EXPECTED_BUFFER / PAGE_SIZE)

#define TID_RDMA_DESTQP_FLOW_SHIFT      11
#define TID_RDMA_DESTQP_FLOW_MASK       0x1f

#define TID_FLOW_SW_PSN BIT(0)

#define TID_OPFN_QP_CTXT_MASK 0xff
#define TID_OPFN_QP_CTXT_SHIFT 56
#define TID_OPFN_QP_KDETH_MASK 0xff
#define TID_OPFN_QP_KDETH_SHIFT 48
#define TID_OPFN_MAX_LEN_MASK 0x7ff
#define TID_OPFN_MAX_LEN_SHIFT 37
#define TID_OPFN_TIMEOUT_MASK 0x1f
#define TID_OPFN_TIMEOUT_SHIFT 32
#define TID_OPFN_RESERVED_MASK 0x3f
#define TID_OPFN_RESERVED_SHIFT 26
#define TID_OPFN_URG_MASK 0x1
#define TID_OPFN_URG_SHIFT 25
#define TID_OPFN_VER_MASK 0x7
#define TID_OPFN_VER_SHIFT 22
#define TID_OPFN_JKEY_MASK 0x3f
#define TID_OPFN_JKEY_SHIFT 16
#define TID_OPFN_MAX_READ_MASK 0x3f
#define TID_OPFN_MAX_READ_SHIFT 10
#define TID_OPFN_MAX_WRITE_MASK 0x3f
#define TID_OPFN_MAX_WRITE_SHIFT 4

/*
 * OPFN TID layout
 *
 * 63               47               31               15
 * NNNNNNNNKKKKKKKK MMMMMMMMMMMTTTTT DDDDDDUVVVJJJJJJ RRRRRRWWWWWWCCCC
 * 3210987654321098 7654321098765432 1098765432109876 5432109876543210
 * N - the context Number
 * K - the Kdeth_qp
 * M - Max_len
 * T - Timeout
 * D - reserveD
 * V - version
 * U - Urg capable
 * J - Jkey
 * R - max_Read
 * W - max_Write
 * C - Capcode
 */

static void tid_rdma_trigger_resume(struct work_struct *work);
static void hfi1_kern_exp_rcv_free_flows(struct tid_rdma_request *req);
static int hfi1_kern_exp_rcv_alloc_flows(struct tid_rdma_request *req,
					 gfp_t gfp);
static void hfi1_init_trdma_req(struct rvt_qp *qp,
				struct tid_rdma_request *req);

static u64 tid_rdma_opfn_encode(struct tid_rdma_params *p)
{
	return
		(((u64)p->qp & TID_OPFN_QP_CTXT_MASK) <<
			TID_OPFN_QP_CTXT_SHIFT) |
		((((u64)p->qp >> 16) & TID_OPFN_QP_KDETH_MASK) <<
			TID_OPFN_QP_KDETH_SHIFT) |
		(((u64)((p->max_len >> PAGE_SHIFT) - 1) &
			TID_OPFN_MAX_LEN_MASK) << TID_OPFN_MAX_LEN_SHIFT) |
		(((u64)p->timeout & TID_OPFN_TIMEOUT_MASK) <<
			TID_OPFN_TIMEOUT_SHIFT) |
		(((u64)p->urg & TID_OPFN_URG_MASK) << TID_OPFN_URG_SHIFT) |
		(((u64)p->jkey & TID_OPFN_JKEY_MASK) << TID_OPFN_JKEY_SHIFT) |
		(((u64)p->max_read & TID_OPFN_MAX_READ_MASK) <<
			TID_OPFN_MAX_READ_SHIFT) |
		(((u64)p->max_write & TID_OPFN_MAX_WRITE_MASK) <<
			TID_OPFN_MAX_WRITE_SHIFT);
}

static void tid_rdma_opfn_decode(struct tid_rdma_params *p, u64 data)
{
	p->max_len = (((data >> TID_OPFN_MAX_LEN_SHIFT) &
		TID_OPFN_MAX_LEN_MASK) + 1) << PAGE_SHIFT;
	p->jkey = (data >> TID_OPFN_JKEY_SHIFT) & TID_OPFN_JKEY_MASK;
	p->max_write = (data >> TID_OPFN_MAX_WRITE_SHIFT) &
		TID_OPFN_MAX_WRITE_MASK;
	p->max_read = (data >> TID_OPFN_MAX_READ_SHIFT) &
		TID_OPFN_MAX_READ_MASK;
	p->qp =
		((((data >> TID_OPFN_QP_KDETH_SHIFT) & TID_OPFN_QP_KDETH_MASK)
			<< 16) |
		((data >> TID_OPFN_QP_CTXT_SHIFT) & TID_OPFN_QP_CTXT_MASK));
	p->urg = (data >> TID_OPFN_URG_SHIFT) & TID_OPFN_URG_MASK;
	p->timeout = (data >> TID_OPFN_TIMEOUT_SHIFT) & TID_OPFN_TIMEOUT_MASK;
}

void tid_rdma_opfn_init(struct rvt_qp *qp, struct tid_rdma_params *p)
{
	struct hfi1_qp_priv *priv = qp->priv;

	p->qp = (kdeth_qp << 16) | priv->rcd->ctxt;
	p->max_len = TID_RDMA_MAX_SEGMENT_SIZE;
	p->jkey = priv->rcd->jkey;
	p->max_read = TID_RDMA_MAX_READ_SEGS_PER_REQ;
	p->max_write = TID_RDMA_MAX_WRITE_SEGS_PER_REQ;
	p->timeout = qp->timeout;
	p->urg = is_urg_masked(priv->rcd);
}

bool tid_rdma_conn_req(struct rvt_qp *qp, u64 *data)
{
	struct hfi1_qp_priv *priv = qp->priv;

	*data = tid_rdma_opfn_encode(&priv->tid_rdma.local);
	return true;
}

bool tid_rdma_conn_reply(struct rvt_qp *qp, u64 data)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct tid_rdma_params *remote, *old;
	bool ret = true;

	old = rcu_dereference_protected(priv->tid_rdma.remote,
					lockdep_is_held(&priv->opfn.lock));
	data &= ~0xfULL;
	/*
	 * If data passed in is zero, return true so as not to continue the
	 * negotiation process
	 */
	if (!data || !HFI1_CAP_IS_KSET(TID_RDMA))
		goto null;
	/*
	 * If kzalloc fails, return false. This will result in:
	 * * at the requester a new OPFN request being generated to retry
	 *   the negotiation
	 * * at the responder, 0 being returned to the requester so as to
	 *   disable TID RDMA at both the requester and the responder
	 */
	remote = kzalloc(sizeof(*remote), GFP_ATOMIC);
	if (!remote) {
		ret = false;
		goto null;
	}

	tid_rdma_opfn_decode(remote, data);
	priv->tid_timer_timeout_jiffies =
		usecs_to_jiffies((((4096UL * (1UL << remote->timeout)) /
				   1000UL) << 3) * 7);
	trace_hfi1_opfn_param(qp, 0, &priv->tid_rdma.local);
	trace_hfi1_opfn_param(qp, 1, remote);
	rcu_assign_pointer(priv->tid_rdma.remote, remote);
	/*
	 * A TID RDMA READ request's segment size is not equal to
	 * remote->max_len only when the request's data length is smaller
	 * than remote->max_len. In that case, there will be only one segment.
	 * Therefore, when priv->pkts_ps is used to calculate req->cur_seg
	 * during retry, it will lead to req->cur_seg = 0, which is exactly
	 * what is expected.
	 */
	priv->pkts_ps = (u16)rvt_div_mtu(qp, remote->max_len);
	priv->timeout_shift = ilog2(priv->pkts_ps - 1) + 1;
	goto free;
null:
	RCU_INIT_POINTER(priv->tid_rdma.remote, NULL);
	priv->timeout_shift = 0;
free:
	if (old)
		kfree_rcu(old, rcu_head);
	return ret;
}

bool tid_rdma_conn_resp(struct rvt_qp *qp, u64 *data)
{
	bool ret;

	ret = tid_rdma_conn_reply(qp, *data);
	*data = 0;
	/*
	 * If tid_rdma_conn_reply() returns error, set *data as 0 to indicate
	 * TID RDMA could not be enabled. This will result in TID RDMA being
	 * disabled at the requester too.
	 */
	if (ret)
		(void)tid_rdma_conn_req(qp, data);
	return ret;
}

void tid_rdma_conn_error(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct tid_rdma_params *old;

	old = rcu_dereference_protected(priv->tid_rdma.remote,
					lockdep_is_held(&priv->opfn.lock));
	RCU_INIT_POINTER(priv->tid_rdma.remote, NULL);
	if (old)
		kfree_rcu(old, rcu_head);
}

/* This is called at context initialization time */
int hfi1_kern_exp_rcv_init(struct hfi1_ctxtdata *rcd, int reinit)
{
	if (reinit)
		return 0;

	BUILD_BUG_ON(TID_RDMA_JKEY < HFI1_KERNEL_MIN_JKEY);
	BUILD_BUG_ON(TID_RDMA_JKEY > HFI1_KERNEL_MAX_JKEY);
	rcd->jkey = TID_RDMA_JKEY;
	hfi1_set_ctxt_jkey(rcd->dd, rcd, rcd->jkey);
	return hfi1_alloc_ctxt_rcv_groups(rcd);
}

/**
 * qp_to_rcd - determine the receive context used by a qp
 * @qp - the qp
 *
 * This routine returns the receive context associated
 * with a a qp's qpn.
 *
 * Returns the context.
 */
static struct hfi1_ctxtdata *qp_to_rcd(struct rvt_dev_info *rdi,
				       struct rvt_qp *qp)
{
	struct hfi1_ibdev *verbs_dev = container_of(rdi,
						    struct hfi1_ibdev,
						    rdi);
	struct hfi1_devdata *dd = container_of(verbs_dev,
					       struct hfi1_devdata,
					       verbs_dev);
	unsigned int ctxt;

	if (qp->ibqp.qp_num == 0)
		ctxt = 0;
	else
		ctxt = ((qp->ibqp.qp_num >> dd->qos_shift) %
			(dd->n_krcv_queues - 1)) + 1;

	return dd->rcd[ctxt];
}

int hfi1_qp_priv_init(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		      struct ib_qp_init_attr *init_attr)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	int i, ret;

	qpriv->rcd = qp_to_rcd(rdi, qp);

	spin_lock_init(&qpriv->opfn.lock);
	INIT_WORK(&qpriv->opfn.opfn_work, opfn_send_conn_request);
	INIT_WORK(&qpriv->tid_rdma.trigger_work, tid_rdma_trigger_resume);
	qpriv->flow_state.psn = 0;
	qpriv->flow_state.index = RXE_NUM_TID_FLOWS;
	qpriv->flow_state.last_index = RXE_NUM_TID_FLOWS;
	qpriv->flow_state.generation = KERN_GENERATION_RESERVED;
	INIT_LIST_HEAD(&qpriv->tid_wait);

	if (init_attr->qp_type == IB_QPT_RC && HFI1_CAP_IS_KSET(TID_RDMA)) {
		struct hfi1_devdata *dd = qpriv->rcd->dd;

		qpriv->pages = kzalloc_node(TID_RDMA_MAX_PAGES *
						sizeof(*qpriv->pages),
					    GFP_KERNEL, dd->node);
		if (!qpriv->pages)
			return -ENOMEM;
		for (i = 0; i < qp->s_size; i++) {
			struct hfi1_swqe_priv *priv;
			struct rvt_swqe *wqe = rvt_get_swqe_ptr(qp, i);

			priv = kzalloc_node(sizeof(*priv), GFP_KERNEL,
					    dd->node);
			if (!priv)
				return -ENOMEM;

			hfi1_init_trdma_req(qp, &priv->tid_req);
			priv->tid_req.e.swqe = wqe;
			wqe->priv = priv;
		}
		for (i = 0; i < rvt_max_atomic(rdi); i++) {
			struct hfi1_ack_priv *priv;

			priv = kzalloc_node(sizeof(*priv), GFP_KERNEL,
					    dd->node);
			if (!priv)
				return -ENOMEM;

			hfi1_init_trdma_req(qp, &priv->tid_req);
			priv->tid_req.e.ack = &qp->s_ack_queue[i];

			ret = hfi1_kern_exp_rcv_alloc_flows(&priv->tid_req,
							    GFP_KERNEL);
			if (ret) {
				kfree(priv);
				return ret;
			}
			qp->s_ack_queue[i].priv = priv;
		}
	}

	return 0;
}

void hfi1_qp_priv_tid_free(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct rvt_swqe *wqe;
	u32 i;

	if (qp->ibqp.qp_type == IB_QPT_RC && HFI1_CAP_IS_KSET(TID_RDMA)) {
		for (i = 0; i < qp->s_size; i++) {
			wqe = rvt_get_swqe_ptr(qp, i);
			kfree(wqe->priv);
			wqe->priv = NULL;
		}
		for (i = 0; i < rvt_max_atomic(rdi); i++) {
			struct hfi1_ack_priv *priv = qp->s_ack_queue[i].priv;

			if (priv)
				hfi1_kern_exp_rcv_free_flows(&priv->tid_req);
			kfree(priv);
			qp->s_ack_queue[i].priv = NULL;
		}
		cancel_work_sync(&qpriv->opfn.opfn_work);
		kfree(qpriv->pages);
		qpriv->pages = NULL;
	}
}

/* Flow and tid waiter functions */
/**
 * DOC: lock ordering
 *
 * There are two locks involved with the queuing
 * routines: the qp s_lock and the exp_lock.
 *
 * Since the tid space allocation is called from
 * the send engine, the qp s_lock is already held.
 *
 * The allocation routines will get the exp_lock.
 *
 * The first_qp() call is provided to allow the head of
 * the rcd wait queue to be fetched under the exp_lock and
 * followed by a drop of the exp_lock.
 *
 * Any qp in the wait list will have the qp reference count held
 * to hold the qp in memory.
 */

/*
 * return head of rcd wait list
 *
 * Must hold the exp_lock.
 *
 * Get a reference to the QP to hold the QP in memory.
 *
 * The caller must release the reference when the local
 * is no longer being used.
 */
static struct rvt_qp *first_qp(struct hfi1_ctxtdata *rcd,
			       struct tid_queue *queue)
	__must_hold(&rcd->exp_lock)
{
	struct hfi1_qp_priv *priv;

	lockdep_assert_held(&rcd->exp_lock);
	priv = list_first_entry_or_null(&queue->queue_head,
					struct hfi1_qp_priv,
					tid_wait);
	if (!priv)
		return NULL;
	rvt_get_qp(priv->owner);
	return priv->owner;
}

/**
 * kernel_tid_waiters - determine rcd wait
 * @rcd: the receive context
 * @qp: the head of the qp being processed
 *
 * This routine will return false IFF
 * the list is NULL or the head of the
 * list is the indicated qp.
 *
 * Must hold the qp s_lock and the exp_lock.
 *
 * Return:
 * false if either of the conditions below are statisfied:
 * 1. The list is empty or
 * 2. The indicated qp is at the head of the list and the
 *    HFI1_S_WAIT_TID_SPACE bit is set in qp->s_flags.
 * true is returned otherwise.
 */
static bool kernel_tid_waiters(struct hfi1_ctxtdata *rcd,
			       struct tid_queue *queue, struct rvt_qp *qp)
	__must_hold(&rcd->exp_lock) __must_hold(&qp->s_lock)
{
	struct rvt_qp *fqp;
	bool ret = true;

	lockdep_assert_held(&qp->s_lock);
	lockdep_assert_held(&rcd->exp_lock);
	fqp = first_qp(rcd, queue);
	if (!fqp || (fqp == qp && (qp->s_flags & HFI1_S_WAIT_TID_SPACE)))
		ret = false;
	rvt_put_qp(fqp);
	return ret;
}

/**
 * dequeue_tid_waiter - dequeue the qp from the list
 * @qp - the qp to remove the wait list
 *
 * This routine removes the indicated qp from the
 * wait list if it is there.
 *
 * This should be done after the hardware flow and
 * tid array resources have been allocated.
 *
 * Must hold the qp s_lock and the rcd exp_lock.
 *
 * It assumes the s_lock to protect the s_flags
 * field and to reliably test the HFI1_S_WAIT_TID_SPACE flag.
 */
static void dequeue_tid_waiter(struct hfi1_ctxtdata *rcd,
			       struct tid_queue *queue, struct rvt_qp *qp)
	__must_hold(&rcd->exp_lock) __must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *priv = qp->priv;

	lockdep_assert_held(&qp->s_lock);
	lockdep_assert_held(&rcd->exp_lock);
	if (list_empty(&priv->tid_wait))
		return;
	list_del_init(&priv->tid_wait);
	qp->s_flags &= ~HFI1_S_WAIT_TID_SPACE;
	queue->dequeue++;
	rvt_put_qp(qp);
}

/**
 * queue_qp_for_tid_wait - suspend QP on tid space
 * @rcd: the receive context
 * @qp: the qp
 *
 * The qp is inserted at the tail of the rcd
 * wait queue and the HFI1_S_WAIT_TID_SPACE s_flag is set.
 *
 * Must hold the qp s_lock and the exp_lock.
 */
static void queue_qp_for_tid_wait(struct hfi1_ctxtdata *rcd,
				  struct tid_queue *queue, struct rvt_qp *qp)
	__must_hold(&rcd->exp_lock) __must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *priv = qp->priv;

	lockdep_assert_held(&qp->s_lock);
	lockdep_assert_held(&rcd->exp_lock);
	if (list_empty(&priv->tid_wait)) {
		qp->s_flags |= HFI1_S_WAIT_TID_SPACE;
		list_add_tail(&priv->tid_wait, &queue->queue_head);
		priv->tid_enqueue = ++queue->enqueue;
		rcd->dd->verbs_dev.n_tidwait++;
		trace_hfi1_qpsleep(qp, HFI1_S_WAIT_TID_SPACE);
		rvt_get_qp(qp);
	}
}

/**
 * __trigger_tid_waiter - trigger tid waiter
 * @qp: the qp
 *
 * This is a private entrance to schedule the qp
 * assuming the caller is holding the qp->s_lock.
 */
static void __trigger_tid_waiter(struct rvt_qp *qp)
	__must_hold(&qp->s_lock)
{
	lockdep_assert_held(&qp->s_lock);
	if (!(qp->s_flags & HFI1_S_WAIT_TID_SPACE))
		return;
	trace_hfi1_qpwakeup(qp, HFI1_S_WAIT_TID_SPACE);
	hfi1_schedule_send(qp);
}

/**
 * tid_rdma_schedule_tid_wakeup - schedule wakeup for a qp
 * @qp - the qp
 *
 * trigger a schedule or a waiting qp in a deadlock
 * safe manner.  The qp reference is held prior
 * to this call via first_qp().
 *
 * If the qp trigger was already scheduled (!rval)
 * the the reference is dropped, otherwise the resume
 * or the destroy cancel will dispatch the reference.
 */
static void tid_rdma_schedule_tid_wakeup(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv;
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	bool rval;

	if (!qp)
		return;

	priv = qp->priv;
	ibp = to_iport(qp->ibqp.device, qp->port_num);
	ppd = ppd_from_ibp(ibp);
	dd = dd_from_ibdev(qp->ibqp.device);

	rval = queue_work_on(priv->s_sde ?
			     priv->s_sde->cpu :
			     cpumask_first(cpumask_of_node(dd->node)),
			     ppd->hfi1_wq,
			     &priv->tid_rdma.trigger_work);
	if (!rval)
		rvt_put_qp(qp);
}

/**
 * tid_rdma_trigger_resume - field a trigger work request
 * @work - the work item
 *
 * Complete the off qp trigger processing by directly
 * calling the progress routine.
 */
static void tid_rdma_trigger_resume(struct work_struct *work)
{
	struct tid_rdma_qp_params *tr;
	struct hfi1_qp_priv *priv;
	struct rvt_qp *qp;

	tr = container_of(work, struct tid_rdma_qp_params, trigger_work);
	priv = container_of(tr, struct hfi1_qp_priv, tid_rdma);
	qp = priv->owner;
	spin_lock_irq(&qp->s_lock);
	if (qp->s_flags & HFI1_S_WAIT_TID_SPACE) {
		spin_unlock_irq(&qp->s_lock);
		hfi1_do_send(priv->owner, true);
	} else {
		spin_unlock_irq(&qp->s_lock);
	}
	rvt_put_qp(qp);
}

/**
 * tid_rdma_flush_wait - unwind any tid space wait
 *
 * This is called when resetting a qp to
 * allow a destroy or reset to get rid
 * of any tid space linkage and reference counts.
 */
static void _tid_rdma_flush_wait(struct rvt_qp *qp, struct tid_queue *queue)
	__must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *priv;

	if (!qp)
		return;
	lockdep_assert_held(&qp->s_lock);
	priv = qp->priv;
	qp->s_flags &= ~HFI1_S_WAIT_TID_SPACE;
	spin_lock(&priv->rcd->exp_lock);
	if (!list_empty(&priv->tid_wait)) {
		list_del_init(&priv->tid_wait);
		qp->s_flags &= ~HFI1_S_WAIT_TID_SPACE;
		queue->dequeue++;
		rvt_put_qp(qp);
	}
	spin_unlock(&priv->rcd->exp_lock);
}

void hfi1_tid_rdma_flush_wait(struct rvt_qp *qp)
	__must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *priv = qp->priv;

	_tid_rdma_flush_wait(qp, &priv->rcd->flow_queue);
	_tid_rdma_flush_wait(qp, &priv->rcd->rarr_queue);
}

/* Flow functions */
/**
 * kern_reserve_flow - allocate a hardware flow
 * @rcd - the context to use for allocation
 * @last - the index of the preferred flow. Use RXE_NUM_TID_FLOWS to
 *         signify "don't care".
 *
 * Use a bit mask based allocation to reserve a hardware
 * flow for use in receiving KDETH data packets. If a preferred flow is
 * specified the function will attempt to reserve that flow again, if
 * available.
 *
 * The exp_lock must be held.
 *
 * Return:
 * On success: a value postive value between 0 and RXE_NUM_TID_FLOWS - 1
 * On failure: -EAGAIN
 */
static int kern_reserve_flow(struct hfi1_ctxtdata *rcd, int last)
	__must_hold(&rcd->exp_lock)
{
	int nr;

	/* Attempt to reserve the preferred flow index */
	if (last >= 0 && last < RXE_NUM_TID_FLOWS &&
	    !test_and_set_bit(last, &rcd->flow_mask))
		return last;

	nr = ffz(rcd->flow_mask);
	BUILD_BUG_ON(RXE_NUM_TID_FLOWS >=
		     (sizeof(rcd->flow_mask) * BITS_PER_BYTE));
	if (nr > (RXE_NUM_TID_FLOWS - 1))
		return -EAGAIN;
	set_bit(nr, &rcd->flow_mask);
	return nr;
}

static void kern_set_hw_flow(struct hfi1_ctxtdata *rcd, u32 generation,
			     u32 flow_idx)
{
	u64 reg;

	reg = ((u64)generation << HFI1_KDETH_BTH_SEQ_SHIFT) |
		RCV_TID_FLOW_TABLE_CTRL_FLOW_VALID_SMASK |
		RCV_TID_FLOW_TABLE_CTRL_KEEP_AFTER_SEQ_ERR_SMASK |
		RCV_TID_FLOW_TABLE_CTRL_KEEP_ON_GEN_ERR_SMASK |
		RCV_TID_FLOW_TABLE_STATUS_SEQ_MISMATCH_SMASK |
		RCV_TID_FLOW_TABLE_STATUS_GEN_MISMATCH_SMASK;

	if (generation != KERN_GENERATION_RESERVED)
		reg |= RCV_TID_FLOW_TABLE_CTRL_HDR_SUPP_EN_SMASK;

	write_uctxt_csr(rcd->dd, rcd->ctxt,
			RCV_TID_FLOW_TABLE + 8 * flow_idx, reg);
}

static u32 kern_setup_hw_flow(struct hfi1_ctxtdata *rcd, u32 flow_idx)
	__must_hold(&rcd->exp_lock)
{
	u32 generation = rcd->flows[flow_idx].generation;

	kern_set_hw_flow(rcd, generation, flow_idx);
	return generation;
}

static u32 kern_flow_generation_next(u32 gen)
{
	u32 generation = mask_generation(gen + 1);

	if (generation == KERN_GENERATION_RESERVED)
		generation = mask_generation(generation + 1);
	return generation;
}

static void kern_clear_hw_flow(struct hfi1_ctxtdata *rcd, u32 flow_idx)
	__must_hold(&rcd->exp_lock)
{
	rcd->flows[flow_idx].generation =
		kern_flow_generation_next(rcd->flows[flow_idx].generation);
	kern_set_hw_flow(rcd, KERN_GENERATION_RESERVED, flow_idx);
}

int hfi1_kern_setup_hw_flow(struct hfi1_ctxtdata *rcd, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = (struct hfi1_qp_priv *)qp->priv;
	struct tid_flow_state *fs = &qpriv->flow_state;
	struct rvt_qp *fqp;
	unsigned long flags;
	int ret = 0;

	/* The QP already has an allocated flow */
	if (fs->index != RXE_NUM_TID_FLOWS)
		return ret;

	spin_lock_irqsave(&rcd->exp_lock, flags);
	if (kernel_tid_waiters(rcd, &rcd->flow_queue, qp))
		goto queue;

	ret = kern_reserve_flow(rcd, fs->last_index);
	if (ret < 0)
		goto queue;
	fs->index = ret;
	fs->last_index = fs->index;

	/* Generation received in a RESYNC overrides default flow generation */
	if (fs->generation != KERN_GENERATION_RESERVED)
		rcd->flows[fs->index].generation = fs->generation;
	fs->generation = kern_setup_hw_flow(rcd, fs->index);
	fs->psn = 0;
	fs->flags = 0;
	dequeue_tid_waiter(rcd, &rcd->flow_queue, qp);
	/* get head before dropping lock */
	fqp = first_qp(rcd, &rcd->flow_queue);
	spin_unlock_irqrestore(&rcd->exp_lock, flags);

	tid_rdma_schedule_tid_wakeup(fqp);
	return 0;
queue:
	queue_qp_for_tid_wait(rcd, &rcd->flow_queue, qp);
	spin_unlock_irqrestore(&rcd->exp_lock, flags);
	return -EAGAIN;
}

void hfi1_kern_clear_hw_flow(struct hfi1_ctxtdata *rcd, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = (struct hfi1_qp_priv *)qp->priv;
	struct tid_flow_state *fs = &qpriv->flow_state;
	struct rvt_qp *fqp;
	unsigned long flags;

	if (fs->index >= RXE_NUM_TID_FLOWS)
		return;
	spin_lock_irqsave(&rcd->exp_lock, flags);
	kern_clear_hw_flow(rcd, fs->index);
	clear_bit(fs->index, &rcd->flow_mask);
	fs->index = RXE_NUM_TID_FLOWS;
	fs->psn = 0;
	fs->generation = KERN_GENERATION_RESERVED;

	/* get head before dropping lock */
	fqp = first_qp(rcd, &rcd->flow_queue);
	spin_unlock_irqrestore(&rcd->exp_lock, flags);

	if (fqp == qp) {
		__trigger_tid_waiter(fqp);
		rvt_put_qp(fqp);
	} else {
		tid_rdma_schedule_tid_wakeup(fqp);
	}
}

void hfi1_kern_init_ctxt_generations(struct hfi1_ctxtdata *rcd)
{
	int i;

	for (i = 0; i < RXE_NUM_TID_FLOWS; i++) {
		rcd->flows[i].generation = mask_generation(prandom_u32());
		kern_set_hw_flow(rcd, KERN_GENERATION_RESERVED, i);
	}
}

/* TID allocation functions */
static u8 trdma_pset_order(struct tid_rdma_pageset *s)
{
	u8 count = s->count;

	return ilog2(count) + 1;
}

/**
 * tid_rdma_find_phys_blocks_4k - get groups base on mr info
 * @npages - number of pages
 * @pages - pointer to an array of page structs
 * @list - page set array to return
 *
 * This routine returns the number of groups associated with
 * the current sge information.  This implementation is based
 * on the expected receive find_phys_blocks() adjusted to
 * use the MR information vs. the pfn.
 *
 * Return:
 * the number of RcvArray entries
 */
static u32 tid_rdma_find_phys_blocks_4k(struct tid_rdma_flow *flow,
					struct page **pages,
					u32 npages,
					struct tid_rdma_pageset *list)
{
	u32 pagecount, pageidx, setcount = 0, i;
	void *vaddr, *this_vaddr;

	if (!npages)
		return 0;

	/*
	 * Look for sets of physically contiguous pages in the user buffer.
	 * This will allow us to optimize Expected RcvArray entry usage by
	 * using the bigger supported sizes.
	 */
	vaddr = page_address(pages[0]);
	trace_hfi1_tid_flow_page(flow->req->qp, flow, 0, 0, 0, vaddr);
	for (pageidx = 0, pagecount = 1, i = 1; i <= npages; i++) {
		this_vaddr = i < npages ? page_address(pages[i]) : NULL;
		trace_hfi1_tid_flow_page(flow->req->qp, flow, i, 0, 0,
					 this_vaddr);
		/*
		 * If the vaddr's are not sequential, pages are not physically
		 * contiguous.
		 */
		if (this_vaddr != (vaddr + PAGE_SIZE)) {
			/*
			 * At this point we have to loop over the set of
			 * physically contiguous pages and break them down it
			 * sizes supported by the HW.
			 * There are two main constraints:
			 *     1. The max buffer size is MAX_EXPECTED_BUFFER.
			 *        If the total set size is bigger than that
			 *        program only a MAX_EXPECTED_BUFFER chunk.
			 *     2. The buffer size has to be a power of two. If
			 *        it is not, round down to the closes power of
			 *        2 and program that size.
			 */
			while (pagecount) {
				int maxpages = pagecount;
				u32 bufsize = pagecount * PAGE_SIZE;

				if (bufsize > MAX_EXPECTED_BUFFER)
					maxpages =
						MAX_EXPECTED_BUFFER >>
						PAGE_SHIFT;
				else if (!is_power_of_2(bufsize))
					maxpages =
						rounddown_pow_of_two(bufsize) >>
						PAGE_SHIFT;

				list[setcount].idx = pageidx;
				list[setcount].count = maxpages;
				trace_hfi1_tid_pageset(flow->req->qp, setcount,
						       list[setcount].idx,
						       list[setcount].count);
				pagecount -= maxpages;
				pageidx += maxpages;
				setcount++;
			}
			pageidx = i;
			pagecount = 1;
			vaddr = this_vaddr;
		} else {
			vaddr += PAGE_SIZE;
			pagecount++;
		}
	}
	/* insure we always return an even number of sets */
	if (setcount & 1)
		list[setcount++].count = 0;
	return setcount;
}

/**
 * tid_flush_pages - dump out pages into pagesets
 * @list - list of pagesets
 * @idx - pointer to current page index
 * @pages - number of pages to dump
 * @sets - current number of pagesset
 *
 * This routine flushes out accumuated pages.
 *
 * To insure an even number of sets the
 * code may add a filler.
 *
 * This can happen with when pages is not
 * a power of 2 or pages is a power of 2
 * less than the maximum pages.
 *
 * Return:
 * The new number of sets
 */

static u32 tid_flush_pages(struct tid_rdma_pageset *list,
			   u32 *idx, u32 pages, u32 sets)
{
	while (pages) {
		u32 maxpages = pages;

		if (maxpages > MAX_EXPECTED_PAGES)
			maxpages = MAX_EXPECTED_PAGES;
		else if (!is_power_of_2(maxpages))
			maxpages = rounddown_pow_of_two(maxpages);
		list[sets].idx = *idx;
		list[sets++].count = maxpages;
		*idx += maxpages;
		pages -= maxpages;
	}
	/* might need a filler */
	if (sets & 1)
		list[sets++].count = 0;
	return sets;
}

/**
 * tid_rdma_find_phys_blocks_8k - get groups base on mr info
 * @pages - pointer to an array of page structs
 * @npages - number of pages
 * @list - page set array to return
 *
 * This routine parses an array of pages to compute pagesets
 * in an 8k compatible way.
 *
 * pages are tested two at a time, i, i + 1 for contiguous
 * pages and i - 1 and i contiguous pages.
 *
 * If any condition is false, any accumlated pages are flushed and
 * v0,v1 are emitted as separate PAGE_SIZE pagesets
 *
 * Otherwise, the current 8k is totaled for a future flush.
 *
 * Return:
 * The number of pagesets
 * list set with the returned number of pagesets
 *
 */
static u32 tid_rdma_find_phys_blocks_8k(struct tid_rdma_flow *flow,
					struct page **pages,
					u32 npages,
					struct tid_rdma_pageset *list)
{
	u32 idx, sets = 0, i;
	u32 pagecnt = 0;
	void *v0, *v1, *vm1;

	if (!npages)
		return 0;
	for (idx = 0, i = 0, vm1 = NULL; i < npages; i += 2) {
		/* get a new v0 */
		v0 = page_address(pages[i]);
		trace_hfi1_tid_flow_page(flow->req->qp, flow, i, 1, 0, v0);
		v1 = i + 1 < npages ?
				page_address(pages[i + 1]) : NULL;
		trace_hfi1_tid_flow_page(flow->req->qp, flow, i, 1, 1, v1);
		/* compare i, i + 1 vaddr */
		if (v1 != (v0 + PAGE_SIZE)) {
			/* flush out pages */
			sets = tid_flush_pages(list, &idx, pagecnt, sets);
			/* output v0,v1 as two pagesets */
			list[sets].idx = idx++;
			list[sets++].count = 1;
			if (v1) {
				list[sets].count = 1;
				list[sets++].idx = idx++;
			} else {
				list[sets++].count = 0;
			}
			vm1 = NULL;
			pagecnt = 0;
			continue;
		}
		/* i,i+1 consecutive, look at i-1,i */
		if (vm1 && v0 != (vm1 + PAGE_SIZE)) {
			/* flush out pages */
			sets = tid_flush_pages(list, &idx, pagecnt, sets);
			pagecnt = 0;
		}
		/* pages will always be a multiple of 8k */
		pagecnt += 2;
		/* save i-1 */
		vm1 = v1;
		/* move to next pair */
	}
	/* dump residual pages at end */
	sets = tid_flush_pages(list, &idx, npages - idx, sets);
	/* by design cannot be odd sets */
	WARN_ON(sets & 1);
	return sets;
}

/**
 * Find pages for one segment of a sge array represented by @ss. The function
 * does not check the sge, the sge must have been checked for alignment with a
 * prior call to hfi1_kern_trdma_ok. Other sge checking is done as part of
 * rvt_lkey_ok and rvt_rkey_ok. Also, the function only modifies the local sge
 * copy maintained in @ss->sge, the original sge is not modified.
 *
 * Unlike IB RDMA WRITE, we can't decrement ss->num_sge here because we are not
 * releasing the MR reference count at the same time. Otherwise, we'll "leak"
 * references to the MR. This difference requires that we keep track of progress
 * into the sg_list. This is done by the cur_seg cursor in the tid_rdma_request
 * structure.
 */
static u32 kern_find_pages(struct tid_rdma_flow *flow,
			   struct page **pages,
			   struct rvt_sge_state *ss, bool *last)
{
	struct tid_rdma_request *req = flow->req;
	struct rvt_sge *sge = &ss->sge;
	u32 length = flow->req->seg_len;
	u32 len = PAGE_SIZE;
	u32 i = 0;

	while (length && req->isge < ss->num_sge) {
		pages[i++] = virt_to_page(sge->vaddr);

		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (!sge->sge_length) {
			if (++req->isge < ss->num_sge)
				*sge = ss->sg_list[req->isge - 1];
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= RVT_SEGSZ) {
				++sge->m;
				sge->n = 0;
			}
			sge->vaddr = sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length = sge->mr->map[sge->m]->segs[sge->n].length;
		}
		length -= len;
	}

	flow->length = flow->req->seg_len - length;
	*last = req->isge == ss->num_sge ? false : true;
	return i;
}

static void dma_unmap_flow(struct tid_rdma_flow *flow)
{
	struct hfi1_devdata *dd;
	int i;
	struct tid_rdma_pageset *pset;

	dd = flow->req->rcd->dd;
	for (i = 0, pset = &flow->pagesets[0]; i < flow->npagesets;
			i++, pset++) {
		if (pset->count && pset->addr) {
			dma_unmap_page(&dd->pcidev->dev,
				       pset->addr,
				       PAGE_SIZE * pset->count,
				       DMA_FROM_DEVICE);
			pset->mapped = 0;
		}
	}
}

static int dma_map_flow(struct tid_rdma_flow *flow, struct page **pages)
{
	int i;
	struct hfi1_devdata *dd = flow->req->rcd->dd;
	struct tid_rdma_pageset *pset;

	for (i = 0, pset = &flow->pagesets[0]; i < flow->npagesets;
			i++, pset++) {
		if (pset->count) {
			pset->addr = dma_map_page(&dd->pcidev->dev,
						  pages[pset->idx],
						  0,
						  PAGE_SIZE * pset->count,
						  DMA_FROM_DEVICE);

			if (dma_mapping_error(&dd->pcidev->dev, pset->addr)) {
				dma_unmap_flow(flow);
				return -ENOMEM;
			}
			pset->mapped = 1;
		}
	}
	return 0;
}

static inline bool dma_mapped(struct tid_rdma_flow *flow)
{
	return !!flow->pagesets[0].mapped;
}

/*
 * Get pages pointers and identify contiguous physical memory chunks for a
 * segment. All segments are of length flow->req->seg_len.
 */
static int kern_get_phys_blocks(struct tid_rdma_flow *flow,
				struct page **pages,
				struct rvt_sge_state *ss, bool *last)
{
	u8 npages;

	/* Reuse previously computed pagesets, if any */
	if (flow->npagesets) {
		trace_hfi1_tid_flow_alloc(flow->req->qp, flow->req->setup_head,
					  flow);
		if (!dma_mapped(flow))
			return dma_map_flow(flow, pages);
		return 0;
	}

	npages = kern_find_pages(flow, pages, ss, last);

	if (flow->req->qp->pmtu == enum_to_mtu(OPA_MTU_4096))
		flow->npagesets =
			tid_rdma_find_phys_blocks_4k(flow, pages, npages,
						     flow->pagesets);
	else
		flow->npagesets =
			tid_rdma_find_phys_blocks_8k(flow, pages, npages,
						     flow->pagesets);

	return dma_map_flow(flow, pages);
}

static inline void kern_add_tid_node(struct tid_rdma_flow *flow,
				     struct hfi1_ctxtdata *rcd, char *s,
				     struct tid_group *grp, u8 cnt)
{
	struct kern_tid_node *node = &flow->tnode[flow->tnode_cnt++];

	WARN_ON_ONCE(flow->tnode_cnt >=
		     (TID_RDMA_MAX_SEGMENT_SIZE >> PAGE_SHIFT));
	if (WARN_ON_ONCE(cnt & 1))
		dd_dev_err(rcd->dd,
			   "unexpected odd allocation cnt %u map 0x%x used %u",
			   cnt, grp->map, grp->used);

	node->grp = grp;
	node->map = grp->map;
	node->cnt = cnt;
	trace_hfi1_tid_node_add(flow->req->qp, s, flow->tnode_cnt - 1,
				grp->base, grp->map, grp->used, cnt);
}

/*
 * Try to allocate pageset_count TID's from TID groups for a context
 *
 * This function allocates TID's without moving groups between lists or
 * modifying grp->map. This is done as follows, being cogizant of the lists
 * between which the TID groups will move:
 * 1. First allocate complete groups of 8 TID's since this is more efficient,
 *    these groups will move from group->full without affecting used
 * 2. If more TID's are needed allocate from used (will move from used->full or
 *    stay in used)
 * 3. If we still don't have the required number of TID's go back and look again
 *    at a complete group (will move from group->used)
 */
static int kern_alloc_tids(struct tid_rdma_flow *flow)
{
	struct hfi1_ctxtdata *rcd = flow->req->rcd;
	struct hfi1_devdata *dd = rcd->dd;
	u32 ngroups, pageidx = 0;
	struct tid_group *group = NULL, *used;
	u8 use;

	flow->tnode_cnt = 0;
	ngroups = flow->npagesets / dd->rcv_entries.group_size;
	if (!ngroups)
		goto used_list;

	/* First look at complete groups */
	list_for_each_entry(group,  &rcd->tid_group_list.list, list) {
		kern_add_tid_node(flow, rcd, "complete groups", group,
				  group->size);

		pageidx += group->size;
		if (!--ngroups)
			break;
	}

	if (pageidx >= flow->npagesets)
		goto ok;

used_list:
	/* Now look at partially used groups */
	list_for_each_entry(used, &rcd->tid_used_list.list, list) {
		use = min_t(u32, flow->npagesets - pageidx,
			    used->size - used->used);
		kern_add_tid_node(flow, rcd, "used groups", used, use);

		pageidx += use;
		if (pageidx >= flow->npagesets)
			goto ok;
	}

	/*
	 * Look again at a complete group, continuing from where we left.
	 * However, if we are at the head, we have reached the end of the
	 * complete groups list from the first loop above
	 */
	if (group && &group->list == &rcd->tid_group_list.list)
		goto bail_eagain;
	group = list_prepare_entry(group, &rcd->tid_group_list.list,
				   list);
	if (list_is_last(&group->list, &rcd->tid_group_list.list))
		goto bail_eagain;
	group = list_next_entry(group, list);
	use = min_t(u32, flow->npagesets - pageidx, group->size);
	kern_add_tid_node(flow, rcd, "complete continue", group, use);
	pageidx += use;
	if (pageidx >= flow->npagesets)
		goto ok;
bail_eagain:
	trace_hfi1_msg_alloc_tids(flow->req->qp, " insufficient tids: needed ",
				  (u64)flow->npagesets);
	return -EAGAIN;
ok:
	return 0;
}

static void kern_program_rcv_group(struct tid_rdma_flow *flow, int grp_num,
				   u32 *pset_idx)
{
	struct hfi1_ctxtdata *rcd = flow->req->rcd;
	struct hfi1_devdata *dd = rcd->dd;
	struct kern_tid_node *node = &flow->tnode[grp_num];
	struct tid_group *grp = node->grp;
	struct tid_rdma_pageset *pset;
	u32 pmtu_pg = flow->req->qp->pmtu >> PAGE_SHIFT;
	u32 rcventry, npages = 0, pair = 0, tidctrl;
	u8 i, cnt = 0;

	for (i = 0; i < grp->size; i++) {
		rcventry = grp->base + i;

		if (node->map & BIT(i) || cnt >= node->cnt) {
			rcv_array_wc_fill(dd, rcventry);
			continue;
		}
		pset = &flow->pagesets[(*pset_idx)++];
		if (pset->count) {
			hfi1_put_tid(dd, rcventry, PT_EXPECTED,
				     pset->addr, trdma_pset_order(pset));
		} else {
			hfi1_put_tid(dd, rcventry, PT_INVALID, 0, 0);
		}
		npages += pset->count;

		rcventry -= rcd->expected_base;
		tidctrl = pair ? 0x3 : rcventry & 0x1 ? 0x2 : 0x1;
		/*
		 * A single TID entry will be used to use a rcvarr pair (with
		 * tidctrl 0x3), if ALL these are true (a) the bit pos is even
		 * (b) the group map shows current and the next bits as free
		 * indicating two consecutive rcvarry entries are available (c)
		 * we actually need 2 more entries
		 */
		pair = !(i & 0x1) && !((node->map >> i) & 0x3) &&
			node->cnt >= cnt + 2;
		if (!pair) {
			if (!pset->count)
				tidctrl = 0x1;
			flow->tid_entry[flow->tidcnt++] =
				EXP_TID_SET(IDX, rcventry >> 1) |
				EXP_TID_SET(CTRL, tidctrl) |
				EXP_TID_SET(LEN, npages);
			trace_hfi1_tid_entry_alloc(/* entry */
			   flow->req->qp, flow->tidcnt - 1,
			   flow->tid_entry[flow->tidcnt - 1]);

			/* Efficient DIV_ROUND_UP(npages, pmtu_pg) */
			flow->npkts += (npages + pmtu_pg - 1) >> ilog2(pmtu_pg);
			npages = 0;
		}

		if (grp->used == grp->size - 1)
			tid_group_move(grp, &rcd->tid_used_list,
				       &rcd->tid_full_list);
		else if (!grp->used)
			tid_group_move(grp, &rcd->tid_group_list,
				       &rcd->tid_used_list);

		grp->used++;
		grp->map |= BIT(i);
		cnt++;
	}
}

static void kern_unprogram_rcv_group(struct tid_rdma_flow *flow, int grp_num)
{
	struct hfi1_ctxtdata *rcd = flow->req->rcd;
	struct hfi1_devdata *dd = rcd->dd;
	struct kern_tid_node *node = &flow->tnode[grp_num];
	struct tid_group *grp = node->grp;
	u32 rcventry;
	u8 i, cnt = 0;

	for (i = 0; i < grp->size; i++) {
		rcventry = grp->base + i;

		if (node->map & BIT(i) || cnt >= node->cnt) {
			rcv_array_wc_fill(dd, rcventry);
			continue;
		}

		hfi1_put_tid(dd, rcventry, PT_INVALID, 0, 0);

		grp->used--;
		grp->map &= ~BIT(i);
		cnt++;

		if (grp->used == grp->size - 1)
			tid_group_move(grp, &rcd->tid_full_list,
				       &rcd->tid_used_list);
		else if (!grp->used)
			tid_group_move(grp, &rcd->tid_used_list,
				       &rcd->tid_group_list);
	}
	if (WARN_ON_ONCE(cnt & 1)) {
		struct hfi1_ctxtdata *rcd = flow->req->rcd;
		struct hfi1_devdata *dd = rcd->dd;

		dd_dev_err(dd, "unexpected odd free cnt %u map 0x%x used %u",
			   cnt, grp->map, grp->used);
	}
}

static void kern_program_rcvarray(struct tid_rdma_flow *flow)
{
	u32 pset_idx = 0;
	int i;

	flow->npkts = 0;
	flow->tidcnt = 0;
	for (i = 0; i < flow->tnode_cnt; i++)
		kern_program_rcv_group(flow, i, &pset_idx);
	trace_hfi1_tid_flow_alloc(flow->req->qp, flow->req->setup_head, flow);
}

/**
 * hfi1_kern_exp_rcv_setup() - setup TID's and flow for one segment of a
 * TID RDMA request
 *
 * @req: TID RDMA request for which the segment/flow is being set up
 * @ss: sge state, maintains state across successive segments of a sge
 * @last: set to true after the last sge segment has been processed
 *
 * This function
 * (1) finds a free flow entry in the flow circular buffer
 * (2) finds pages and continuous physical chunks constituing one segment
 *     of an sge
 * (3) allocates TID group entries for those chunks
 * (4) programs rcvarray entries in the hardware corresponding to those
 *     TID's
 * (5) computes a tidarray with formatted TID entries which can be sent
 *     to the sender
 * (6) Reserves and programs HW flows.
 * (7) It also manages queing the QP when TID/flow resources are not
 *     available.
 *
 * @req points to struct tid_rdma_request of which the segments are a part. The
 * function uses qp, rcd and seg_len members of @req. In the absence of errors,
 * req->flow_idx is the index of the flow which has been prepared in this
 * invocation of function call. With flow = &req->flows[req->flow_idx],
 * flow->tid_entry contains the TID array which the sender can use for TID RDMA
 * sends and flow->npkts contains number of packets required to send the
 * segment.
 *
 * hfi1_check_sge_align should be called prior to calling this function and if
 * it signals error TID RDMA cannot be used for this sge and this function
 * should not be called.
 *
 * For the queuing, caller must hold the flow->req->qp s_lock from the send
 * engine and the function will procure the exp_lock.
 *
 * Return:
 * The function returns -EAGAIN if sufficient number of TID/flow resources to
 * map the segment could not be allocated. In this case the function should be
 * called again with previous arguments to retry the TID allocation. There are
 * no other error returns. The function returns 0 on success.
 */
int hfi1_kern_exp_rcv_setup(struct tid_rdma_request *req,
			    struct rvt_sge_state *ss, bool *last)
	__must_hold(&req->qp->s_lock)
{
	struct tid_rdma_flow *flow = &req->flows[req->setup_head];
	struct hfi1_ctxtdata *rcd = req->rcd;
	struct hfi1_qp_priv *qpriv = req->qp->priv;
	unsigned long flags;
	struct rvt_qp *fqp;
	u16 clear_tail = req->clear_tail;

	lockdep_assert_held(&req->qp->s_lock);
	/*
	 * We return error if either (a) we don't have space in the flow
	 * circular buffer, or (b) we already have max entries in the buffer.
	 * Max entries depend on the type of request we are processing and the
	 * negotiated TID RDMA parameters.
	 */
	if (!CIRC_SPACE(req->setup_head, clear_tail, MAX_FLOWS) ||
	    CIRC_CNT(req->setup_head, clear_tail, MAX_FLOWS) >=
	    req->n_flows)
		return -EINVAL;

	/*
	 * Get pages, identify contiguous physical memory chunks for the segment
	 * If we can not determine a DMA address mapping we will treat it just
	 * like if we ran out of space above.
	 */
	if (kern_get_phys_blocks(flow, qpriv->pages, ss, last)) {
		hfi1_wait_kmem(flow->req->qp);
		return -ENOMEM;
	}

	spin_lock_irqsave(&rcd->exp_lock, flags);
	if (kernel_tid_waiters(rcd, &rcd->rarr_queue, flow->req->qp))
		goto queue;

	/*
	 * At this point we know the number of pagesets and hence the number of
	 * TID's to map the segment. Allocate the TID's from the TID groups. If
	 * we cannot allocate the required number we exit and try again later
	 */
	if (kern_alloc_tids(flow))
		goto queue;
	/*
	 * Finally program the TID entries with the pagesets, compute the
	 * tidarray and enable the HW flow
	 */
	kern_program_rcvarray(flow);

	/*
	 * Setup the flow state with relevant information.
	 * This information is used for tracking the sequence of data packets
	 * for the segment.
	 * The flow is setup here as this is the most accurate time and place
	 * to do so. Doing at a later time runs the risk of the flow data in
	 * qpriv getting out of sync.
	 */
	memset(&flow->flow_state, 0x0, sizeof(flow->flow_state));
	flow->idx = qpriv->flow_state.index;
	flow->flow_state.generation = qpriv->flow_state.generation;
	flow->flow_state.spsn = qpriv->flow_state.psn;
	flow->flow_state.lpsn = flow->flow_state.spsn + flow->npkts - 1;
	flow->flow_state.r_next_psn =
		full_flow_psn(flow, flow->flow_state.spsn);
	qpriv->flow_state.psn += flow->npkts;

	dequeue_tid_waiter(rcd, &rcd->rarr_queue, flow->req->qp);
	/* get head before dropping lock */
	fqp = first_qp(rcd, &rcd->rarr_queue);
	spin_unlock_irqrestore(&rcd->exp_lock, flags);
	tid_rdma_schedule_tid_wakeup(fqp);

	req->setup_head = (req->setup_head + 1) & (MAX_FLOWS - 1);
	return 0;
queue:
	queue_qp_for_tid_wait(rcd, &rcd->rarr_queue, flow->req->qp);
	spin_unlock_irqrestore(&rcd->exp_lock, flags);
	return -EAGAIN;
}

static void hfi1_tid_rdma_reset_flow(struct tid_rdma_flow *flow)
{
	flow->npagesets = 0;
}

/*
 * This function is called after one segment has been successfully sent to
 * release the flow and TID HW/SW resources for that segment. The segments for a
 * TID RDMA request are setup and cleared in FIFO order which is managed using a
 * circular buffer.
 */
int hfi1_kern_exp_rcv_clear(struct tid_rdma_request *req)
	__must_hold(&req->qp->s_lock)
{
	struct tid_rdma_flow *flow = &req->flows[req->clear_tail];
	struct hfi1_ctxtdata *rcd = req->rcd;
	unsigned long flags;
	int i;
	struct rvt_qp *fqp;

	lockdep_assert_held(&req->qp->s_lock);
	/* Exit if we have nothing in the flow circular buffer */
	if (!CIRC_CNT(req->setup_head, req->clear_tail, MAX_FLOWS))
		return -EINVAL;

	spin_lock_irqsave(&rcd->exp_lock, flags);

	for (i = 0; i < flow->tnode_cnt; i++)
		kern_unprogram_rcv_group(flow, i);
	/* To prevent double unprogramming */
	flow->tnode_cnt = 0;
	/* get head before dropping lock */
	fqp = first_qp(rcd, &rcd->rarr_queue);
	spin_unlock_irqrestore(&rcd->exp_lock, flags);

	dma_unmap_flow(flow);

	hfi1_tid_rdma_reset_flow(flow);
	req->clear_tail = (req->clear_tail + 1) & (MAX_FLOWS - 1);

	if (fqp == req->qp) {
		__trigger_tid_waiter(fqp);
		rvt_put_qp(fqp);
	} else {
		tid_rdma_schedule_tid_wakeup(fqp);
	}

	return 0;
}

/*
 * This function is called to release all the tid entries for
 * a request.
 */
void hfi1_kern_exp_rcv_clear_all(struct tid_rdma_request *req)
	__must_hold(&req->qp->s_lock)
{
	/* Use memory barrier for proper ordering */
	while (CIRC_CNT(req->setup_head, req->clear_tail, MAX_FLOWS)) {
		if (hfi1_kern_exp_rcv_clear(req))
			break;
	}
}

/**
 * hfi1_kern_exp_rcv_free_flows - free priviously allocated flow information
 * @req - the tid rdma request to be cleaned
 */
static void hfi1_kern_exp_rcv_free_flows(struct tid_rdma_request *req)
{
	kfree(req->flows);
	req->flows = NULL;
}

/**
 * __trdma_clean_swqe - clean up for large sized QPs
 * @qp: the queue patch
 * @wqe: the send wqe
 */
void __trdma_clean_swqe(struct rvt_qp *qp, struct rvt_swqe *wqe)
{
	struct hfi1_swqe_priv *p = wqe->priv;

	hfi1_kern_exp_rcv_free_flows(&p->tid_req);
}

/*
 * This can be called at QP create time or in the data path.
 */
static int hfi1_kern_exp_rcv_alloc_flows(struct tid_rdma_request *req,
					 gfp_t gfp)
{
	struct tid_rdma_flow *flows;
	int i;

	if (likely(req->flows))
		return 0;
	flows = kmalloc_node(MAX_FLOWS * sizeof(*flows), gfp,
			     req->rcd->numa_id);
	if (!flows)
		return -ENOMEM;
	/* mini init */
	for (i = 0; i < MAX_FLOWS; i++) {
		flows[i].req = req;
		flows[i].npagesets = 0;
		flows[i].pagesets[0].mapped =  0;
	}
	req->flows = flows;
	return 0;
}

static void hfi1_init_trdma_req(struct rvt_qp *qp,
				struct tid_rdma_request *req)
{
	struct hfi1_qp_priv *qpriv = qp->priv;

	/*
	 * Initialize various TID RDMA request variables.
	 * These variables are "static", which is why they
	 * can be pre-initialized here before the WRs has
	 * even been submitted.
	 * However, non-NULL values for these variables do not
	 * imply that this WQE has been enabled for TID RDMA.
	 * Drivers should check the WQE's opcode to determine
	 * if a request is a TID RDMA one or not.
	 */
	req->qp = qp;
	req->rcd = qpriv->rcd;
}

u64 hfi1_access_sw_tid_wait(const struct cntr_entry *entry,
			    void *context, int vl, int mode, u64 data)
{
	struct hfi1_devdata *dd = context;

	return dd->verbs_dev.n_tidwait;
}

static struct tid_rdma_flow *find_flow_ib(struct tid_rdma_request *req,
					  u32 psn, u16 *fidx)
{
	u16 head, tail;
	struct tid_rdma_flow *flow;

	head = req->setup_head;
	tail = req->clear_tail;
	for ( ; CIRC_CNT(head, tail, MAX_FLOWS);
	     tail = CIRC_NEXT(tail, MAX_FLOWS)) {
		flow = &req->flows[tail];
		if (cmp_psn(psn, flow->flow_state.ib_spsn) >= 0 &&
		    cmp_psn(psn, flow->flow_state.ib_lpsn) <= 0) {
			if (fidx)
				*fidx = tail;
			return flow;
		}
	}
	return NULL;
}

static struct tid_rdma_flow *
__find_flow_ranged(struct tid_rdma_request *req, u16 head, u16 tail,
		   u32 psn, u16 *fidx)
{
	for ( ; CIRC_CNT(head, tail, MAX_FLOWS);
	      tail = CIRC_NEXT(tail, MAX_FLOWS)) {
		struct tid_rdma_flow *flow = &req->flows[tail];
		u32 spsn, lpsn;

		spsn = full_flow_psn(flow, flow->flow_state.spsn);
		lpsn = full_flow_psn(flow, flow->flow_state.lpsn);

		if (cmp_psn(psn, spsn) >= 0 && cmp_psn(psn, lpsn) <= 0) {
			if (fidx)
				*fidx = tail;
			return flow;
		}
	}
	return NULL;
}

static struct tid_rdma_flow *find_flow(struct tid_rdma_request *req,
				       u32 psn, u16 *fidx)
{
	return __find_flow_ranged(req, req->setup_head, req->clear_tail, psn,
				  fidx);
}

/* TID RDMA READ functions */
u32 hfi1_build_tid_rdma_read_packet(struct rvt_swqe *wqe,
				    struct ib_other_headers *ohdr, u32 *bth1,
				    u32 *bth2, u32 *len)
{
	struct tid_rdma_request *req = wqe_to_tid_req(wqe);
	struct tid_rdma_flow *flow = &req->flows[req->flow_idx];
	struct rvt_qp *qp = req->qp;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct hfi1_swqe_priv *wpriv = wqe->priv;
	struct tid_rdma_read_req *rreq = &ohdr->u.tid_rdma.r_req;
	struct tid_rdma_params *remote;
	u32 req_len = 0;
	void *req_addr = NULL;

	/* This is the IB psn used to send the request */
	*bth2 = mask_psn(flow->flow_state.ib_spsn + flow->pkt);
	trace_hfi1_tid_flow_build_read_pkt(qp, req->flow_idx, flow);

	/* TID Entries for TID RDMA READ payload */
	req_addr = &flow->tid_entry[flow->tid_idx];
	req_len = sizeof(*flow->tid_entry) *
			(flow->tidcnt - flow->tid_idx);

	memset(&ohdr->u.tid_rdma.r_req, 0, sizeof(ohdr->u.tid_rdma.r_req));
	wpriv->ss.sge.vaddr = req_addr;
	wpriv->ss.sge.sge_length = req_len;
	wpriv->ss.sge.length = wpriv->ss.sge.sge_length;
	/*
	 * We can safely zero these out. Since the first SGE covers the
	 * entire packet, nothing else should even look at the MR.
	 */
	wpriv->ss.sge.mr = NULL;
	wpriv->ss.sge.m = 0;
	wpriv->ss.sge.n = 0;

	wpriv->ss.sg_list = NULL;
	wpriv->ss.total_len = wpriv->ss.sge.sge_length;
	wpriv->ss.num_sge = 1;

	/* Construct the TID RDMA READ REQ packet header */
	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);

	KDETH_RESET(rreq->kdeth0, KVER, 0x1);
	KDETH_RESET(rreq->kdeth1, JKEY, remote->jkey);
	rreq->reth.vaddr = cpu_to_be64(wqe->rdma_wr.remote_addr +
			   req->cur_seg * req->seg_len + flow->sent);
	rreq->reth.rkey = cpu_to_be32(wqe->rdma_wr.rkey);
	rreq->reth.length = cpu_to_be32(*len);
	rreq->tid_flow_psn =
		cpu_to_be32((flow->flow_state.generation <<
			     HFI1_KDETH_BTH_SEQ_SHIFT) |
			    ((flow->flow_state.spsn + flow->pkt) &
			     HFI1_KDETH_BTH_SEQ_MASK));
	rreq->tid_flow_qp =
		cpu_to_be32(qpriv->tid_rdma.local.qp |
			    ((flow->idx & TID_RDMA_DESTQP_FLOW_MASK) <<
			     TID_RDMA_DESTQP_FLOW_SHIFT) |
			    qpriv->rcd->ctxt);
	rreq->verbs_qp = cpu_to_be32(qp->remote_qpn);
	*bth1 &= ~RVT_QPN_MASK;
	*bth1 |= remote->qp;
	*bth2 |= IB_BTH_REQ_ACK;
	rcu_read_unlock();

	/* We are done with this segment */
	flow->sent += *len;
	req->cur_seg++;
	qp->s_state = TID_OP(READ_REQ);
	req->ack_pending++;
	req->flow_idx = (req->flow_idx + 1) & (MAX_FLOWS - 1);
	qpriv->pending_tid_r_segs++;
	qp->s_num_rd_atomic++;

	/* Set the TID RDMA READ request payload size */
	*len = req_len;

	return sizeof(ohdr->u.tid_rdma.r_req) / sizeof(u32);
}

/*
 * @len: contains the data length to read upon entry and the read request
 *       payload length upon exit.
 */
u32 hfi1_build_tid_rdma_read_req(struct rvt_qp *qp, struct rvt_swqe *wqe,
				 struct ib_other_headers *ohdr, u32 *bth1,
				 u32 *bth2, u32 *len)
	__must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_rdma_request *req = wqe_to_tid_req(wqe);
	struct tid_rdma_flow *flow = NULL;
	u32 hdwords = 0;
	bool last;
	bool retry = true;
	u32 npkts = rvt_div_round_up_mtu(qp, *len);

	trace_hfi1_tid_req_build_read_req(qp, 0, wqe->wr.opcode, wqe->psn,
					  wqe->lpsn, req);
	/*
	 * Check sync conditions. Make sure that there are no pending
	 * segments before freeing the flow.
	 */
sync_check:
	if (req->state == TID_REQUEST_SYNC) {
		if (qpriv->pending_tid_r_segs)
			goto done;

		hfi1_kern_clear_hw_flow(req->rcd, qp);
		req->state = TID_REQUEST_ACTIVE;
	}

	/*
	 * If the request for this segment is resent, the tid resources should
	 * have been allocated before. In this case, req->flow_idx should
	 * fall behind req->setup_head.
	 */
	if (req->flow_idx == req->setup_head) {
		retry = false;
		if (req->state == TID_REQUEST_RESEND) {
			/*
			 * This is the first new segment for a request whose
			 * earlier segments have been re-sent. We need to
			 * set up the sge pointer correctly.
			 */
			restart_sge(&qp->s_sge, wqe, req->s_next_psn,
				    qp->pmtu);
			req->isge = 0;
			req->state = TID_REQUEST_ACTIVE;
		}

		/*
		 * Check sync. The last PSN of each generation is reserved for
		 * RESYNC.
		 */
		if ((qpriv->flow_state.psn + npkts) > MAX_TID_FLOW_PSN - 1) {
			req->state = TID_REQUEST_SYNC;
			goto sync_check;
		}

		/* Allocate the flow if not yet */
		if (hfi1_kern_setup_hw_flow(qpriv->rcd, qp))
			goto done;

		/*
		 * The following call will advance req->setup_head after
		 * allocating the tid entries.
		 */
		if (hfi1_kern_exp_rcv_setup(req, &qp->s_sge, &last)) {
			req->state = TID_REQUEST_QUEUED;

			/*
			 * We don't have resources for this segment. The QP has
			 * already been queued.
			 */
			goto done;
		}
	}

	/* req->flow_idx should only be one slot behind req->setup_head */
	flow = &req->flows[req->flow_idx];
	flow->pkt = 0;
	flow->tid_idx = 0;
	flow->sent = 0;
	if (!retry) {
		/* Set the first and last IB PSN for the flow in use.*/
		flow->flow_state.ib_spsn = req->s_next_psn;
		flow->flow_state.ib_lpsn =
			flow->flow_state.ib_spsn + flow->npkts - 1;
	}

	/* Calculate the next segment start psn.*/
	req->s_next_psn += flow->npkts;

	/* Build the packet header */
	hdwords = hfi1_build_tid_rdma_read_packet(wqe, ohdr, bth1, bth2, len);
done:
	return hdwords;
}

/*
 * Validate and accept the TID RDMA READ request parameters.
 * Return 0 if the request is accepted successfully;
 * Return 1 otherwise.
 */
static int tid_rdma_rcv_read_request(struct rvt_qp *qp,
				     struct rvt_ack_entry *e,
				     struct hfi1_packet *packet,
				     struct ib_other_headers *ohdr,
				     u32 bth0, u32 psn, u64 vaddr, u32 len)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	u32 flow_psn, i, tidlen = 0, pktlen, tlen;

	req = ack_to_tid_req(e);

	/* Validate the payload first */
	flow = &req->flows[req->setup_head];

	/* payload length = packet length - (header length + ICRC length) */
	pktlen = packet->tlen - (packet->hlen + 4);
	if (pktlen > sizeof(flow->tid_entry))
		return 1;
	memcpy(flow->tid_entry, packet->ebuf, pktlen);
	flow->tidcnt = pktlen / sizeof(*flow->tid_entry);

	/*
	 * Walk the TID_ENTRY list to make sure we have enough space for a
	 * complete segment. Also calculate the number of required packets.
	 */
	flow->npkts = rvt_div_round_up_mtu(qp, len);
	for (i = 0; i < flow->tidcnt; i++) {
		trace_hfi1_tid_entry_rcv_read_req(qp, i,
						  flow->tid_entry[i]);
		tlen = EXP_TID_GET(flow->tid_entry[i], LEN);
		if (!tlen)
			return 1;

		/*
		 * For tid pair (tidctr == 3), the buffer size of the pair
		 * should be the sum of the buffer size described by each
		 * tid entry. However, only the first entry needs to be
		 * specified in the request (see WFR HAS Section 8.5.7.1).
		 */
		tidlen += tlen;
	}
	if (tidlen * PAGE_SIZE < len)
		return 1;

	/* Empty the flow array */
	req->clear_tail = req->setup_head;
	flow->pkt = 0;
	flow->tid_idx = 0;
	flow->tid_offset = 0;
	flow->sent = 0;
	flow->tid_qpn = be32_to_cpu(ohdr->u.tid_rdma.r_req.tid_flow_qp);
	flow->idx = (flow->tid_qpn >> TID_RDMA_DESTQP_FLOW_SHIFT) &
		    TID_RDMA_DESTQP_FLOW_MASK;
	flow_psn = mask_psn(be32_to_cpu(ohdr->u.tid_rdma.r_req.tid_flow_psn));
	flow->flow_state.generation = flow_psn >> HFI1_KDETH_BTH_SEQ_SHIFT;
	flow->flow_state.spsn = flow_psn & HFI1_KDETH_BTH_SEQ_MASK;
	flow->length = len;

	flow->flow_state.lpsn = flow->flow_state.spsn +
		flow->npkts - 1;
	flow->flow_state.ib_spsn = psn;
	flow->flow_state.ib_lpsn = flow->flow_state.ib_spsn + flow->npkts - 1;

	trace_hfi1_tid_flow_rcv_read_req(qp, req->setup_head, flow);
	/* Set the initial flow index to the current flow. */
	req->flow_idx = req->setup_head;

	/* advance circular buffer head */
	req->setup_head = (req->setup_head + 1) & (MAX_FLOWS - 1);

	/*
	 * Compute last PSN for request.
	 */
	e->opcode = (bth0 >> 24) & 0xff;
	e->psn = psn;
	e->lpsn = psn + flow->npkts - 1;
	e->sent = 0;

	req->n_flows = qpriv->tid_rdma.local.max_read;
	req->state = TID_REQUEST_ACTIVE;
	req->cur_seg = 0;
	req->comp_seg = 0;
	req->ack_seg = 0;
	req->isge = 0;
	req->seg_len = qpriv->tid_rdma.local.max_len;
	req->total_len = len;
	req->total_segs = 1;
	req->r_flow_psn = e->psn;

	trace_hfi1_tid_req_rcv_read_req(qp, 0, e->opcode, e->psn, e->lpsn,
					req);
	return 0;
}

static int tid_rdma_rcv_error(struct hfi1_packet *packet,
			      struct ib_other_headers *ohdr,
			      struct rvt_qp *qp, u32 psn, int diff)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct hfi1_ctxtdata *rcd = ((struct hfi1_qp_priv *)qp->priv)->rcd;
	struct rvt_ack_entry *e;
	struct tid_rdma_request *req;
	unsigned long flags;
	u8 prev;
	bool old_req;

	trace_hfi1_rsp_tid_rcv_error(qp, psn);
	trace_hfi1_tid_rdma_rcv_err(qp, 0, psn, diff);
	if (diff > 0) {
		/* sequence error */
		if (!qp->r_nak_state) {
			ibp->rvp.n_rc_seqnak++;
			qp->r_nak_state = IB_NAK_PSN_ERROR;
			qp->r_ack_psn = qp->r_psn;
			rc_defered_ack(rcd, qp);
		}
		goto done;
	}

	ibp->rvp.n_rc_dupreq++;

	spin_lock_irqsave(&qp->s_lock, flags);
	e = find_prev_entry(qp, psn, &prev, NULL, &old_req);
	if (!e || e->opcode != TID_OP(READ_REQ))
		goto unlock;

	req = ack_to_tid_req(e);
	req->r_flow_psn = psn;
	trace_hfi1_tid_req_rcv_err(qp, 0, e->opcode, e->psn, e->lpsn, req);
	if (e->opcode == TID_OP(READ_REQ)) {
		struct ib_reth *reth;
		u32 offset;
		u32 len;
		u32 rkey;
		u64 vaddr;
		int ok;
		u32 bth0;

		reth = &ohdr->u.tid_rdma.r_req.reth;
		/*
		 * The requester always restarts from the start of the original
		 * request.
		 */
		offset = delta_psn(psn, e->psn) * qp->pmtu;
		len = be32_to_cpu(reth->length);
		if (psn != e->psn || len != req->total_len)
			goto unlock;

		if (e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}

		rkey = be32_to_cpu(reth->rkey);
		vaddr = get_ib_reth_vaddr(reth);

		qp->r_len = len;
		ok = rvt_rkey_ok(qp, &e->rdma_sge, len, vaddr, rkey,
				 IB_ACCESS_REMOTE_READ);
		if (unlikely(!ok))
			goto unlock;

		/*
		 * If all the response packets for the current request have
		 * been sent out and this request is complete (old_request
		 * == false) and the TID flow may be unusable (the
		 * req->clear_tail is advanced). However, when an earlier
		 * request is received, this request will not be complete any
		 * more (qp->s_tail_ack_queue is moved back, see below).
		 * Consequently, we need to update the TID flow info everytime
		 * a duplicate request is received.
		 */
		bth0 = be32_to_cpu(ohdr->bth[0]);
		if (tid_rdma_rcv_read_request(qp, e, packet, ohdr, bth0, psn,
					      vaddr, len))
			goto unlock;

		/*
		 * True if the request is already scheduled (between
		 * qp->s_tail_ack_queue and qp->r_head_ack_queue);
		 */
		if (old_req)
			goto unlock;
	}
	/* Re-process old requests.*/
	if (qp->s_acked_ack_queue == qp->s_tail_ack_queue)
		qp->s_acked_ack_queue = prev;
	qp->s_tail_ack_queue = prev;
	/*
	 * Since the qp->s_tail_ack_queue is modified, the
	 * qp->s_ack_state must be changed to re-initialize
	 * qp->s_ack_rdma_sge; Otherwise, we will end up in
	 * wrong memory region.
	 */
	qp->s_ack_state = OP(ACKNOWLEDGE);
	qp->r_state = e->opcode;
	qp->r_nak_state = 0;
	qp->s_flags |= RVT_S_RESP_PENDING;
	hfi1_schedule_send(qp);
unlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
done:
	return 1;
}

void hfi1_rc_rcv_tid_rdma_read_req(struct hfi1_packet *packet)
{
	/* HANDLER FOR TID RDMA READ REQUEST packet (Responder side)*/

	/*
	 * 1. Verify TID RDMA READ REQ as per IB_OPCODE_RC_RDMA_READ
	 *    (see hfi1_rc_rcv())
	 * 2. Put TID RDMA READ REQ into the response queueu (s_ack_queue)
	 *     - Setup struct tid_rdma_req with request info
	 *     - Initialize struct tid_rdma_flow info;
	 *     - Copy TID entries;
	 * 3. Set the qp->s_ack_state.
	 * 4. Set RVT_S_RESP_PENDING in s_flags.
	 * 5. Kick the send engine (hfi1_schedule_send())
	 */
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_ack_entry *e;
	unsigned long flags;
	struct ib_reth *reth;
	struct hfi1_qp_priv *qpriv = qp->priv;
	u32 bth0, psn, len, rkey;
	bool is_fecn;
	u8 next;
	u64 vaddr;
	int diff;
	u8 nack_state = IB_NAK_INVALID_REQUEST;

	bth0 = be32_to_cpu(ohdr->bth[0]);
	if (hfi1_ruc_check_hdr(ibp, packet))
		return;

	is_fecn = process_ecn(qp, packet);
	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	trace_hfi1_rsp_rcv_tid_read_req(qp, psn);

	if (qp->state == IB_QPS_RTR && !(qp->r_flags & RVT_R_COMM_EST))
		rvt_comm_est(qp);

	if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_READ)))
		goto nack_inv;

	reth = &ohdr->u.tid_rdma.r_req.reth;
	vaddr = be64_to_cpu(reth->vaddr);
	len = be32_to_cpu(reth->length);
	/* The length needs to be in multiples of PAGE_SIZE */
	if (!len || len & ~PAGE_MASK || len > qpriv->tid_rdma.local.max_len)
		goto nack_inv;

	diff = delta_psn(psn, qp->r_psn);
	if (unlikely(diff)) {
		if (tid_rdma_rcv_error(packet, ohdr, qp, psn, diff))
			return;
		goto send_ack;
	}

	/* We've verified the request, insert it into the ack queue. */
	next = qp->r_head_ack_queue + 1;
	if (next > rvt_size_atomic(ib_to_rvt(qp->ibqp.device)))
		next = 0;
	spin_lock_irqsave(&qp->s_lock, flags);
	if (unlikely(next == qp->s_tail_ack_queue)) {
		if (!qp->s_ack_queue[next].sent) {
			nack_state = IB_NAK_REMOTE_OPERATIONAL_ERROR;
			goto nack_inv_unlock;
		}
		update_ack_queue(qp, next);
	}
	e = &qp->s_ack_queue[qp->r_head_ack_queue];
	if (e->rdma_sge.mr) {
		rvt_put_mr(e->rdma_sge.mr);
		e->rdma_sge.mr = NULL;
	}

	rkey = be32_to_cpu(reth->rkey);
	qp->r_len = len;

	if (unlikely(!rvt_rkey_ok(qp, &e->rdma_sge, qp->r_len, vaddr,
				  rkey, IB_ACCESS_REMOTE_READ)))
		goto nack_acc;

	/* Accept the request parameters */
	if (tid_rdma_rcv_read_request(qp, e, packet, ohdr, bth0, psn, vaddr,
				      len))
		goto nack_inv_unlock;

	qp->r_state = e->opcode;
	qp->r_nak_state = 0;
	/*
	 * We need to increment the MSN here instead of when we
	 * finish sending the result since a duplicate request would
	 * increment it more than once.
	 */
	qp->r_msn++;
	qp->r_psn += e->lpsn - e->psn + 1;

	qp->r_head_ack_queue = next;

	/* Schedule the send tasklet. */
	qp->s_flags |= RVT_S_RESP_PENDING;
	hfi1_schedule_send(qp);

	spin_unlock_irqrestore(&qp->s_lock, flags);
	if (is_fecn)
		goto send_ack;
	return;

nack_inv_unlock:
	spin_unlock_irqrestore(&qp->s_lock, flags);
nack_inv:
	rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
	qp->r_nak_state = nack_state;
	qp->r_ack_psn = qp->r_psn;
	/* Queue NAK for later */
	rc_defered_ack(rcd, qp);
	return;
nack_acc:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	rvt_rc_error(qp, IB_WC_LOC_PROT_ERR);
	qp->r_nak_state = IB_NAK_REMOTE_ACCESS_ERROR;
	qp->r_ack_psn = qp->r_psn;
send_ack:
	hfi1_send_rc_ack(packet, is_fecn);
}

u32 hfi1_build_tid_rdma_read_resp(struct rvt_qp *qp, struct rvt_ack_entry *e,
				  struct ib_other_headers *ohdr, u32 *bth0,
				  u32 *bth1, u32 *bth2, u32 *len, bool *last)
{
	struct hfi1_ack_priv *epriv = e->priv;
	struct tid_rdma_request *req = &epriv->tid_req;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_rdma_flow *flow = &req->flows[req->clear_tail];
	u32 tidentry = flow->tid_entry[flow->tid_idx];
	u32 tidlen = EXP_TID_GET(tidentry, LEN) << PAGE_SHIFT;
	struct tid_rdma_read_resp *resp = &ohdr->u.tid_rdma.r_rsp;
	u32 next_offset, om = KDETH_OM_LARGE;
	bool last_pkt;
	u32 hdwords = 0;
	struct tid_rdma_params *remote;

	*len = min_t(u32, qp->pmtu, tidlen - flow->tid_offset);
	flow->sent += *len;
	next_offset = flow->tid_offset + *len;
	last_pkt = (flow->sent >= flow->length);

	trace_hfi1_tid_entry_build_read_resp(qp, flow->tid_idx, tidentry);
	trace_hfi1_tid_flow_build_read_resp(qp, req->clear_tail, flow);

	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);
	if (!remote) {
		rcu_read_unlock();
		goto done;
	}
	KDETH_RESET(resp->kdeth0, KVER, 0x1);
	KDETH_SET(resp->kdeth0, SH, !last_pkt);
	KDETH_SET(resp->kdeth0, INTR, !!(!last_pkt && remote->urg));
	KDETH_SET(resp->kdeth0, TIDCTRL, EXP_TID_GET(tidentry, CTRL));
	KDETH_SET(resp->kdeth0, TID, EXP_TID_GET(tidentry, IDX));
	KDETH_SET(resp->kdeth0, OM, om == KDETH_OM_LARGE);
	KDETH_SET(resp->kdeth0, OFFSET, flow->tid_offset / om);
	KDETH_RESET(resp->kdeth1, JKEY, remote->jkey);
	resp->verbs_qp = cpu_to_be32(qp->remote_qpn);
	rcu_read_unlock();

	resp->aeth = rvt_compute_aeth(qp);
	resp->verbs_psn = cpu_to_be32(mask_psn(flow->flow_state.ib_spsn +
					       flow->pkt));

	*bth0 = TID_OP(READ_RESP) << 24;
	*bth1 = flow->tid_qpn;
	*bth2 = mask_psn(((flow->flow_state.spsn + flow->pkt++) &
			  HFI1_KDETH_BTH_SEQ_MASK) |
			 (flow->flow_state.generation <<
			  HFI1_KDETH_BTH_SEQ_SHIFT));
	*last = last_pkt;
	if (last_pkt)
		/* Advance to next flow */
		req->clear_tail = (req->clear_tail + 1) &
				  (MAX_FLOWS - 1);

	if (next_offset >= tidlen) {
		flow->tid_offset = 0;
		flow->tid_idx++;
	} else {
		flow->tid_offset = next_offset;
	}

	hdwords = sizeof(ohdr->u.tid_rdma.r_rsp) / sizeof(u32);

done:
	return hdwords;
}

static inline struct tid_rdma_request *
find_tid_request(struct rvt_qp *qp, u32 psn, enum ib_wr_opcode opcode)
	__must_hold(&qp->s_lock)
{
	struct rvt_swqe *wqe;
	struct tid_rdma_request *req = NULL;
	u32 i, end;

	end = qp->s_cur + 1;
	if (end == qp->s_size)
		end = 0;
	for (i = qp->s_acked; i != end;) {
		wqe = rvt_get_swqe_ptr(qp, i);
		if (cmp_psn(psn, wqe->psn) >= 0 &&
		    cmp_psn(psn, wqe->lpsn) <= 0) {
			if (wqe->wr.opcode == opcode)
				req = wqe_to_tid_req(wqe);
			break;
		}
		if (++i == qp->s_size)
			i = 0;
	}

	return req;
}

void hfi1_rc_rcv_tid_rdma_read_resp(struct hfi1_packet *packet)
{
	/* HANDLER FOR TID RDMA READ RESPONSE packet (Requestor side */

	/*
	 * 1. Find matching SWQE
	 * 2. Check that the entire segment has been read.
	 * 3. Remove HFI1_S_WAIT_TID_RESP from s_flags.
	 * 4. Free the TID flow resources.
	 * 5. Kick the send engine (hfi1_schedule_send())
	 */
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	u32 opcode, aeth;
	bool is_fecn;
	unsigned long flags;
	u32 kpsn, ipsn;

	trace_hfi1_sender_rcv_tid_read_resp(qp);
	is_fecn = process_ecn(qp, packet);
	kpsn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	aeth = be32_to_cpu(ohdr->u.tid_rdma.r_rsp.aeth);
	opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;

	spin_lock_irqsave(&qp->s_lock, flags);
	ipsn = mask_psn(be32_to_cpu(ohdr->u.tid_rdma.r_rsp.verbs_psn));
	req = find_tid_request(qp, ipsn, IB_WR_TID_RDMA_READ);
	if (unlikely(!req))
		goto ack_op_err;

	flow = &req->flows[req->clear_tail];
	/* When header suppression is disabled */
	if (cmp_psn(ipsn, flow->flow_state.ib_lpsn))
		goto ack_done;
	req->ack_pending--;
	priv->pending_tid_r_segs--;
	qp->s_num_rd_atomic--;
	if ((qp->s_flags & RVT_S_WAIT_FENCE) &&
	    !qp->s_num_rd_atomic) {
		qp->s_flags &= ~(RVT_S_WAIT_FENCE |
				 RVT_S_WAIT_ACK);
		hfi1_schedule_send(qp);
	}
	if (qp->s_flags & RVT_S_WAIT_RDMAR) {
		qp->s_flags &= ~(RVT_S_WAIT_RDMAR | RVT_S_WAIT_ACK);
		hfi1_schedule_send(qp);
	}

	trace_hfi1_ack(qp, ipsn);
	trace_hfi1_tid_req_rcv_read_resp(qp, 0, req->e.swqe->wr.opcode,
					 req->e.swqe->psn, req->e.swqe->lpsn,
					 req);
	trace_hfi1_tid_flow_rcv_read_resp(qp, req->clear_tail, flow);

	/* Release the tid resources */
	hfi1_kern_exp_rcv_clear(req);

	if (!do_rc_ack(qp, aeth, ipsn, opcode, 0, rcd))
		goto ack_done;

	/* If not done yet, build next read request */
	if (++req->comp_seg >= req->total_segs) {
		priv->tid_r_comp++;
		req->state = TID_REQUEST_COMPLETE;
	}

	/*
	 * Clear the hw flow under two conditions:
	 * 1. This request is a sync point and it is complete;
	 * 2. Current request is completed and there are no more requests.
	 */
	if ((req->state == TID_REQUEST_SYNC &&
	     req->comp_seg == req->cur_seg) ||
	    priv->tid_r_comp == priv->tid_r_reqs) {
		hfi1_kern_clear_hw_flow(priv->rcd, qp);
		if (req->state == TID_REQUEST_SYNC)
			req->state = TID_REQUEST_ACTIVE;
	}

	hfi1_schedule_send(qp);
	goto ack_done;

ack_op_err:
	/*
	 * The test indicates that the send engine has finished its cleanup
	 * after sending the request and it's now safe to put the QP into error
	 * state. However, if the wqe queue is empty (qp->s_acked == qp->s_tail
	 * == qp->s_head), it would be unsafe to complete the wqe pointed by
	 * qp->s_acked here. Putting the qp into error state will safely flush
	 * all remaining requests.
	 */
	if (qp->s_last == qp->s_acked)
		rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);

ack_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	if (is_fecn)
		hfi1_send_rc_ack(packet, is_fecn);
}

void hfi1_kern_read_tid_flow_free(struct rvt_qp *qp)
	__must_hold(&qp->s_lock)
{
	u32 n = qp->s_acked;
	struct rvt_swqe *wqe;
	struct tid_rdma_request *req;
	struct hfi1_qp_priv *priv = qp->priv;

	lockdep_assert_held(&qp->s_lock);
	/* Free any TID entries */
	while (n != qp->s_tail) {
		wqe = rvt_get_swqe_ptr(qp, n);
		if (wqe->wr.opcode == IB_WR_TID_RDMA_READ) {
			req = wqe_to_tid_req(wqe);
			hfi1_kern_exp_rcv_clear_all(req);
		}

		if (++n == qp->s_size)
			n = 0;
	}
	/* Free flow */
	hfi1_kern_clear_hw_flow(priv->rcd, qp);
}

static bool tid_rdma_tid_err(struct hfi1_ctxtdata *rcd,
			     struct hfi1_packet *packet, u8 rcv_type,
			     u8 opcode)
{
	struct rvt_qp *qp = packet->qp;
	u32 ipsn;
	struct ib_other_headers *ohdr = packet->ohdr;

	if (rcv_type >= RHF_RCV_TYPE_IB)
		goto done;

	spin_lock(&qp->s_lock);
	/*
	 * For TID READ response, error out QP after freeing the tid
	 * resources.
	 */
	if (opcode == TID_OP(READ_RESP)) {
		ipsn = mask_psn(be32_to_cpu(ohdr->u.tid_rdma.r_rsp.verbs_psn));
		if (cmp_psn(ipsn, qp->s_last_psn) > 0 &&
		    cmp_psn(ipsn, qp->s_psn) < 0) {
			hfi1_kern_read_tid_flow_free(qp);
			spin_unlock(&qp->s_lock);
			rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
			goto done;
		}
	}

	spin_unlock(&qp->s_lock);
done:
	return true;
}

static void restart_tid_rdma_read_req(struct hfi1_ctxtdata *rcd,
				      struct rvt_qp *qp, struct rvt_swqe *wqe)
{
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;

	/* Start from the right segment */
	qp->r_flags |= RVT_R_RDMAR_SEQ;
	req = wqe_to_tid_req(wqe);
	flow = &req->flows[req->clear_tail];
	hfi1_restart_rc(qp, flow->flow_state.ib_spsn, 0);
	if (list_empty(&qp->rspwait)) {
		qp->r_flags |= RVT_R_RSP_SEND;
		rvt_get_qp(qp);
		list_add_tail(&qp->rspwait, &rcd->qp_wait_list);
	}
}

/*
 * Handle the KDETH eflags for TID RDMA READ response.
 *
 * Return true if the last packet for a segment has been received and it is
 * time to process the response normally; otherwise, return true.
 *
 * The caller must hold the packet->qp->r_lock and the rcu_read_lock.
 */
static bool handle_read_kdeth_eflags(struct hfi1_ctxtdata *rcd,
				     struct hfi1_packet *packet, u8 rcv_type,
				     u8 rte, u32 psn, u32 ibpsn)
	__must_hold(&packet->qp->r_lock) __must_hold(RCU)
{
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_devdata *dd = ppd->dd;
	struct hfi1_ibport *ibp;
	struct rvt_swqe *wqe;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	u32 ack_psn;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_qp_priv *priv = qp->priv;
	bool ret = true;
	int diff = 0;
	u32 fpsn;

	lockdep_assert_held(&qp->r_lock);
	/* If the psn is out of valid range, drop the packet */
	if (cmp_psn(ibpsn, qp->s_last_psn) < 0 ||
	    cmp_psn(ibpsn, qp->s_psn) > 0)
		return ret;

	spin_lock(&qp->s_lock);
	/*
	 * Note that NAKs implicitly ACK outstanding SEND and RDMA write
	 * requests and implicitly NAK RDMA read and atomic requests issued
	 * before the NAK'ed request.
	 */
	ack_psn = ibpsn - 1;
	wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
	ibp = to_iport(qp->ibqp.device, qp->port_num);

	/* Complete WQEs that the PSN finishes. */
	while ((int)delta_psn(ack_psn, wqe->lpsn) >= 0) {
		/*
		 * If this request is a RDMA read or atomic, and the NACK is
		 * for a later operation, this NACK NAKs the RDMA read or
		 * atomic.
		 */
		if (wqe->wr.opcode == IB_WR_RDMA_READ ||
		    wqe->wr.opcode == IB_WR_TID_RDMA_READ ||
		    wqe->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		    wqe->wr.opcode == IB_WR_ATOMIC_FETCH_AND_ADD) {
			/* Retry this request. */
			if (!(qp->r_flags & RVT_R_RDMAR_SEQ)) {
				qp->r_flags |= RVT_R_RDMAR_SEQ;
				if (wqe->wr.opcode == IB_WR_TID_RDMA_READ) {
					restart_tid_rdma_read_req(rcd, qp,
								  wqe);
				} else {
					hfi1_restart_rc(qp, qp->s_last_psn + 1,
							0);
					if (list_empty(&qp->rspwait)) {
						qp->r_flags |= RVT_R_RSP_SEND;
						rvt_get_qp(qp);
						list_add_tail(/* wait */
						   &qp->rspwait,
						   &rcd->qp_wait_list);
					}
				}
			}
			/*
			 * No need to process the NAK since we are
			 * restarting an earlier request.
			 */
			break;
		}

		wqe = do_rc_completion(qp, wqe, ibp);
		if (qp->s_acked == qp->s_tail)
			break;
	}

	/* Handle the eflags for the request */
	if (wqe->wr.opcode != IB_WR_TID_RDMA_READ)
		goto s_unlock;

	req = wqe_to_tid_req(wqe);
	switch (rcv_type) {
	case RHF_RCV_TYPE_EXPECTED:
		switch (rte) {
		case RHF_RTE_EXPECTED_FLOW_SEQ_ERR:
			/*
			 * On the first occurrence of a Flow Sequence error,
			 * the flag TID_FLOW_SW_PSN is set.
			 *
			 * After that, the flow is *not* reprogrammed and the
			 * protocol falls back to SW PSN checking. This is done
			 * to prevent continuous Flow Sequence errors for any
			 * packets that could be still in the fabric.
			 */
			flow = find_flow(req, psn, NULL);
			if (!flow) {
				/*
				 * We can't find the IB PSN matching the
				 * received KDETH PSN. The only thing we can
				 * do at this point is report the error to
				 * the QP.
				 */
				hfi1_kern_read_tid_flow_free(qp);
				spin_unlock(&qp->s_lock);
				rvt_rc_error(qp, IB_WC_LOC_QP_OP_ERR);
				return ret;
			}
			if (priv->flow_state.flags & TID_FLOW_SW_PSN) {
				diff = cmp_psn(psn,
					       priv->flow_state.r_next_psn);
				if (diff > 0) {
					if (!(qp->r_flags & RVT_R_RDMAR_SEQ))
						restart_tid_rdma_read_req(rcd,
									  qp,
									  wqe);

					/* Drop the packet.*/
					goto s_unlock;
				} else if (diff < 0) {
					/*
					 * If a response packet for a restarted
					 * request has come back, reset the
					 * restart flag.
					 */
					if (qp->r_flags & RVT_R_RDMAR_SEQ)
						qp->r_flags &=
							~RVT_R_RDMAR_SEQ;

					/* Drop the packet.*/
					goto s_unlock;
				}

				/*
				 * If SW PSN verification is successful and
				 * this is the last packet in the segment, tell
				 * the caller to process it as a normal packet.
				 */
				fpsn = full_flow_psn(flow,
						     flow->flow_state.lpsn);
				if (cmp_psn(fpsn, psn) == 0) {
					ret = false;
					if (qp->r_flags & RVT_R_RDMAR_SEQ)
						qp->r_flags &=
							~RVT_R_RDMAR_SEQ;
				}
				priv->flow_state.r_next_psn++;
			} else {
				u64 reg;
				u32 last_psn;

				/*
				 * The only sane way to get the amount of
				 * progress is to read the HW flow state.
				 */
				reg = read_uctxt_csr(dd, rcd->ctxt,
						     RCV_TID_FLOW_TABLE +
						     (8 * flow->idx));
				last_psn = mask_psn(reg);

				priv->flow_state.r_next_psn = last_psn;
				priv->flow_state.flags |= TID_FLOW_SW_PSN;
				/*
				 * If no request has been restarted yet,
				 * restart the current one.
				 */
				if (!(qp->r_flags & RVT_R_RDMAR_SEQ))
					restart_tid_rdma_read_req(rcd, qp,
								  wqe);
			}

			break;

		case RHF_RTE_EXPECTED_FLOW_GEN_ERR:
			/*
			 * Since the TID flow is able to ride through
			 * generation mismatch, drop this stale packet.
			 */
			break;

		default:
			break;
		}
		break;

	case RHF_RCV_TYPE_ERROR:
		switch (rte) {
		case RHF_RTE_ERROR_OP_CODE_ERR:
		case RHF_RTE_ERROR_KHDR_MIN_LEN_ERR:
		case RHF_RTE_ERROR_KHDR_HCRC_ERR:
		case RHF_RTE_ERROR_KHDR_KVER_ERR:
		case RHF_RTE_ERROR_CONTEXT_ERR:
		case RHF_RTE_ERROR_KHDR_TID_ERR:
		default:
			break;
		}
	default:
		break;
	}
s_unlock:
	spin_unlock(&qp->s_lock);
	return ret;
}

bool hfi1_handle_kdeth_eflags(struct hfi1_ctxtdata *rcd,
			      struct hfi1_pportdata *ppd,
			      struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	struct hfi1_devdata *dd = ppd->dd;
	struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;
	u8 rcv_type = rhf_rcv_type(packet->rhf);
	u8 rte = rhf_rcv_type_err(packet->rhf);
	struct ib_header *hdr = packet->hdr;
	struct ib_other_headers *ohdr = NULL;
	int lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	u16 lid  = be16_to_cpu(hdr->lrh[1]);
	u8 opcode;
	u32 qp_num, psn, ibpsn;
	struct rvt_qp *qp;
	unsigned long flags;
	bool ret = true;

	trace_hfi1_msg_handle_kdeth_eflags(NULL, "Kdeth error: rhf ",
					   packet->rhf);
	if (packet->rhf & (RHF_VCRC_ERR | RHF_ICRC_ERR))
		return ret;

	packet->ohdr = &hdr->u.oth;
	ohdr = packet->ohdr;
	trace_input_ibhdr(rcd->dd, packet, !!(rhf_dc_info(packet->rhf)));

	/* Get the destination QP number. */
	qp_num = be32_to_cpu(ohdr->u.tid_rdma.r_rsp.verbs_qp) &
		RVT_QPN_MASK;
	if (lid >= be16_to_cpu(IB_MULTICAST_LID_BASE))
		goto drop;

	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;

	rcu_read_lock();
	qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
	if (!qp)
		goto rcu_unlock;

	packet->qp = qp;

	/* Check for valid receive state. */
	spin_lock_irqsave(&qp->r_lock, flags);
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK)) {
		ibp->rvp.n_pkt_drops++;
		goto r_unlock;
	}

	if (packet->rhf & RHF_TID_ERR) {
		/* For TIDERR and RC QPs preemptively schedule a NAK */
		u32 tlen = rhf_pkt_len(packet->rhf); /* in bytes */

		/* Sanity check packet */
		if (tlen < 24)
			goto r_unlock;

		/*
		 * Check for GRH. We should never get packets with GRH in this
		 * path.
		 */
		if (lnh == HFI1_LRH_GRH)
			goto r_unlock;

		if (tid_rdma_tid_err(rcd, packet, rcv_type, opcode))
			goto r_unlock;
	}

	/* handle TID RDMA READ */
	if (opcode == TID_OP(READ_RESP)) {
		ibpsn = be32_to_cpu(ohdr->u.tid_rdma.r_rsp.verbs_psn);
		ibpsn = mask_psn(ibpsn);
		ret = handle_read_kdeth_eflags(rcd, packet, rcv_type, rte, psn,
					       ibpsn);
	}

r_unlock:
	spin_unlock_irqrestore(&qp->r_lock, flags);
rcu_unlock:
	rcu_read_unlock();
drop:
	return ret;
}

/*
 * "Rewind" the TID request information.
 * This means that we reset the state back to ACTIVE,
 * find the proper flow, set the flow index to that flow,
 * and reset the flow information.
 */
void hfi1_tid_rdma_restart_req(struct rvt_qp *qp, struct rvt_swqe *wqe,
			       u32 *bth2)
{
	struct tid_rdma_request *req = wqe_to_tid_req(wqe);
	struct tid_rdma_flow *flow;
	int diff;
	u32 tididx = 0;
	u16 fidx;

	if (wqe->wr.opcode == IB_WR_TID_RDMA_READ) {
		*bth2 = mask_psn(qp->s_psn);
		flow = find_flow_ib(req, *bth2, &fidx);
		if (!flow) {
			trace_hfi1_msg_tid_restart_req(/* msg */
			   qp, "!!!!!! Could not find flow to restart: bth2 ",
			   (u64)*bth2);
			trace_hfi1_tid_req_restart_req(qp, 0, wqe->wr.opcode,
						       wqe->psn, wqe->lpsn,
						       req);
			return;
		}
	} else {
		return;
	}

	trace_hfi1_tid_flow_restart_req(qp, fidx, flow);
	diff = delta_psn(*bth2, flow->flow_state.ib_spsn);

	flow->sent = 0;
	flow->pkt = 0;
	flow->tid_idx = 0;
	flow->tid_offset = 0;
	if (diff) {
		for (tididx = 0; tididx < flow->tidcnt; tididx++) {
			u32 tidentry = flow->tid_entry[tididx], tidlen,
				tidnpkts, npkts;

			flow->tid_offset = 0;
			tidlen = EXP_TID_GET(tidentry, LEN) * PAGE_SIZE;
			tidnpkts = rvt_div_round_up_mtu(qp, tidlen);
			npkts = min_t(u32, diff, tidnpkts);
			flow->pkt += npkts;
			flow->sent += (npkts == tidnpkts ? tidlen :
				       npkts * qp->pmtu);
			flow->tid_offset += npkts * qp->pmtu;
			diff -= npkts;
			if (!diff)
				break;
		}
	}

	if (flow->tid_offset ==
	    EXP_TID_GET(flow->tid_entry[tididx], LEN) * PAGE_SIZE) {
		tididx++;
		flow->tid_offset = 0;
	}
	flow->tid_idx = tididx;
	/* Move flow_idx to correct index */
	req->flow_idx = fidx;

	trace_hfi1_tid_flow_restart_req(qp, fidx, flow);
	trace_hfi1_tid_req_restart_req(qp, 0, wqe->wr.opcode, wqe->psn,
				       wqe->lpsn, req);
	req->state = TID_REQUEST_ACTIVE;
}

void hfi1_qp_kern_exp_rcv_clear_all(struct rvt_qp *qp)
{
	int i, ret;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_flow_state *fs;

	if (qp->ibqp.qp_type != IB_QPT_RC || !HFI1_CAP_IS_KSET(TID_RDMA))
		return;

	/*
	 * First, clear the flow to help prevent any delayed packets from
	 * being delivered.
	 */
	fs = &qpriv->flow_state;
	if (fs->index != RXE_NUM_TID_FLOWS)
		hfi1_kern_clear_hw_flow(qpriv->rcd, qp);

	for (i = qp->s_acked; i != qp->s_head;) {
		struct rvt_swqe *wqe = rvt_get_swqe_ptr(qp, i);

		if (++i == qp->s_size)
			i = 0;
		/* Free only locally allocated TID entries */
		if (wqe->wr.opcode != IB_WR_TID_RDMA_READ)
			continue;
		do {
			struct hfi1_swqe_priv *priv = wqe->priv;

			ret = hfi1_kern_exp_rcv_clear(&priv->tid_req);
		} while (!ret);
	}
}

bool hfi1_tid_rdma_wqe_interlock(struct rvt_qp *qp, struct rvt_swqe *wqe)
{
	struct rvt_swqe *prev;
	struct hfi1_qp_priv *priv = qp->priv;
	u32 s_prev;

	s_prev = (qp->s_cur == 0 ? qp->s_size : qp->s_cur) - 1;
	prev = rvt_get_swqe_ptr(qp, s_prev);

	switch (wqe->wr.opcode) {
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_SEND_WITH_INV:
	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_READ:
		break;
	case IB_WR_TID_RDMA_READ:
		switch (prev->wr.opcode) {
		case IB_WR_RDMA_READ:
			if (qp->s_acked != qp->s_cur)
				goto interlock;
			break;
		default:
			break;
		}
	default:
		break;
	}
	return false;

interlock:
	priv->s_flags |= HFI1_S_TID_WAIT_INTERLCK;
	return true;
}

/* Does @sge meet the alignment requirements for tid rdma? */
static inline bool hfi1_check_sge_align(struct rvt_qp *qp,
					struct rvt_sge *sge, int num_sge)
{
	int i;

	for (i = 0; i < num_sge; i++, sge++) {
		trace_hfi1_sge_check_align(qp, i, sge);
		if ((u64)sge->vaddr & ~PAGE_MASK ||
		    sge->sge_length & ~PAGE_MASK)
			return false;
	}
	return true;
}

void setup_tid_rdma_wqe(struct rvt_qp *qp, struct rvt_swqe *wqe)
{
	struct hfi1_qp_priv *qpriv = (struct hfi1_qp_priv *)qp->priv;
	struct hfi1_swqe_priv *priv = wqe->priv;
	struct tid_rdma_params *remote;
	enum ib_wr_opcode new_opcode;
	bool do_tid_rdma = false;
	struct hfi1_pportdata *ppd = qpriv->rcd->ppd;

	if ((rdma_ah_get_dlid(&qp->remote_ah_attr) & ~((1 << ppd->lmc) - 1)) ==
				ppd->lid)
		return;
	if (qpriv->hdr_type != HFI1_PKT_TYPE_9B)
		return;

	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);
	/*
	 * If TID RDMA is disabled by the negotiation, don't
	 * use it.
	 */
	if (!remote)
		goto exit;

	if (wqe->wr.opcode == IB_WR_RDMA_READ) {
		if (hfi1_check_sge_align(qp, &wqe->sg_list[0],
					 wqe->wr.num_sge)) {
			new_opcode = IB_WR_TID_RDMA_READ;
			do_tid_rdma = true;
		}
	}

	if (do_tid_rdma) {
		if (hfi1_kern_exp_rcv_alloc_flows(&priv->tid_req, GFP_ATOMIC))
			goto exit;
		wqe->wr.opcode = new_opcode;
		priv->tid_req.seg_len =
			min_t(u32, remote->max_len, wqe->length);
		priv->tid_req.total_segs =
			DIV_ROUND_UP(wqe->length, priv->tid_req.seg_len);
		/* Compute the last PSN of the request */
		wqe->lpsn = wqe->psn;
		if (wqe->wr.opcode == IB_WR_TID_RDMA_READ) {
			priv->tid_req.n_flows = remote->max_read;
			qpriv->tid_r_reqs++;
			wqe->lpsn += rvt_div_round_up_mtu(qp, wqe->length) - 1;
		}

		priv->tid_req.cur_seg = 0;
		priv->tid_req.comp_seg = 0;
		priv->tid_req.ack_seg = 0;
		priv->tid_req.state = TID_REQUEST_INACTIVE;
		trace_hfi1_tid_req_setup_tid_wqe(qp, 1, wqe->wr.opcode,
						 wqe->psn, wqe->lpsn,
						 &priv->tid_req);
	}
exit:
	rcu_read_unlock();
}

/* TID RDMA WRITE functions */

u32 hfi1_build_tid_rdma_write_req(struct rvt_qp *qp, struct rvt_swqe *wqe,
				  struct ib_other_headers *ohdr,
				  u32 *bth1, u32 *bth2, u32 *len)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_rdma_request *req = wqe_to_tid_req(wqe);
	struct tid_rdma_params *remote;

	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);
	/*
	 * Set the number of flow to be used based on negotiated
	 * parameters.
	 */
	req->n_flows = remote->max_write;
	req->state = TID_REQUEST_ACTIVE;

	KDETH_RESET(ohdr->u.tid_rdma.w_req.kdeth0, KVER, 0x1);
	KDETH_RESET(ohdr->u.tid_rdma.w_req.kdeth1, JKEY, remote->jkey);
	ohdr->u.tid_rdma.w_req.reth.vaddr =
		cpu_to_be64(wqe->rdma_wr.remote_addr + (wqe->length - *len));
	ohdr->u.tid_rdma.w_req.reth.rkey =
		cpu_to_be32(wqe->rdma_wr.rkey);
	ohdr->u.tid_rdma.w_req.reth.length = cpu_to_be32(*len);
	ohdr->u.tid_rdma.w_req.verbs_qp = cpu_to_be32(qp->remote_qpn);
	*bth1 &= ~RVT_QPN_MASK;
	*bth1 |= remote->qp;
	qp->s_state = TID_OP(WRITE_REQ);
	qp->s_flags |= HFI1_S_WAIT_TID_RESP;
	*bth2 |= IB_BTH_REQ_ACK;
	*len = 0;

	rcu_read_unlock();
	return sizeof(ohdr->u.tid_rdma.w_req) / sizeof(u32);
}
