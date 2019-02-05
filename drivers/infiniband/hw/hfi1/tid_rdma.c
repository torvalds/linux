// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */

#include "hfi.h"
#include "qp.h"
#include "verbs.h"
#include "tid_rdma.h"
#include "trace.h"

#define RCV_TID_FLOW_TABLE_CTRL_FLOW_VALID_SMASK BIT_ULL(32)
#define RCV_TID_FLOW_TABLE_CTRL_HDR_SUPP_EN_SMASK BIT_ULL(33)
#define RCV_TID_FLOW_TABLE_CTRL_KEEP_AFTER_SEQ_ERR_SMASK BIT_ULL(34)
#define RCV_TID_FLOW_TABLE_CTRL_KEEP_ON_GEN_ERR_SMASK BIT_ULL(35)
#define RCV_TID_FLOW_TABLE_STATUS_SEQ_MISMATCH_SMASK BIT_ULL(37)
#define RCV_TID_FLOW_TABLE_STATUS_GEN_MISMATCH_SMASK BIT_ULL(38)

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

#define TID_RDMA_MAX_READ_SEGS_PER_REQ  6
#define TID_RDMA_MAX_WRITE_SEGS_PER_REQ 4

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
	return 0;
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

	qpriv->rcd = qp_to_rcd(rdi, qp);

	spin_lock_init(&qpriv->opfn.lock);
	INIT_WORK(&qpriv->opfn.opfn_work, opfn_send_conn_request);
	INIT_WORK(&qpriv->tid_rdma.trigger_work, tid_rdma_trigger_resume);
	qpriv->flow_state.psn = 0;
	qpriv->flow_state.index = RXE_NUM_TID_FLOWS;
	qpriv->flow_state.last_index = RXE_NUM_TID_FLOWS;
	qpriv->flow_state.generation = KERN_GENERATION_RESERVED;
	INIT_LIST_HEAD(&qpriv->tid_wait);

	return 0;
}

void hfi1_qp_priv_tid_free(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	if (qp->ibqp.qp_type == IB_QPT_RC && HFI1_CAP_IS_KSET(TID_RDMA))
		cancel_work_sync(&priv->opfn.opfn_work);
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
