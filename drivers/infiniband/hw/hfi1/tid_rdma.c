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

static u32 tid_rdma_flow_wt;

static void tid_rdma_trigger_resume(struct work_struct *work);
static void hfi1_kern_exp_rcv_free_flows(struct tid_rdma_request *req);
static int hfi1_kern_exp_rcv_alloc_flows(struct tid_rdma_request *req,
					 gfp_t gfp);
static void hfi1_init_trdma_req(struct rvt_qp *qp,
				struct tid_rdma_request *req);
static void hfi1_tid_write_alloc_resources(struct rvt_qp *qp, bool intr_ctx);
static void hfi1_tid_timeout(struct timer_list *t);
static void hfi1_add_tid_reap_timer(struct rvt_qp *qp);
static void hfi1_mod_tid_reap_timer(struct rvt_qp *qp);
static void hfi1_mod_tid_retry_timer(struct rvt_qp *qp);
static int hfi1_stop_tid_retry_timer(struct rvt_qp *qp);
static void hfi1_tid_retry_timeout(struct timer_list *t);
static int make_tid_rdma_ack(struct rvt_qp *qp,
			     struct ib_other_headers *ohdr,
			     struct hfi1_pkt_state *ps);
static void hfi1_do_tid_send(struct rvt_qp *qp);

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
	qpriv->s_state = TID_OP(WRITE_RESP);
	qpriv->s_tid_cur = HFI1_QP_WQE_INVALID;
	qpriv->s_tid_head = HFI1_QP_WQE_INVALID;
	qpriv->s_tid_tail = HFI1_QP_WQE_INVALID;
	qpriv->rnr_nak_state = TID_RNR_NAK_INIT;
	qpriv->r_tid_head = HFI1_QP_WQE_INVALID;
	qpriv->r_tid_tail = HFI1_QP_WQE_INVALID;
	qpriv->r_tid_ack = HFI1_QP_WQE_INVALID;
	qpriv->r_tid_alloc = HFI1_QP_WQE_INVALID;
	atomic_set(&qpriv->n_requests, 0);
	atomic_set(&qpriv->n_tid_requests, 0);
	timer_setup(&qpriv->s_tid_timer, hfi1_tid_timeout, 0);
	timer_setup(&qpriv->s_tid_retry_timer, hfi1_tid_retry_timeout, 0);
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
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	struct hfi1_qp_priv *qpriv = qp->priv;
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
	if (!e || (e->opcode != TID_OP(READ_REQ) &&
		   e->opcode != TID_OP(WRITE_REQ)))
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
	} else {
		struct flow_state *fstate;
		bool schedule = false;
		u8 i;

		if (req->state == TID_REQUEST_RESEND) {
			req->state = TID_REQUEST_RESEND_ACTIVE;
		} else if (req->state == TID_REQUEST_INIT_RESEND) {
			req->state = TID_REQUEST_INIT;
			schedule = true;
		}

		/*
		 * True if the request is already scheduled (between
		 * qp->s_tail_ack_queue and qp->r_head_ack_queue).
		 * Also, don't change requests, which are at the SYNC
		 * point and haven't generated any responses yet.
		 * There is nothing to retransmit for them yet.
		 */
		if (old_req || req->state == TID_REQUEST_INIT ||
		    (req->state == TID_REQUEST_SYNC && !req->cur_seg)) {
			for (i = prev + 1; ; i++) {
				if (i > rvt_size_atomic(&dev->rdi))
					i = 0;
				if (i == qp->r_head_ack_queue)
					break;
				e = &qp->s_ack_queue[i];
				req = ack_to_tid_req(e);
				if (e->opcode == TID_OP(WRITE_REQ) &&
				    req->state == TID_REQUEST_INIT)
					req->state = TID_REQUEST_INIT_RESEND;
			}
			/*
			 * If the state of the request has been changed,
			 * the first leg needs to get scheduled in order to
			 * pick up the change. Otherwise, normal response
			 * processing should take care of it.
			 */
			if (!schedule)
				goto unlock;
		}

		/*
		 * If there is no more allocated segment, just schedule the qp
		 * without changing any state.
		 */
		if (req->clear_tail == req->setup_head)
			goto schedule;
		/*
		 * If this request has sent responses for segments, which have
		 * not received data yet (flow_idx != clear_tail), the flow_idx
		 * pointer needs to be adjusted so the same responses can be
		 * re-sent.
		 */
		if (CIRC_CNT(req->flow_idx, req->clear_tail, MAX_FLOWS)) {
			fstate = &req->flows[req->clear_tail].flow_state;
			qpriv->pending_tid_w_segs -=
				CIRC_CNT(req->flow_idx, req->clear_tail,
					 MAX_FLOWS);
			req->flow_idx =
				CIRC_ADD(req->clear_tail,
					 delta_psn(psn, fstate->resp_ib_psn),
					 MAX_FLOWS);
			qpriv->pending_tid_w_segs +=
				delta_psn(psn, fstate->resp_ib_psn);
			/*
			 * When flow_idx == setup_head, we've gotten a duplicate
			 * request for a segment, which has not been allocated
			 * yet. In that case, don't adjust this request.
			 * However, we still want to go through the loop below
			 * to adjust all subsequent requests.
			 */
			if (CIRC_CNT(req->setup_head, req->flow_idx,
				     MAX_FLOWS)) {
				req->cur_seg = delta_psn(psn, e->psn);
				req->state = TID_REQUEST_RESEND_ACTIVE;
			}
		}

		for (i = prev + 1; ; i++) {
			/*
			 * Look at everything up to and including
			 * s_tail_ack_queue
			 */
			if (i > rvt_size_atomic(&dev->rdi))
				i = 0;
			if (i == qp->r_head_ack_queue)
				break;
			e = &qp->s_ack_queue[i];
			req = ack_to_tid_req(e);
			trace_hfi1_tid_req_rcv_err(qp, 0, e->opcode, e->psn,
						   e->lpsn, req);
			if (e->opcode != TID_OP(WRITE_REQ) ||
			    req->cur_seg == req->comp_seg ||
			    req->state == TID_REQUEST_INIT ||
			    req->state == TID_REQUEST_INIT_RESEND) {
				if (req->state == TID_REQUEST_INIT)
					req->state = TID_REQUEST_INIT_RESEND;
				continue;
			}
			qpriv->pending_tid_w_segs -=
				CIRC_CNT(req->flow_idx,
					 req->clear_tail,
					 MAX_FLOWS);
			req->flow_idx = req->clear_tail;
			req->state = TID_REQUEST_RESEND;
			req->cur_seg = req->comp_seg;
		}
		qpriv->s_flags &= ~HFI1_R_TID_WAIT_INTERLCK;
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
schedule:
	/*
	 * It's possible to receive a retry psn that is earlier than an RNRNAK
	 * psn. In this case, the rnrnak state should be cleared.
	 */
	if (qpriv->rnr_nak_state) {
		qp->s_nak_state = 0;
		qpriv->rnr_nak_state = TID_RNR_NAK_INIT;
		qp->r_psn = e->lpsn + 1;
		hfi1_tid_write_alloc_resources(qp, true);
	}

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

	/*
	 * For all requests other than TID WRITE which are added to the ack
	 * queue, qpriv->r_tid_alloc follows qp->r_head_ack_queue. It is ok to
	 * do this because of interlocks between these and TID WRITE
	 * requests. The same change has also been made in hfi1_rc_rcv().
	 */
	qpriv->r_tid_alloc = qp->r_head_ack_queue;

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
	struct hfi1_qp_priv *qpriv = qp->priv;
	u32 ipsn;
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_ack_entry *e;
	struct tid_rdma_request *req;
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);
	u32 i;

	if (rcv_type >= RHF_RCV_TYPE_IB)
		goto done;

	spin_lock(&qp->s_lock);

	/*
	 * We've ran out of space in the eager buffer.
	 * Eagerly received KDETH packets which require space in the
	 * Eager buffer (packet that have payload) are TID RDMA WRITE
	 * response packets. In this case, we have to re-transmit the
	 * TID RDMA WRITE request.
	 */
	if (rcv_type == RHF_RCV_TYPE_EAGER) {
		hfi1_restart_rc(qp, qp->s_last_psn + 1, 1);
		hfi1_schedule_send(qp);
		goto done_unlock;
	}

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
		goto done_unlock;
	}

	/*
	 * Error out the qp for TID RDMA WRITE
	 */
	hfi1_kern_clear_hw_flow(qpriv->rcd, qp);
	for (i = 0; i < rvt_max_atomic(rdi); i++) {
		e = &qp->s_ack_queue[i];
		if (e->opcode == TID_OP(WRITE_REQ)) {
			req = ack_to_tid_req(e);
			hfi1_kern_exp_rcv_clear_all(req);
		}
	}
	spin_unlock(&qp->s_lock);
	rvt_rc_error(qp, IB_WC_LOC_LEN_ERR);
	goto done;

done_unlock:
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
	struct hfi1_qp_priv *qpriv;
	unsigned long flags;
	bool ret = true;
	struct rvt_ack_entry *e;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;

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
		goto r_unlock;
	}

	/*
	 * qp->s_tail_ack_queue points to the rvt_ack_entry currently being
	 * processed. These a completed sequentially so we can be sure that
	 * the pointer will not change until the entire request has completed.
	 */
	spin_lock(&qp->s_lock);
	qpriv = qp->priv;
	e = &qp->s_ack_queue[qpriv->r_tid_tail];
	req = ack_to_tid_req(e);
	flow = &req->flows[req->clear_tail];
	trace_hfi1_eflags_err_write(qp, rcv_type, rte, psn);
	trace_hfi1_rsp_handle_kdeth_eflags(qp, psn);
	trace_hfi1_tid_write_rsp_handle_kdeth_eflags(qp);
	trace_hfi1_tid_req_handle_kdeth_eflags(qp, 0, e->opcode, e->psn,
					       e->lpsn, req);
	trace_hfi1_tid_flow_handle_kdeth_eflags(qp, req->clear_tail, flow);

	switch (rcv_type) {
	case RHF_RCV_TYPE_EXPECTED:
		switch (rte) {
		case RHF_RTE_EXPECTED_FLOW_SEQ_ERR:
			if (!(qpriv->s_flags & HFI1_R_TID_SW_PSN)) {
				u64 reg;

				qpriv->s_flags |= HFI1_R_TID_SW_PSN;
				/*
				 * The only sane way to get the amount of
				 * progress is to read the HW flow state.
				 */
				reg = read_uctxt_csr(dd, rcd->ctxt,
						     RCV_TID_FLOW_TABLE +
						     (8 * flow->idx));
				flow->flow_state.r_next_psn = mask_psn(reg);
				qpriv->r_next_psn_kdeth =
					flow->flow_state.r_next_psn;
				goto nak_psn;
			} else {
				/*
				 * If the received PSN does not match the next
				 * expected PSN, NAK the packet.
				 * However, only do that if we know that the a
				 * NAK has already been sent. Otherwise, this
				 * mismatch could be due to packets that were
				 * already in flight.
				 */
				if (psn != flow->flow_state.r_next_psn) {
					psn = flow->flow_state.r_next_psn;
					goto nak_psn;
				}

				qpriv->s_nak_state = 0;
				/*
				 * If SW PSN verification is successful and this
				 * is the last packet in the segment, tell the
				 * caller to process it as a normal packet.
				 */
				if (psn == full_flow_psn(flow,
							 flow->flow_state.lpsn))
					ret = false;
				qpriv->r_next_psn_kdeth =
					++flow->flow_state.r_next_psn;
			}
			break;

		case RHF_RTE_EXPECTED_FLOW_GEN_ERR:
			goto nak_psn;

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

unlock:
	spin_unlock(&qp->s_lock);
r_unlock:
	spin_unlock_irqrestore(&qp->r_lock, flags);
rcu_unlock:
	rcu_read_unlock();
drop:
	return ret;
nak_psn:
	ibp->rvp.n_rc_seqnak++;
	if (!qpriv->s_nak_state) {
		qpriv->s_nak_state = IB_NAK_PSN_ERROR;
		/* We are NAK'ing the next expected PSN */
		qpriv->s_nak_psn = mask_psn(flow->flow_state.r_next_psn);
		qpriv->s_flags |= RVT_S_ACK_PENDING;
		if (qpriv->r_tid_ack == HFI1_QP_WQE_INVALID)
			qpriv->r_tid_ack = qpriv->r_tid_tail;
		hfi1_schedule_tid_send(qp);
	}
	goto unlock;
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
	struct hfi1_qp_priv *qpriv = qp->priv;
	int diff, delta_pkts;
	u32 tididx = 0, i;
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
		fidx = req->acked_tail;
		flow = &req->flows[fidx];
		*bth2 = mask_psn(req->r_ack_psn);
	}

	if (wqe->wr.opcode == IB_WR_TID_RDMA_READ)
		delta_pkts = delta_psn(*bth2, flow->flow_state.ib_spsn);
	else
		delta_pkts = delta_psn(*bth2,
				       full_flow_psn(flow,
						     flow->flow_state.spsn));

	trace_hfi1_tid_flow_restart_req(qp, fidx, flow);
	diff = delta_pkts + flow->resync_npkts;

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
	if (wqe->wr.opcode == IB_WR_TID_RDMA_WRITE) {
		rvt_skip_sge(&qpriv->tid_ss, (req->cur_seg * req->seg_len) +
			     flow->sent, 0);
		/*
		 * Packet PSN is based on flow_state.spsn + flow->pkt. However,
		 * during a RESYNC, the generation is incremented and the
		 * sequence is reset to 0. Since we've adjusted the npkts in the
		 * flow and the SGE has been sufficiently advanced, we have to
		 * adjust flow->pkt in order to calculate the correct PSN.
		 */
		flow->pkt -= flow->resync_npkts;
	}

	if (flow->tid_offset ==
	    EXP_TID_GET(flow->tid_entry[tididx], LEN) * PAGE_SIZE) {
		tididx++;
		flow->tid_offset = 0;
	}
	flow->tid_idx = tididx;
	if (wqe->wr.opcode == IB_WR_TID_RDMA_READ)
		/* Move flow_idx to correct index */
		req->flow_idx = fidx;
	else
		req->clear_tail = fidx;

	trace_hfi1_tid_flow_restart_req(qp, fidx, flow);
	trace_hfi1_tid_req_restart_req(qp, 0, wqe->wr.opcode, wqe->psn,
				       wqe->lpsn, req);
	req->state = TID_REQUEST_ACTIVE;
	if (wqe->wr.opcode == IB_WR_TID_RDMA_WRITE) {
		/* Reset all the flows that we are going to resend */
		fidx = CIRC_NEXT(fidx, MAX_FLOWS);
		i = qpriv->s_tid_tail;
		do {
			for (; CIRC_CNT(req->setup_head, fidx, MAX_FLOWS);
			      fidx = CIRC_NEXT(fidx, MAX_FLOWS)) {
				req->flows[fidx].sent = 0;
				req->flows[fidx].pkt = 0;
				req->flows[fidx].tid_idx = 0;
				req->flows[fidx].tid_offset = 0;
				req->flows[fidx].resync_npkts = 0;
			}
			if (i == qpriv->s_tid_cur)
				break;
			do {
				i = (++i == qp->s_size ? 0 : i);
				wqe = rvt_get_swqe_ptr(qp, i);
			} while (wqe->wr.opcode != IB_WR_TID_RDMA_WRITE);
			req = wqe_to_tid_req(wqe);
			req->cur_seg = req->ack_seg;
			fidx = req->acked_tail;
			/* Pull req->clear_tail back */
			req->clear_tail = fidx;
		} while (1);
	}
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
	for (i = qp->s_acked_ack_queue; i != qp->r_head_ack_queue;) {
		struct rvt_ack_entry *e = &qp->s_ack_queue[i];

		if (++i == rvt_max_atomic(ib_to_rvt(qp->ibqp.device)))
			i = 0;
		/* Free only locally allocated TID entries */
		if (e->opcode != TID_OP(WRITE_REQ))
			continue;
		do {
			struct hfi1_ack_priv *priv = e->priv;

			ret = hfi1_kern_exp_rcv_clear(&priv->tid_req);
		} while (!ret);
	}
}

bool hfi1_tid_rdma_wqe_interlock(struct rvt_qp *qp, struct rvt_swqe *wqe)
{
	struct rvt_swqe *prev;
	struct hfi1_qp_priv *priv = qp->priv;
	u32 s_prev;
	struct tid_rdma_request *req;

	s_prev = (qp->s_cur == 0 ? qp->s_size : qp->s_cur) - 1;
	prev = rvt_get_swqe_ptr(qp, s_prev);

	switch (wqe->wr.opcode) {
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_SEND_WITH_INV:
	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
	case IB_WR_RDMA_WRITE:
		switch (prev->wr.opcode) {
		case IB_WR_TID_RDMA_WRITE:
			req = wqe_to_tid_req(prev);
			if (req->ack_seg != req->total_segs)
				goto interlock;
		default:
			break;
		}
		break;
	case IB_WR_RDMA_READ:
		if (prev->wr.opcode != IB_WR_TID_RDMA_WRITE)
			break;
		/* fall through */
	case IB_WR_TID_RDMA_READ:
		switch (prev->wr.opcode) {
		case IB_WR_RDMA_READ:
			if (qp->s_acked != qp->s_cur)
				goto interlock;
			break;
		case IB_WR_TID_RDMA_WRITE:
			req = wqe_to_tid_req(prev);
			if (req->ack_seg != req->total_segs)
				goto interlock;
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
	} else if (wqe->wr.opcode == IB_WR_RDMA_WRITE) {
		/*
		 * TID RDMA is enabled for this RDMA WRITE request iff:
		 *   1. The remote address is page-aligned,
		 *   2. The length is larger than the minimum segment size,
		 *   3. The length is page-multiple.
		 */
		if (!(wqe->rdma_wr.remote_addr & ~PAGE_MASK) &&
		    !(wqe->length & ~PAGE_MASK)) {
			new_opcode = IB_WR_TID_RDMA_WRITE;
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
		} else {
			wqe->lpsn += priv->tid_req.total_segs - 1;
			atomic_inc(&qpriv->n_requests);
		}

		priv->tid_req.cur_seg = 0;
		priv->tid_req.comp_seg = 0;
		priv->tid_req.ack_seg = 0;
		priv->tid_req.state = TID_REQUEST_INACTIVE;
		/*
		 * Reset acked_tail.
		 * TID RDMA READ does not have ACKs so it does not
		 * update the pointer. We have to reset it so TID RDMA
		 * WRITE does not get confused.
		 */
		priv->tid_req.acked_tail = priv->tid_req.setup_head;
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

void hfi1_compute_tid_rdma_flow_wt(void)
{
	/*
	 * Heuristic for computing the RNR timeout when waiting on the flow
	 * queue. Rather than a computationaly expensive exact estimate of when
	 * a flow will be available, we assume that if a QP is at position N in
	 * the flow queue it has to wait approximately (N + 1) * (number of
	 * segments between two sync points), assuming PMTU of 4K. The rationale
	 * for this is that flows are released and recycled at each sync point.
	 */
	tid_rdma_flow_wt = MAX_TID_FLOW_PSN * enum_to_mtu(OPA_MTU_4096) /
		TID_RDMA_MAX_SEGMENT_SIZE;
}

static u32 position_in_queue(struct hfi1_qp_priv *qpriv,
			     struct tid_queue *queue)
{
	return qpriv->tid_enqueue - queue->dequeue;
}

/*
 * @qp: points to rvt_qp context.
 * @to_seg: desired RNR timeout in segments.
 * Return: index of the next highest timeout in the ib_hfi1_rnr_table[]
 */
static u32 hfi1_compute_tid_rnr_timeout(struct rvt_qp *qp, u32 to_seg)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	u64 timeout;
	u32 bytes_per_us;
	u8 i;

	bytes_per_us = active_egress_rate(qpriv->rcd->ppd) / 8;
	timeout = (to_seg * TID_RDMA_MAX_SEGMENT_SIZE) / bytes_per_us;
	/*
	 * Find the next highest value in the RNR table to the required
	 * timeout. This gives the responder some padding.
	 */
	for (i = 1; i <= IB_AETH_CREDIT_MASK; i++)
		if (rvt_rnr_tbl_to_usec(i) >= timeout)
			return i;
	return 0;
}

/**
 * Central place for resource allocation at TID write responder,
 * is called from write_req and write_data interrupt handlers as
 * well as the send thread when a queued QP is scheduled for
 * resource allocation.
 *
 * Iterates over (a) segments of a request and then (b) queued requests
 * themselves to allocate resources for up to local->max_write
 * segments across multiple requests. Stop allocating when we
 * hit a sync point, resume allocating after data packets at
 * sync point have been received.
 *
 * Resource allocation and sending of responses is decoupled. The
 * request/segment which are being allocated and sent are as follows.
 * Resources are allocated for:
 *     [request: qpriv->r_tid_alloc, segment: req->alloc_seg]
 * The send thread sends:
 *     [request: qp->s_tail_ack_queue, segment:req->cur_seg]
 */
static void hfi1_tid_write_alloc_resources(struct rvt_qp *qp, bool intr_ctx)
{
	struct tid_rdma_request *req;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct hfi1_ctxtdata *rcd = qpriv->rcd;
	struct tid_rdma_params *local = &qpriv->tid_rdma.local;
	struct rvt_ack_entry *e;
	u32 npkts, to_seg;
	bool last;
	int ret = 0;

	lockdep_assert_held(&qp->s_lock);

	while (1) {
		trace_hfi1_rsp_tid_write_alloc_res(qp, 0);
		trace_hfi1_tid_write_rsp_alloc_res(qp);
		/*
		 * Don't allocate more segments if a RNR NAK has already been
		 * scheduled to avoid messing up qp->r_psn: the RNR NAK will
		 * be sent only when all allocated segments have been sent.
		 * However, if more segments are allocated before that, TID RDMA
		 * WRITE RESP packets will be sent out for these new segments
		 * before the RNR NAK packet. When the requester receives the
		 * RNR NAK packet, it will restart with qp->s_last_psn + 1,
		 * which does not match qp->r_psn and will be dropped.
		 * Consequently, the requester will exhaust its retries and
		 * put the qp into error state.
		 */
		if (qpriv->rnr_nak_state == TID_RNR_NAK_SEND)
			break;

		/* No requests left to process */
		if (qpriv->r_tid_alloc == qpriv->r_tid_head) {
			/* If all data has been received, clear the flow */
			if (qpriv->flow_state.index < RXE_NUM_TID_FLOWS &&
			    !qpriv->alloc_w_segs)
				hfi1_kern_clear_hw_flow(rcd, qp);
			break;
		}

		e = &qp->s_ack_queue[qpriv->r_tid_alloc];
		if (e->opcode != TID_OP(WRITE_REQ))
			goto next_req;
		req = ack_to_tid_req(e);
		trace_hfi1_tid_req_write_alloc_res(qp, 0, e->opcode, e->psn,
						   e->lpsn, req);
		/* Finished allocating for all segments of this request */
		if (req->alloc_seg >= req->total_segs)
			goto next_req;

		/* Can allocate only a maximum of local->max_write for a QP */
		if (qpriv->alloc_w_segs >= local->max_write)
			break;

		/* Don't allocate at a sync point with data packets pending */
		if (qpriv->sync_pt && qpriv->alloc_w_segs)
			break;

		/* All data received at the sync point, continue */
		if (qpriv->sync_pt && !qpriv->alloc_w_segs) {
			hfi1_kern_clear_hw_flow(rcd, qp);
			qpriv->sync_pt = false;
			if (qpriv->s_flags & HFI1_R_TID_SW_PSN)
				qpriv->s_flags &= ~HFI1_R_TID_SW_PSN;
		}

		/* Allocate flow if we don't have one */
		if (qpriv->flow_state.index >= RXE_NUM_TID_FLOWS) {
			ret = hfi1_kern_setup_hw_flow(qpriv->rcd, qp);
			if (ret) {
				to_seg = tid_rdma_flow_wt *
					position_in_queue(qpriv,
							  &rcd->flow_queue);
				break;
			}
		}

		npkts = rvt_div_round_up_mtu(qp, req->seg_len);

		/*
		 * We are at a sync point if we run out of KDETH PSN space.
		 * Last PSN of every generation is reserved for RESYNC.
		 */
		if (qpriv->flow_state.psn + npkts > MAX_TID_FLOW_PSN - 1) {
			qpriv->sync_pt = true;
			break;
		}

		/*
		 * If overtaking req->acked_tail, send an RNR NAK. Because the
		 * QP is not queued in this case, and the issue can only be
		 * caused due a delay in scheduling the second leg which we
		 * cannot estimate, we use a rather arbitrary RNR timeout of
		 * (MAX_FLOWS / 2) segments
		 */
		if (!CIRC_SPACE(req->setup_head, req->acked_tail,
				MAX_FLOWS)) {
			ret = -EAGAIN;
			to_seg = MAX_FLOWS >> 1;
			qpriv->s_flags |= RVT_S_ACK_PENDING;
			hfi1_schedule_tid_send(qp);
			break;
		}

		/* Try to allocate rcv array / TID entries */
		ret = hfi1_kern_exp_rcv_setup(req, &req->ss, &last);
		if (ret == -EAGAIN)
			to_seg = position_in_queue(qpriv, &rcd->rarr_queue);
		if (ret)
			break;

		qpriv->alloc_w_segs++;
		req->alloc_seg++;
		continue;
next_req:
		/* Begin processing the next request */
		if (++qpriv->r_tid_alloc >
		    rvt_size_atomic(ib_to_rvt(qp->ibqp.device)))
			qpriv->r_tid_alloc = 0;
	}

	/*
	 * Schedule an RNR NAK to be sent if (a) flow or rcv array allocation
	 * has failed (b) we are called from the rcv handler interrupt context
	 * (c) an RNR NAK has not already been scheduled
	 */
	if (ret == -EAGAIN && intr_ctx && !qp->r_nak_state)
		goto send_rnr_nak;

	return;

send_rnr_nak:
	lockdep_assert_held(&qp->r_lock);

	/* Set r_nak_state to prevent unrelated events from generating NAK's */
	qp->r_nak_state = hfi1_compute_tid_rnr_timeout(qp, to_seg) | IB_RNR_NAK;

	/* Pull back r_psn to the segment being RNR NAK'd */
	qp->r_psn = e->psn + req->alloc_seg;
	qp->r_ack_psn = qp->r_psn;
	/*
	 * Pull back r_head_ack_queue to the ack entry following the request
	 * being RNR NAK'd. This allows resources to be allocated to the request
	 * if the queued QP is scheduled.
	 */
	qp->r_head_ack_queue = qpriv->r_tid_alloc + 1;
	if (qp->r_head_ack_queue > rvt_size_atomic(ib_to_rvt(qp->ibqp.device)))
		qp->r_head_ack_queue = 0;
	qpriv->r_tid_head = qp->r_head_ack_queue;
	/*
	 * These send side fields are used in make_rc_ack(). They are set in
	 * hfi1_send_rc_ack() but must be set here before dropping qp->s_lock
	 * for consistency
	 */
	qp->s_nak_state = qp->r_nak_state;
	qp->s_ack_psn = qp->r_ack_psn;
	/*
	 * Clear the ACK PENDING flag to prevent unwanted ACK because we
	 * have modified qp->s_ack_psn here.
	 */
	qp->s_flags &= ~(RVT_S_ACK_PENDING);

	trace_hfi1_rsp_tid_write_alloc_res(qp, qp->r_psn);
	/*
	 * qpriv->rnr_nak_state is used to determine when the scheduled RNR NAK
	 * has actually been sent. qp->s_flags RVT_S_ACK_PENDING bit cannot be
	 * used for this because qp->s_lock is dropped before calling
	 * hfi1_send_rc_ack() leading to inconsistency between the receive
	 * interrupt handlers and the send thread in make_rc_ack()
	 */
	qpriv->rnr_nak_state = TID_RNR_NAK_SEND;

	/*
	 * Schedule RNR NAK to be sent. RNR NAK's are scheduled from the receive
	 * interrupt handlers but will be sent from the send engine behind any
	 * previous responses that may have been scheduled
	 */
	rc_defered_ack(rcd, qp);
}

void hfi1_rc_rcv_tid_rdma_write_req(struct hfi1_packet *packet)
{
	/* HANDLER FOR TID RDMA WRITE REQUEST packet (Responder side)*/

	/*
	 * 1. Verify TID RDMA WRITE REQ as per IB_OPCODE_RC_RDMA_WRITE_FIRST
	 *    (see hfi1_rc_rcv())
	 *     - Don't allow 0-length requests.
	 * 2. Put TID RDMA WRITE REQ into the response queueu (s_ack_queue)
	 *     - Setup struct tid_rdma_req with request info
	 *     - Prepare struct tid_rdma_flow array?
	 * 3. Set the qp->s_ack_state as state diagram in design doc.
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
	struct tid_rdma_request *req;
	u32 bth0, psn, len, rkey, num_segs;
	bool is_fecn;
	u8 next;
	u64 vaddr;
	int diff;

	bth0 = be32_to_cpu(ohdr->bth[0]);
	if (hfi1_ruc_check_hdr(ibp, packet))
		return;

	is_fecn = process_ecn(qp, packet);
	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	trace_hfi1_rsp_rcv_tid_write_req(qp, psn);

	if (qp->state == IB_QPS_RTR && !(qp->r_flags & RVT_R_COMM_EST))
		rvt_comm_est(qp);

	if (unlikely(!(qp->qp_access_flags & IB_ACCESS_REMOTE_WRITE)))
		goto nack_inv;

	reth = &ohdr->u.tid_rdma.w_req.reth;
	vaddr = be64_to_cpu(reth->vaddr);
	len = be32_to_cpu(reth->length);

	num_segs = DIV_ROUND_UP(len, qpriv->tid_rdma.local.max_len);
	diff = delta_psn(psn, qp->r_psn);
	if (unlikely(diff)) {
		if (tid_rdma_rcv_error(packet, ohdr, qp, psn, diff))
			return;
		goto send_ack;
	}

	/*
	 * The resent request which was previously RNR NAK'd is inserted at the
	 * location of the original request, which is one entry behind
	 * r_head_ack_queue
	 */
	if (qpriv->rnr_nak_state)
		qp->r_head_ack_queue = qp->r_head_ack_queue ?
			qp->r_head_ack_queue - 1 :
			rvt_size_atomic(ib_to_rvt(qp->ibqp.device));

	/* We've verified the request, insert it into the ack queue. */
	next = qp->r_head_ack_queue + 1;
	if (next > rvt_size_atomic(ib_to_rvt(qp->ibqp.device)))
		next = 0;
	spin_lock_irqsave(&qp->s_lock, flags);
	if (unlikely(next == qp->s_acked_ack_queue)) {
		if (!qp->s_ack_queue[next].sent)
			goto nack_inv_unlock;
		update_ack_queue(qp, next);
	}
	e = &qp->s_ack_queue[qp->r_head_ack_queue];
	req = ack_to_tid_req(e);

	/* Bring previously RNR NAK'd request back to life */
	if (qpriv->rnr_nak_state) {
		qp->r_nak_state = 0;
		qp->s_nak_state = 0;
		qpriv->rnr_nak_state = TID_RNR_NAK_INIT;
		qp->r_psn = e->lpsn + 1;
		req->state = TID_REQUEST_INIT;
		goto update_head;
	}

	if (e->rdma_sge.mr) {
		rvt_put_mr(e->rdma_sge.mr);
		e->rdma_sge.mr = NULL;
	}

	/* The length needs to be in multiples of PAGE_SIZE */
	if (!len || len & ~PAGE_MASK)
		goto nack_inv_unlock;

	rkey = be32_to_cpu(reth->rkey);
	qp->r_len = len;

	if (e->opcode == TID_OP(WRITE_REQ) &&
	    (req->setup_head != req->clear_tail ||
	     req->clear_tail != req->acked_tail))
		goto nack_inv_unlock;

	if (unlikely(!rvt_rkey_ok(qp, &e->rdma_sge, qp->r_len, vaddr,
				  rkey, IB_ACCESS_REMOTE_WRITE)))
		goto nack_acc;

	qp->r_psn += num_segs - 1;

	e->opcode = (bth0 >> 24) & 0xff;
	e->psn = psn;
	e->lpsn = qp->r_psn;
	e->sent = 0;

	req->n_flows = min_t(u16, num_segs, qpriv->tid_rdma.local.max_write);
	req->state = TID_REQUEST_INIT;
	req->cur_seg = 0;
	req->comp_seg = 0;
	req->ack_seg = 0;
	req->alloc_seg = 0;
	req->isge = 0;
	req->seg_len = qpriv->tid_rdma.local.max_len;
	req->total_len = len;
	req->total_segs = num_segs;
	req->r_flow_psn = e->psn;
	req->ss.sge = e->rdma_sge;
	req->ss.num_sge = 1;

	req->flow_idx = req->setup_head;
	req->clear_tail = req->setup_head;
	req->acked_tail = req->setup_head;

	qp->r_state = e->opcode;
	qp->r_nak_state = 0;
	/*
	 * We need to increment the MSN here instead of when we
	 * finish sending the result since a duplicate request would
	 * increment it more than once.
	 */
	qp->r_msn++;
	qp->r_psn++;

	trace_hfi1_tid_req_rcv_write_req(qp, 0, e->opcode, e->psn, e->lpsn,
					 req);

	if (qpriv->r_tid_tail == HFI1_QP_WQE_INVALID) {
		qpriv->r_tid_tail = qp->r_head_ack_queue;
	} else if (qpriv->r_tid_tail == qpriv->r_tid_head) {
		struct tid_rdma_request *ptr;

		e = &qp->s_ack_queue[qpriv->r_tid_tail];
		ptr = ack_to_tid_req(e);

		if (e->opcode != TID_OP(WRITE_REQ) ||
		    ptr->comp_seg == ptr->total_segs) {
			if (qpriv->r_tid_tail == qpriv->r_tid_ack)
				qpriv->r_tid_ack = qp->r_head_ack_queue;
			qpriv->r_tid_tail = qp->r_head_ack_queue;
		}
	}
update_head:
	qp->r_head_ack_queue = next;
	qpriv->r_tid_head = qp->r_head_ack_queue;

	hfi1_tid_write_alloc_resources(qp, true);
	trace_hfi1_tid_write_rsp_rcv_req(qp);

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
	qp->r_nak_state = IB_NAK_INVALID_REQUEST;
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

u32 hfi1_build_tid_rdma_write_resp(struct rvt_qp *qp, struct rvt_ack_entry *e,
				   struct ib_other_headers *ohdr, u32 *bth1,
				   u32 bth2, u32 *len,
				   struct rvt_sge_state **ss)
{
	struct hfi1_ack_priv *epriv = e->priv;
	struct tid_rdma_request *req = &epriv->tid_req;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_rdma_flow *flow = NULL;
	u32 resp_len = 0, hdwords = 0;
	void *resp_addr = NULL;
	struct tid_rdma_params *remote;

	trace_hfi1_tid_req_build_write_resp(qp, 0, e->opcode, e->psn, e->lpsn,
					    req);
	trace_hfi1_tid_write_rsp_build_resp(qp);
	trace_hfi1_rsp_build_tid_write_resp(qp, bth2);
	flow = &req->flows[req->flow_idx];
	switch (req->state) {
	default:
		/*
		 * Try to allocate resources here in case QP was queued and was
		 * later scheduled when resources became available
		 */
		hfi1_tid_write_alloc_resources(qp, false);

		/* We've already sent everything which is ready */
		if (req->cur_seg >= req->alloc_seg)
			goto done;

		/*
		 * Resources can be assigned but responses cannot be sent in
		 * rnr_nak state, till the resent request is received
		 */
		if (qpriv->rnr_nak_state == TID_RNR_NAK_SENT)
			goto done;

		req->state = TID_REQUEST_ACTIVE;
		trace_hfi1_tid_flow_build_write_resp(qp, req->flow_idx, flow);
		req->flow_idx = CIRC_NEXT(req->flow_idx, MAX_FLOWS);
		hfi1_add_tid_reap_timer(qp);
		break;

	case TID_REQUEST_RESEND_ACTIVE:
	case TID_REQUEST_RESEND:
		trace_hfi1_tid_flow_build_write_resp(qp, req->flow_idx, flow);
		req->flow_idx = CIRC_NEXT(req->flow_idx, MAX_FLOWS);
		if (!CIRC_CNT(req->setup_head, req->flow_idx, MAX_FLOWS))
			req->state = TID_REQUEST_ACTIVE;

		hfi1_mod_tid_reap_timer(qp);
		break;
	}
	flow->flow_state.resp_ib_psn = bth2;
	resp_addr = (void *)flow->tid_entry;
	resp_len = sizeof(*flow->tid_entry) * flow->tidcnt;
	req->cur_seg++;

	memset(&ohdr->u.tid_rdma.w_rsp, 0, sizeof(ohdr->u.tid_rdma.w_rsp));
	epriv->ss.sge.vaddr = resp_addr;
	epriv->ss.sge.sge_length = resp_len;
	epriv->ss.sge.length = epriv->ss.sge.sge_length;
	/*
	 * We can safely zero these out. Since the first SGE covers the
	 * entire packet, nothing else should even look at the MR.
	 */
	epriv->ss.sge.mr = NULL;
	epriv->ss.sge.m = 0;
	epriv->ss.sge.n = 0;

	epriv->ss.sg_list = NULL;
	epriv->ss.total_len = epriv->ss.sge.sge_length;
	epriv->ss.num_sge = 1;

	*ss = &epriv->ss;
	*len = epriv->ss.total_len;

	/* Construct the TID RDMA WRITE RESP packet header */
	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);

	KDETH_RESET(ohdr->u.tid_rdma.w_rsp.kdeth0, KVER, 0x1);
	KDETH_RESET(ohdr->u.tid_rdma.w_rsp.kdeth1, JKEY, remote->jkey);
	ohdr->u.tid_rdma.w_rsp.aeth = rvt_compute_aeth(qp);
	ohdr->u.tid_rdma.w_rsp.tid_flow_psn =
		cpu_to_be32((flow->flow_state.generation <<
			     HFI1_KDETH_BTH_SEQ_SHIFT) |
			    (flow->flow_state.spsn &
			     HFI1_KDETH_BTH_SEQ_MASK));
	ohdr->u.tid_rdma.w_rsp.tid_flow_qp =
		cpu_to_be32(qpriv->tid_rdma.local.qp |
			    ((flow->idx & TID_RDMA_DESTQP_FLOW_MASK) <<
			     TID_RDMA_DESTQP_FLOW_SHIFT) |
			    qpriv->rcd->ctxt);
	ohdr->u.tid_rdma.w_rsp.verbs_qp = cpu_to_be32(qp->remote_qpn);
	*bth1 = remote->qp;
	rcu_read_unlock();
	hdwords = sizeof(ohdr->u.tid_rdma.w_rsp) / sizeof(u32);
	qpriv->pending_tid_w_segs++;
done:
	return hdwords;
}

static void hfi1_add_tid_reap_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = qp->priv;

	lockdep_assert_held(&qp->s_lock);
	if (!(qpriv->s_flags & HFI1_R_TID_RSC_TIMER)) {
		qpriv->s_flags |= HFI1_R_TID_RSC_TIMER;
		qpriv->s_tid_timer.expires = jiffies +
			qpriv->tid_timer_timeout_jiffies;
		add_timer(&qpriv->s_tid_timer);
	}
}

static void hfi1_mod_tid_reap_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = qp->priv;

	lockdep_assert_held(&qp->s_lock);
	qpriv->s_flags |= HFI1_R_TID_RSC_TIMER;
	mod_timer(&qpriv->s_tid_timer, jiffies +
		  qpriv->tid_timer_timeout_jiffies);
}

static int hfi1_stop_tid_reap_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	int rval = 0;

	lockdep_assert_held(&qp->s_lock);
	if (qpriv->s_flags & HFI1_R_TID_RSC_TIMER) {
		rval = del_timer(&qpriv->s_tid_timer);
		qpriv->s_flags &= ~HFI1_R_TID_RSC_TIMER;
	}
	return rval;
}

void hfi1_del_tid_reap_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *qpriv = qp->priv;

	del_timer_sync(&qpriv->s_tid_timer);
	qpriv->s_flags &= ~HFI1_R_TID_RSC_TIMER;
}

static void hfi1_tid_timeout(struct timer_list *t)
{
	struct hfi1_qp_priv *qpriv = from_timer(qpriv, t, s_tid_timer);
	struct rvt_qp *qp = qpriv->owner;
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);
	unsigned long flags;
	u32 i;

	spin_lock_irqsave(&qp->r_lock, flags);
	spin_lock(&qp->s_lock);
	if (qpriv->s_flags & HFI1_R_TID_RSC_TIMER) {
		dd_dev_warn(dd_from_ibdev(qp->ibqp.device), "[QP%u] %s %d\n",
			    qp->ibqp.qp_num, __func__, __LINE__);
		trace_hfi1_msg_tid_timeout(/* msg */
			qp, "resource timeout = ",
			(u64)qpriv->tid_timer_timeout_jiffies);
		hfi1_stop_tid_reap_timer(qp);
		/*
		 * Go though the entire ack queue and clear any outstanding
		 * HW flow and RcvArray resources.
		 */
		hfi1_kern_clear_hw_flow(qpriv->rcd, qp);
		for (i = 0; i < rvt_max_atomic(rdi); i++) {
			struct tid_rdma_request *req =
				ack_to_tid_req(&qp->s_ack_queue[i]);

			hfi1_kern_exp_rcv_clear_all(req);
		}
		spin_unlock(&qp->s_lock);
		if (qp->ibqp.event_handler) {
			struct ib_event ev;

			ev.device = qp->ibqp.device;
			ev.element.qp = &qp->ibqp;
			ev.event = IB_EVENT_QP_FATAL;
			qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
		}
		rvt_rc_error(qp, IB_WC_RESP_TIMEOUT_ERR);
		goto unlock_r_lock;
	}
	spin_unlock(&qp->s_lock);
unlock_r_lock:
	spin_unlock_irqrestore(&qp->r_lock, flags);
}

void hfi1_rc_rcv_tid_rdma_write_resp(struct hfi1_packet *packet)
{
	/* HANDLER FOR TID RDMA WRITE RESPONSE packet (Requestor side */

	/*
	 * 1. Find matching SWQE
	 * 2. Check that TIDENTRY array has enough space for a complete
	 *    segment. If not, put QP in error state.
	 * 3. Save response data in struct tid_rdma_req and struct tid_rdma_flow
	 * 4. Remove HFI1_S_WAIT_TID_RESP from s_flags.
	 * 5. Set qp->s_state
	 * 6. Kick the send engine (hfi1_schedule_send())
	 */
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct rvt_swqe *wqe;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	enum ib_wc_status status;
	u32 opcode, aeth, psn, flow_psn, i, tidlen = 0, pktlen;
	bool is_fecn;
	unsigned long flags;

	is_fecn = process_ecn(qp, packet);
	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	aeth = be32_to_cpu(ohdr->u.tid_rdma.w_rsp.aeth);
	opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;

	spin_lock_irqsave(&qp->s_lock, flags);

	/* Ignore invalid responses */
	if (cmp_psn(psn, qp->s_next_psn) >= 0)
		goto ack_done;

	/* Ignore duplicate responses. */
	if (unlikely(cmp_psn(psn, qp->s_last_psn) <= 0))
		goto ack_done;

	if (unlikely(qp->s_acked == qp->s_tail))
		goto ack_done;

	/*
	 * If we are waiting for a particular packet sequence number
	 * due to a request being resent, check for it. Otherwise,
	 * ensure that we haven't missed anything.
	 */
	if (qp->r_flags & RVT_R_RDMAR_SEQ) {
		if (cmp_psn(psn, qp->s_last_psn + 1) != 0)
			goto ack_done;
		qp->r_flags &= ~RVT_R_RDMAR_SEQ;
	}

	wqe = rvt_get_swqe_ptr(qp, qpriv->s_tid_cur);
	if (unlikely(wqe->wr.opcode != IB_WR_TID_RDMA_WRITE))
		goto ack_op_err;

	req = wqe_to_tid_req(wqe);
	/*
	 * If we've lost ACKs and our acked_tail pointer is too far
	 * behind, don't overwrite segments. Just drop the packet and
	 * let the reliability protocol take care of it.
	 */
	if (!CIRC_SPACE(req->setup_head, req->acked_tail, MAX_FLOWS))
		goto ack_done;

	/*
	 * The call to do_rc_ack() should be last in the chain of
	 * packet checks because it will end up updating the QP state.
	 * Therefore, anything that would prevent the packet from
	 * being accepted as a successful response should be prior
	 * to it.
	 */
	if (!do_rc_ack(qp, aeth, psn, opcode, 0, rcd))
		goto ack_done;

	trace_hfi1_ack(qp, psn);

	flow = &req->flows[req->setup_head];
	flow->pkt = 0;
	flow->tid_idx = 0;
	flow->tid_offset = 0;
	flow->sent = 0;
	flow->resync_npkts = 0;
	flow->tid_qpn = be32_to_cpu(ohdr->u.tid_rdma.w_rsp.tid_flow_qp);
	flow->idx = (flow->tid_qpn >> TID_RDMA_DESTQP_FLOW_SHIFT) &
		TID_RDMA_DESTQP_FLOW_MASK;
	flow_psn = mask_psn(be32_to_cpu(ohdr->u.tid_rdma.w_rsp.tid_flow_psn));
	flow->flow_state.generation = flow_psn >> HFI1_KDETH_BTH_SEQ_SHIFT;
	flow->flow_state.spsn = flow_psn & HFI1_KDETH_BTH_SEQ_MASK;
	flow->flow_state.resp_ib_psn = psn;
	flow->length = min_t(u32, req->seg_len,
			     (wqe->length - (req->comp_seg * req->seg_len)));

	flow->npkts = rvt_div_round_up_mtu(qp, flow->length);
	flow->flow_state.lpsn = flow->flow_state.spsn +
		flow->npkts - 1;
	/* payload length = packet length - (header length + ICRC length) */
	pktlen = packet->tlen - (packet->hlen + 4);
	if (pktlen > sizeof(flow->tid_entry)) {
		status = IB_WC_LOC_LEN_ERR;
		goto ack_err;
	}
	memcpy(flow->tid_entry, packet->ebuf, pktlen);
	flow->tidcnt = pktlen / sizeof(*flow->tid_entry);
	trace_hfi1_tid_flow_rcv_write_resp(qp, req->setup_head, flow);

	req->comp_seg++;
	trace_hfi1_tid_write_sender_rcv_resp(qp, 0);
	/*
	 * Walk the TID_ENTRY list to make sure we have enough space for a
	 * complete segment.
	 */
	for (i = 0; i < flow->tidcnt; i++) {
		trace_hfi1_tid_entry_rcv_write_resp(/* entry */
			qp, i, flow->tid_entry[i]);
		if (!EXP_TID_GET(flow->tid_entry[i], LEN)) {
			status = IB_WC_LOC_LEN_ERR;
			goto ack_err;
		}
		tidlen += EXP_TID_GET(flow->tid_entry[i], LEN);
	}
	if (tidlen * PAGE_SIZE < flow->length) {
		status = IB_WC_LOC_LEN_ERR;
		goto ack_err;
	}

	trace_hfi1_tid_req_rcv_write_resp(qp, 0, wqe->wr.opcode, wqe->psn,
					  wqe->lpsn, req);
	/*
	 * If this is the first response for this request, set the initial
	 * flow index to the current flow.
	 */
	if (!cmp_psn(psn, wqe->psn)) {
		req->r_last_acked = mask_psn(wqe->psn - 1);
		/* Set acked flow index to head index */
		req->acked_tail = req->setup_head;
	}

	/* advance circular buffer head */
	req->setup_head = CIRC_NEXT(req->setup_head, MAX_FLOWS);
	req->state = TID_REQUEST_ACTIVE;

	/*
	 * If all responses for this TID RDMA WRITE request have been received
	 * advance the pointer to the next one.
	 * Since TID RDMA requests could be mixed in with regular IB requests,
	 * they might not appear sequentially in the queue. Therefore, the
	 * next request needs to be "found".
	 */
	if (qpriv->s_tid_cur != qpriv->s_tid_head &&
	    req->comp_seg == req->total_segs) {
		for (i = qpriv->s_tid_cur + 1; ; i++) {
			if (i == qp->s_size)
				i = 0;
			wqe = rvt_get_swqe_ptr(qp, i);
			if (i == qpriv->s_tid_head)
				break;
			if (wqe->wr.opcode == IB_WR_TID_RDMA_WRITE)
				break;
		}
		qpriv->s_tid_cur = i;
	}
	qp->s_flags &= ~HFI1_S_WAIT_TID_RESP;

	hfi1_schedule_tid_send(qp);
	goto ack_done;

ack_op_err:
	status = IB_WC_LOC_QP_OP_ERR;
ack_err:
	rvt_error_qp(qp, status);
ack_done:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	if (is_fecn)
		hfi1_send_rc_ack(packet, is_fecn);
}

bool hfi1_build_tid_rdma_packet(struct rvt_swqe *wqe,
				struct ib_other_headers *ohdr,
				u32 *bth1, u32 *bth2, u32 *len)
{
	struct tid_rdma_request *req = wqe_to_tid_req(wqe);
	struct tid_rdma_flow *flow = &req->flows[req->clear_tail];
	struct tid_rdma_params *remote;
	struct rvt_qp *qp = req->qp;
	struct hfi1_qp_priv *qpriv = qp->priv;
	u32 tidentry = flow->tid_entry[flow->tid_idx];
	u32 tidlen = EXP_TID_GET(tidentry, LEN) << PAGE_SHIFT;
	struct tid_rdma_write_data *wd = &ohdr->u.tid_rdma.w_data;
	u32 next_offset, om = KDETH_OM_LARGE;
	bool last_pkt;

	if (!tidlen) {
		hfi1_trdma_send_complete(qp, wqe, IB_WC_REM_INV_RD_REQ_ERR);
		rvt_error_qp(qp, IB_WC_REM_INV_RD_REQ_ERR);
	}

	*len = min_t(u32, qp->pmtu, tidlen - flow->tid_offset);
	flow->sent += *len;
	next_offset = flow->tid_offset + *len;
	last_pkt = (flow->tid_idx == (flow->tidcnt - 1) &&
		    next_offset >= tidlen) || (flow->sent >= flow->length);
	trace_hfi1_tid_entry_build_write_data(qp, flow->tid_idx, tidentry);
	trace_hfi1_tid_flow_build_write_data(qp, req->clear_tail, flow);

	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);
	KDETH_RESET(wd->kdeth0, KVER, 0x1);
	KDETH_SET(wd->kdeth0, SH, !last_pkt);
	KDETH_SET(wd->kdeth0, INTR, !!(!last_pkt && remote->urg));
	KDETH_SET(wd->kdeth0, TIDCTRL, EXP_TID_GET(tidentry, CTRL));
	KDETH_SET(wd->kdeth0, TID, EXP_TID_GET(tidentry, IDX));
	KDETH_SET(wd->kdeth0, OM, om == KDETH_OM_LARGE);
	KDETH_SET(wd->kdeth0, OFFSET, flow->tid_offset / om);
	KDETH_RESET(wd->kdeth1, JKEY, remote->jkey);
	wd->verbs_qp = cpu_to_be32(qp->remote_qpn);
	rcu_read_unlock();

	*bth1 = flow->tid_qpn;
	*bth2 = mask_psn(((flow->flow_state.spsn + flow->pkt++) &
			 HFI1_KDETH_BTH_SEQ_MASK) |
			 (flow->flow_state.generation <<
			  HFI1_KDETH_BTH_SEQ_SHIFT));
	if (last_pkt) {
		/* PSNs are zero-based, so +1 to count number of packets */
		if (flow->flow_state.lpsn + 1 +
		    rvt_div_round_up_mtu(qp, req->seg_len) >
		    MAX_TID_FLOW_PSN)
			req->state = TID_REQUEST_SYNC;
		*bth2 |= IB_BTH_REQ_ACK;
	}

	if (next_offset >= tidlen) {
		flow->tid_offset = 0;
		flow->tid_idx++;
	} else {
		flow->tid_offset = next_offset;
	}
	return last_pkt;
}

void hfi1_rc_rcv_tid_rdma_write_data(struct hfi1_packet *packet)
{
	struct rvt_qp *qp = packet->qp;
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ctxtdata *rcd = priv->rcd;
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_ack_entry *e;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	unsigned long flags;
	u32 psn, next;
	u8 opcode;

	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0xff;

	/*
	 * All error handling should be done by now. If we are here, the packet
	 * is either good or been accepted by the error handler.
	 */
	spin_lock_irqsave(&qp->s_lock, flags);
	e = &qp->s_ack_queue[priv->r_tid_tail];
	req = ack_to_tid_req(e);
	flow = &req->flows[req->clear_tail];
	if (cmp_psn(psn, full_flow_psn(flow, flow->flow_state.lpsn))) {
		if (cmp_psn(psn, flow->flow_state.r_next_psn))
			goto send_nak;
		flow->flow_state.r_next_psn++;
		goto exit;
	}
	flow->flow_state.r_next_psn = mask_psn(psn + 1);
	hfi1_kern_exp_rcv_clear(req);
	priv->alloc_w_segs--;
	rcd->flows[flow->idx].psn = psn & HFI1_KDETH_BTH_SEQ_MASK;
	req->comp_seg++;
	priv->s_nak_state = 0;

	/*
	 * Release the flow if one of the following conditions has been met:
	 *  - The request has reached a sync point AND all outstanding
	 *    segments have been completed, or
	 *  - The entire request is complete and there are no more requests
	 *    (of any kind) in the queue.
	 */
	trace_hfi1_rsp_rcv_tid_write_data(qp, psn);
	trace_hfi1_tid_req_rcv_write_data(qp, 0, e->opcode, e->psn, e->lpsn,
					  req);
	trace_hfi1_tid_write_rsp_rcv_data(qp);
	if (priv->r_tid_ack == HFI1_QP_WQE_INVALID)
		priv->r_tid_ack = priv->r_tid_tail;

	if (opcode == TID_OP(WRITE_DATA_LAST)) {
		for (next = priv->r_tid_tail + 1; ; next++) {
			if (next > rvt_size_atomic(&dev->rdi))
				next = 0;
			if (next == priv->r_tid_head)
				break;
			e = &qp->s_ack_queue[next];
			if (e->opcode == TID_OP(WRITE_REQ))
				break;
		}
		priv->r_tid_tail = next;
		if (++qp->s_acked_ack_queue > rvt_size_atomic(&dev->rdi))
			qp->s_acked_ack_queue = 0;
	}

	hfi1_tid_write_alloc_resources(qp, true);

	/*
	 * If we need to generate more responses, schedule the
	 * send engine.
	 */
	if (req->cur_seg < req->total_segs ||
	    qp->s_tail_ack_queue != qp->r_head_ack_queue) {
		qp->s_flags |= RVT_S_RESP_PENDING;
		hfi1_schedule_send(qp);
	}

	priv->pending_tid_w_segs--;
	if (priv->s_flags & HFI1_R_TID_RSC_TIMER) {
		if (priv->pending_tid_w_segs)
			hfi1_mod_tid_reap_timer(req->qp);
		else
			hfi1_stop_tid_reap_timer(req->qp);
	}

done:
	priv->s_flags |= RVT_S_ACK_PENDING;
	hfi1_schedule_tid_send(qp);
exit:
	priv->r_next_psn_kdeth = flow->flow_state.r_next_psn;
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return;

send_nak:
	if (!priv->s_nak_state) {
		priv->s_nak_state = IB_NAK_PSN_ERROR;
		priv->s_nak_psn = flow->flow_state.r_next_psn;
		priv->s_flags |= RVT_S_ACK_PENDING;
		if (priv->r_tid_ack == HFI1_QP_WQE_INVALID)
			priv->r_tid_ack = priv->r_tid_tail;
		hfi1_schedule_tid_send(qp);
	}
	goto done;
}

static bool hfi1_tid_rdma_is_resync_psn(u32 psn)
{
	return (bool)((psn & HFI1_KDETH_BTH_SEQ_MASK) ==
		      HFI1_KDETH_BTH_SEQ_MASK);
}

u32 hfi1_build_tid_rdma_write_ack(struct rvt_qp *qp, struct rvt_ack_entry *e,
				  struct ib_other_headers *ohdr, u16 iflow,
				  u32 *bth1, u32 *bth2)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_flow_state *fs = &qpriv->flow_state;
	struct tid_rdma_request *req = ack_to_tid_req(e);
	struct tid_rdma_flow *flow = &req->flows[iflow];
	struct tid_rdma_params *remote;

	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);
	KDETH_RESET(ohdr->u.tid_rdma.ack.kdeth1, JKEY, remote->jkey);
	ohdr->u.tid_rdma.ack.verbs_qp = cpu_to_be32(qp->remote_qpn);
	*bth1 = remote->qp;
	rcu_read_unlock();

	if (qpriv->resync) {
		*bth2 = mask_psn((fs->generation <<
				  HFI1_KDETH_BTH_SEQ_SHIFT) - 1);
		ohdr->u.tid_rdma.ack.aeth = rvt_compute_aeth(qp);
	} else if (qpriv->s_nak_state) {
		*bth2 = mask_psn(qpriv->s_nak_psn);
		ohdr->u.tid_rdma.ack.aeth =
			cpu_to_be32((qp->r_msn & IB_MSN_MASK) |
				    (qpriv->s_nak_state <<
				     IB_AETH_CREDIT_SHIFT));
	} else {
		*bth2 = full_flow_psn(flow, flow->flow_state.lpsn);
		ohdr->u.tid_rdma.ack.aeth = rvt_compute_aeth(qp);
	}
	KDETH_RESET(ohdr->u.tid_rdma.ack.kdeth0, KVER, 0x1);
	ohdr->u.tid_rdma.ack.tid_flow_qp =
		cpu_to_be32(qpriv->tid_rdma.local.qp |
			    ((flow->idx & TID_RDMA_DESTQP_FLOW_MASK) <<
			     TID_RDMA_DESTQP_FLOW_SHIFT) |
			    qpriv->rcd->ctxt);

	ohdr->u.tid_rdma.ack.tid_flow_psn = 0;
	ohdr->u.tid_rdma.ack.verbs_psn =
		cpu_to_be32(flow->flow_state.resp_ib_psn);

	if (qpriv->resync) {
		/*
		 * If the PSN before the current expect KDETH PSN is the
		 * RESYNC PSN, then we never received a good TID RDMA WRITE
		 * DATA packet after a previous RESYNC.
		 * In this case, the next expected KDETH PSN stays the same.
		 */
		if (hfi1_tid_rdma_is_resync_psn(qpriv->r_next_psn_kdeth - 1)) {
			ohdr->u.tid_rdma.ack.tid_flow_psn =
				cpu_to_be32(qpriv->r_next_psn_kdeth_save);
		} else {
			/*
			 * Because the KDETH PSNs jump during a RESYNC, it's
			 * not possible to infer (or compute) the previous value
			 * of r_next_psn_kdeth in the case of back-to-back
			 * RESYNC packets. Therefore, we save it.
			 */
			qpriv->r_next_psn_kdeth_save =
				qpriv->r_next_psn_kdeth - 1;
			ohdr->u.tid_rdma.ack.tid_flow_psn =
				cpu_to_be32(qpriv->r_next_psn_kdeth_save);
			qpriv->r_next_psn_kdeth = mask_psn(*bth2 + 1);
		}
		qpriv->resync = false;
	}

	return sizeof(ohdr->u.tid_rdma.ack) / sizeof(u32);
}

void hfi1_rc_rcv_tid_rdma_ack(struct hfi1_packet *packet)
{
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct rvt_swqe *wqe;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	u32 aeth, psn, req_psn, ack_psn, fspsn, resync_psn, ack_kpsn;
	bool is_fecn;
	unsigned long flags;
	u16 fidx;

	trace_hfi1_tid_write_sender_rcv_tid_ack(qp, 0);
	is_fecn = process_ecn(qp, packet);
	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
	aeth = be32_to_cpu(ohdr->u.tid_rdma.ack.aeth);
	req_psn = mask_psn(be32_to_cpu(ohdr->u.tid_rdma.ack.verbs_psn));
	resync_psn = mask_psn(be32_to_cpu(ohdr->u.tid_rdma.ack.tid_flow_psn));

	spin_lock_irqsave(&qp->s_lock, flags);
	trace_hfi1_rcv_tid_ack(qp, aeth, psn, req_psn, resync_psn);

	/* If we are waiting for an ACK to RESYNC, drop any other packets */
	if ((qp->s_flags & HFI1_S_WAIT_HALT) &&
	    cmp_psn(psn, qpriv->s_resync_psn))
		goto ack_op_err;

	ack_psn = req_psn;
	if (hfi1_tid_rdma_is_resync_psn(psn))
		ack_kpsn = resync_psn;
	else
		ack_kpsn = psn;
	if (aeth >> 29) {
		ack_psn--;
		ack_kpsn--;
	}

	wqe = rvt_get_swqe_ptr(qp, qp->s_acked);

	if (wqe->wr.opcode != IB_WR_TID_RDMA_WRITE)
		goto ack_op_err;

	req = wqe_to_tid_req(wqe);
	trace_hfi1_tid_req_rcv_tid_ack(qp, 0, wqe->wr.opcode, wqe->psn,
				       wqe->lpsn, req);
	flow = &req->flows[req->acked_tail];
	trace_hfi1_tid_flow_rcv_tid_ack(qp, req->acked_tail, flow);

	/* Drop stale ACK/NAK */
	if (cmp_psn(psn, full_flow_psn(flow, flow->flow_state.spsn)) < 0)
		goto ack_op_err;

	while (cmp_psn(ack_kpsn,
		       full_flow_psn(flow, flow->flow_state.lpsn)) >= 0 &&
	       req->ack_seg < req->cur_seg) {
		req->ack_seg++;
		/* advance acked segment pointer */
		req->acked_tail = CIRC_NEXT(req->acked_tail, MAX_FLOWS);
		req->r_last_acked = flow->flow_state.resp_ib_psn;
		trace_hfi1_tid_req_rcv_tid_ack(qp, 0, wqe->wr.opcode, wqe->psn,
					       wqe->lpsn, req);
		if (req->ack_seg == req->total_segs) {
			req->state = TID_REQUEST_COMPLETE;
			wqe = do_rc_completion(qp, wqe,
					       to_iport(qp->ibqp.device,
							qp->port_num));
			trace_hfi1_sender_rcv_tid_ack(qp);
			atomic_dec(&qpriv->n_tid_requests);
			if (qp->s_acked == qp->s_tail)
				break;
			if (wqe->wr.opcode != IB_WR_TID_RDMA_WRITE)
				break;
			req = wqe_to_tid_req(wqe);
		}
		flow = &req->flows[req->acked_tail];
		trace_hfi1_tid_flow_rcv_tid_ack(qp, req->acked_tail, flow);
	}

	trace_hfi1_tid_req_rcv_tid_ack(qp, 0, wqe->wr.opcode, wqe->psn,
				       wqe->lpsn, req);
	switch (aeth >> 29) {
	case 0:         /* ACK */
		if (qpriv->s_flags & RVT_S_WAIT_ACK)
			qpriv->s_flags &= ~RVT_S_WAIT_ACK;
		if (!hfi1_tid_rdma_is_resync_psn(psn)) {
			/* Check if there is any pending TID ACK */
			if (wqe->wr.opcode == IB_WR_TID_RDMA_WRITE &&
			    req->ack_seg < req->cur_seg)
				hfi1_mod_tid_retry_timer(qp);
			else
				hfi1_stop_tid_retry_timer(qp);
			hfi1_schedule_send(qp);
		} else {
			u32 spsn, fpsn, last_acked, generation;
			struct tid_rdma_request *rptr;

			/* ACK(RESYNC) */
			hfi1_stop_tid_retry_timer(qp);
			/* Allow new requests (see hfi1_make_tid_rdma_pkt) */
			qp->s_flags &= ~HFI1_S_WAIT_HALT;
			/*
			 * Clear RVT_S_SEND_ONE flag in case that the TID RDMA
			 * ACK is received after the TID retry timer is fired
			 * again. In this case, do not send any more TID
			 * RESYNC request or wait for any more TID ACK packet.
			 */
			qpriv->s_flags &= ~RVT_S_SEND_ONE;
			hfi1_schedule_send(qp);

			if ((qp->s_acked == qpriv->s_tid_tail &&
			     req->ack_seg == req->total_segs) ||
			    qp->s_acked == qp->s_tail) {
				qpriv->s_state = TID_OP(WRITE_DATA_LAST);
				goto done;
			}

			if (req->ack_seg == req->comp_seg) {
				qpriv->s_state = TID_OP(WRITE_DATA);
				goto done;
			}

			/*
			 * The PSN to start with is the next PSN after the
			 * RESYNC PSN.
			 */
			psn = mask_psn(psn + 1);
			generation = psn >> HFI1_KDETH_BTH_SEQ_SHIFT;
			spsn = 0;

			/*
			 * Update to the correct WQE when we get an ACK(RESYNC)
			 * in the middle of a request.
			 */
			if (delta_psn(ack_psn, wqe->lpsn))
				wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
			req = wqe_to_tid_req(wqe);
			flow = &req->flows[req->acked_tail];
			/*
			 * RESYNC re-numbers the PSN ranges of all remaining
			 * segments. Also, PSN's start from 0 in the middle of a
			 * segment and the first segment size is less than the
			 * default number of packets. flow->resync_npkts is used
			 * to track the number of packets from the start of the
			 * real segment to the point of 0 PSN after the RESYNC
			 * in order to later correctly rewind the SGE.
			 */
			fpsn = full_flow_psn(flow, flow->flow_state.spsn);
			req->r_ack_psn = psn;
			flow->resync_npkts +=
				delta_psn(mask_psn(resync_psn + 1), fpsn);
			/*
			 * Renumber all packet sequence number ranges
			 * based on the new generation.
			 */
			last_acked = qp->s_acked;
			rptr = req;
			while (1) {
				/* start from last acked segment */
				for (fidx = rptr->acked_tail;
				     CIRC_CNT(rptr->setup_head, fidx,
					      MAX_FLOWS);
				     fidx = CIRC_NEXT(fidx, MAX_FLOWS)) {
					u32 lpsn;
					u32 gen;

					flow = &rptr->flows[fidx];
					gen = flow->flow_state.generation;
					if (WARN_ON(gen == generation &&
						    flow->flow_state.spsn !=
						     spsn))
						continue;
					lpsn = flow->flow_state.lpsn;
					lpsn = full_flow_psn(flow, lpsn);
					flow->npkts =
						delta_psn(lpsn,
							  mask_psn(resync_psn)
							  );
					flow->flow_state.generation =
						generation;
					flow->flow_state.spsn = spsn;
					flow->flow_state.lpsn =
						flow->flow_state.spsn +
						flow->npkts - 1;
					flow->pkt = 0;
					spsn += flow->npkts;
					resync_psn += flow->npkts;
					trace_hfi1_tid_flow_rcv_tid_ack(qp,
									fidx,
									flow);
				}
				if (++last_acked == qpriv->s_tid_cur + 1)
					break;
				if (last_acked == qp->s_size)
					last_acked = 0;
				wqe = rvt_get_swqe_ptr(qp, last_acked);
				rptr = wqe_to_tid_req(wqe);
			}
			req->cur_seg = req->ack_seg;
			qpriv->s_tid_tail = qp->s_acked;
			qpriv->s_state = TID_OP(WRITE_REQ);
			hfi1_schedule_tid_send(qp);
		}
done:
		qpriv->s_retry = qp->s_retry_cnt;
		break;

	case 3:         /* NAK */
		hfi1_stop_tid_retry_timer(qp);
		switch ((aeth >> IB_AETH_CREDIT_SHIFT) &
			IB_AETH_CREDIT_MASK) {
		case 0: /* PSN sequence error */
			flow = &req->flows[req->acked_tail];
			fspsn = full_flow_psn(flow, flow->flow_state.spsn);
			trace_hfi1_tid_flow_rcv_tid_ack(qp, req->acked_tail,
							flow);
			req->r_ack_psn = mask_psn(be32_to_cpu(ohdr->bth[2]));
			req->cur_seg = req->ack_seg;
			qpriv->s_tid_tail = qp->s_acked;
			qpriv->s_state = TID_OP(WRITE_REQ);
			qpriv->s_retry = qp->s_retry_cnt;
			hfi1_schedule_tid_send(qp);
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}

ack_op_err:
	spin_unlock_irqrestore(&qp->s_lock, flags);
}

void hfi1_add_tid_retry_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_qp *ibqp = &qp->ibqp;
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);

	lockdep_assert_held(&qp->s_lock);
	if (!(priv->s_flags & HFI1_S_TID_RETRY_TIMER)) {
		priv->s_flags |= HFI1_S_TID_RETRY_TIMER;
		priv->s_tid_retry_timer.expires = jiffies +
			priv->tid_retry_timeout_jiffies + rdi->busy_jiffies;
		add_timer(&priv->s_tid_retry_timer);
	}
}

static void hfi1_mod_tid_retry_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_qp *ibqp = &qp->ibqp;
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);

	lockdep_assert_held(&qp->s_lock);
	priv->s_flags |= HFI1_S_TID_RETRY_TIMER;
	mod_timer(&priv->s_tid_retry_timer, jiffies +
		  priv->tid_retry_timeout_jiffies + rdi->busy_jiffies);
}

static int hfi1_stop_tid_retry_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	int rval = 0;

	lockdep_assert_held(&qp->s_lock);
	if (priv->s_flags & HFI1_S_TID_RETRY_TIMER) {
		rval = del_timer(&priv->s_tid_retry_timer);
		priv->s_flags &= ~HFI1_S_TID_RETRY_TIMER;
	}
	return rval;
}

void hfi1_del_tid_retry_timer(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	del_timer_sync(&priv->s_tid_retry_timer);
	priv->s_flags &= ~HFI1_S_TID_RETRY_TIMER;
}

static void hfi1_tid_retry_timeout(struct timer_list *t)
{
	struct hfi1_qp_priv *priv = from_timer(priv, t, s_tid_retry_timer);
	struct rvt_qp *qp = priv->owner;
	struct rvt_swqe *wqe;
	unsigned long flags;
	struct tid_rdma_request *req;

	spin_lock_irqsave(&qp->r_lock, flags);
	spin_lock(&qp->s_lock);
	trace_hfi1_tid_write_sender_retry_timeout(qp, 0);
	if (priv->s_flags & HFI1_S_TID_RETRY_TIMER) {
		hfi1_stop_tid_retry_timer(qp);
		if (!priv->s_retry) {
			trace_hfi1_msg_tid_retry_timeout(/* msg */
				qp,
				"Exhausted retries. Tid retry timeout = ",
				(u64)priv->tid_retry_timeout_jiffies);

			wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
			hfi1_trdma_send_complete(qp, wqe, IB_WC_RETRY_EXC_ERR);
			rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);
		} else {
			wqe = rvt_get_swqe_ptr(qp, qp->s_acked);
			req = wqe_to_tid_req(wqe);
			trace_hfi1_tid_req_tid_retry_timeout(/* req */
			   qp, 0, wqe->wr.opcode, wqe->psn, wqe->lpsn, req);

			priv->s_flags &= ~RVT_S_WAIT_ACK;
			/* Only send one packet (the RESYNC) */
			priv->s_flags |= RVT_S_SEND_ONE;
			/*
			 * No additional request shall be made by this QP until
			 * the RESYNC has been complete.
			 */
			qp->s_flags |= HFI1_S_WAIT_HALT;
			priv->s_state = TID_OP(RESYNC);
			priv->s_retry--;
			hfi1_schedule_tid_send(qp);
		}
	}
	spin_unlock(&qp->s_lock);
	spin_unlock_irqrestore(&qp->r_lock, flags);
}

u32 hfi1_build_tid_rdma_resync(struct rvt_qp *qp, struct rvt_swqe *wqe,
			       struct ib_other_headers *ohdr, u32 *bth1,
			       u32 *bth2, u16 fidx)
{
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct tid_rdma_params *remote;
	struct tid_rdma_request *req = wqe_to_tid_req(wqe);
	struct tid_rdma_flow *flow = &req->flows[fidx];
	u32 generation;

	rcu_read_lock();
	remote = rcu_dereference(qpriv->tid_rdma.remote);
	KDETH_RESET(ohdr->u.tid_rdma.ack.kdeth1, JKEY, remote->jkey);
	ohdr->u.tid_rdma.ack.verbs_qp = cpu_to_be32(qp->remote_qpn);
	*bth1 = remote->qp;
	rcu_read_unlock();

	generation = kern_flow_generation_next(flow->flow_state.generation);
	*bth2 = mask_psn((generation << HFI1_KDETH_BTH_SEQ_SHIFT) - 1);
	qpriv->s_resync_psn = *bth2;
	*bth2 |= IB_BTH_REQ_ACK;
	KDETH_RESET(ohdr->u.tid_rdma.ack.kdeth0, KVER, 0x1);

	return sizeof(ohdr->u.tid_rdma.resync) / sizeof(u32);
}

void hfi1_rc_rcv_tid_rdma_resync(struct hfi1_packet *packet)
{
	struct ib_other_headers *ohdr = packet->ohdr;
	struct rvt_qp *qp = packet->qp;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct hfi1_ctxtdata *rcd = qpriv->rcd;
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	struct rvt_ack_entry *e;
	struct tid_rdma_request *req;
	struct tid_rdma_flow *flow;
	struct tid_flow_state *fs = &qpriv->flow_state;
	u32 psn, generation, idx, gen_next;
	bool is_fecn;
	unsigned long flags;

	is_fecn = process_ecn(qp, packet);
	psn = mask_psn(be32_to_cpu(ohdr->bth[2]));

	generation = mask_psn(psn + 1) >> HFI1_KDETH_BTH_SEQ_SHIFT;
	spin_lock_irqsave(&qp->s_lock, flags);

	gen_next = (fs->generation == KERN_GENERATION_RESERVED) ?
		generation : kern_flow_generation_next(fs->generation);
	/*
	 * RESYNC packet contains the "next" generation and can only be
	 * from the current or previous generations
	 */
	if (generation != mask_generation(gen_next - 1) &&
	    generation != gen_next)
		goto bail;
	/* Already processing a resync */
	if (qpriv->resync)
		goto bail;

	spin_lock(&rcd->exp_lock);
	if (fs->index >= RXE_NUM_TID_FLOWS) {
		/*
		 * If we don't have a flow, save the generation so it can be
		 * applied when a new flow is allocated
		 */
		fs->generation = generation;
	} else {
		/* Reprogram the QP flow with new generation */
		rcd->flows[fs->index].generation = generation;
		fs->generation = kern_setup_hw_flow(rcd, fs->index);
	}
	fs->psn = 0;
	/*
	 * Disable SW PSN checking since a RESYNC is equivalent to a
	 * sync point and the flow has/will be reprogrammed
	 */
	qpriv->s_flags &= ~HFI1_R_TID_SW_PSN;
	trace_hfi1_tid_write_rsp_rcv_resync(qp);

	/*
	 * Reset all TID flow information with the new generation.
	 * This is done for all requests and segments after the
	 * last received segment
	 */
	for (idx = qpriv->r_tid_tail; ; idx++) {
		u16 flow_idx;

		if (idx > rvt_size_atomic(&dev->rdi))
			idx = 0;
		e = &qp->s_ack_queue[idx];
		if (e->opcode == TID_OP(WRITE_REQ)) {
			req = ack_to_tid_req(e);
			trace_hfi1_tid_req_rcv_resync(qp, 0, e->opcode, e->psn,
						      e->lpsn, req);

			/* start from last unacked segment */
			for (flow_idx = req->clear_tail;
			     CIRC_CNT(req->setup_head, flow_idx,
				      MAX_FLOWS);
			     flow_idx = CIRC_NEXT(flow_idx, MAX_FLOWS)) {
				u32 lpsn;
				u32 next;

				flow = &req->flows[flow_idx];
				lpsn = full_flow_psn(flow,
						     flow->flow_state.lpsn);
				next = flow->flow_state.r_next_psn;
				flow->npkts = delta_psn(lpsn, next - 1);
				flow->flow_state.generation = fs->generation;
				flow->flow_state.spsn = fs->psn;
				flow->flow_state.lpsn =
					flow->flow_state.spsn + flow->npkts - 1;
				flow->flow_state.r_next_psn =
					full_flow_psn(flow,
						      flow->flow_state.spsn);
				fs->psn += flow->npkts;
				trace_hfi1_tid_flow_rcv_resync(qp, flow_idx,
							       flow);
			}
		}
		if (idx == qp->s_tail_ack_queue)
			break;
	}

	spin_unlock(&rcd->exp_lock);
	qpriv->resync = true;
	/* RESYNC request always gets a TID RDMA ACK. */
	qpriv->s_nak_state = 0;
	qpriv->s_flags |= RVT_S_ACK_PENDING;
	hfi1_schedule_tid_send(qp);
bail:
	spin_unlock_irqrestore(&qp->s_lock, flags);
}

/*
 * Call this function when the last TID RDMA WRITE DATA packet for a request
 * is built.
 */
static void update_tid_tail(struct rvt_qp *qp)
	__must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *priv = qp->priv;
	u32 i;
	struct rvt_swqe *wqe;

	lockdep_assert_held(&qp->s_lock);
	/* Can't move beyond s_tid_cur */
	if (priv->s_tid_tail == priv->s_tid_cur)
		return;
	for (i = priv->s_tid_tail + 1; ; i++) {
		if (i == qp->s_size)
			i = 0;

		if (i == priv->s_tid_cur)
			break;
		wqe = rvt_get_swqe_ptr(qp, i);
		if (wqe->wr.opcode == IB_WR_TID_RDMA_WRITE)
			break;
	}
	priv->s_tid_tail = i;
	priv->s_state = TID_OP(WRITE_RESP);
}

int hfi1_make_tid_rdma_pkt(struct rvt_qp *qp, struct hfi1_pkt_state *ps)
	__must_hold(&qp->s_lock)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct rvt_swqe *wqe;
	u32 bth1 = 0, bth2 = 0, hwords = 5, len, middle = 0;
	struct ib_other_headers *ohdr;
	struct rvt_sge_state *ss = &qp->s_sge;
	struct rvt_ack_entry *e = &qp->s_ack_queue[qp->s_tail_ack_queue];
	struct tid_rdma_request *req = ack_to_tid_req(e);
	bool last = false;
	u8 opcode = TID_OP(WRITE_DATA);

	lockdep_assert_held(&qp->s_lock);
	trace_hfi1_tid_write_sender_make_tid_pkt(qp, 0);
	/*
	 * Prioritize the sending of the requests and responses over the
	 * sending of the TID RDMA data packets.
	 */
	if (((atomic_read(&priv->n_tid_requests) < HFI1_TID_RDMA_WRITE_CNT) &&
	     atomic_read(&priv->n_requests) &&
	     !(qp->s_flags & (RVT_S_BUSY | RVT_S_WAIT_ACK |
			     HFI1_S_ANY_WAIT_IO))) ||
	    (e->opcode == TID_OP(WRITE_REQ) && req->cur_seg < req->alloc_seg &&
	     !(qp->s_flags & (RVT_S_BUSY | HFI1_S_ANY_WAIT_IO)))) {
		struct iowait_work *iowork;

		iowork = iowait_get_ib_work(&priv->s_iowait);
		ps->s_txreq = get_waiting_verbs_txreq(iowork);
		if (ps->s_txreq || hfi1_make_rc_req(qp, ps)) {
			priv->s_flags |= HFI1_S_TID_BUSY_SET;
			return 1;
		}
	}

	ps->s_txreq = get_txreq(ps->dev, qp);
	if (!ps->s_txreq)
		goto bail_no_tx;

	ohdr = &ps->s_txreq->phdr.hdr.ibh.u.oth;

	if ((priv->s_flags & RVT_S_ACK_PENDING) &&
	    make_tid_rdma_ack(qp, ohdr, ps))
		return 1;

	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_SEND_OK)) {
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
		clear_ahg(qp);
		wqe = rvt_get_swqe_ptr(qp, qp->s_last);
		hfi1_trdma_send_complete(qp, wqe, qp->s_last != qp->s_acked ?
					 IB_WC_SUCCESS : IB_WC_WR_FLUSH_ERR);
		/* will get called again */
		goto done_free_tx;
	}

	if (priv->s_flags & RVT_S_WAIT_ACK)
		goto bail;

	/* Check whether there is anything to do. */
	if (priv->s_tid_tail == HFI1_QP_WQE_INVALID)
		goto bail;
	wqe = rvt_get_swqe_ptr(qp, priv->s_tid_tail);
	req = wqe_to_tid_req(wqe);
	trace_hfi1_tid_req_make_tid_pkt(qp, 0, wqe->wr.opcode, wqe->psn,
					wqe->lpsn, req);
	switch (priv->s_state) {
	case TID_OP(WRITE_REQ):
	case TID_OP(WRITE_RESP):
		priv->tid_ss.sge = wqe->sg_list[0];
		priv->tid_ss.sg_list = wqe->sg_list + 1;
		priv->tid_ss.num_sge = wqe->wr.num_sge;
		priv->tid_ss.total_len = wqe->length;

		if (priv->s_state == TID_OP(WRITE_REQ))
			hfi1_tid_rdma_restart_req(qp, wqe, &bth2);
		priv->s_state = TID_OP(WRITE_DATA);
		/* fall through */

	case TID_OP(WRITE_DATA):
		/*
		 * 1. Check whether TID RDMA WRITE RESP available.
		 * 2. If no:
		 *    2.1 If have more segments and no TID RDMA WRITE RESP,
		 *        set HFI1_S_WAIT_TID_RESP
		 *    2.2 Return indicating no progress made.
		 * 3. If yes:
		 *    3.1 Build TID RDMA WRITE DATA packet.
		 *    3.2 If last packet in segment:
		 *        3.2.1 Change KDETH header bits
		 *        3.2.2 Advance RESP pointers.
		 *    3.3 Return indicating progress made.
		 */
		trace_hfi1_sender_make_tid_pkt(qp);
		trace_hfi1_tid_write_sender_make_tid_pkt(qp, 0);
		wqe = rvt_get_swqe_ptr(qp, priv->s_tid_tail);
		req = wqe_to_tid_req(wqe);
		len = wqe->length;

		if (!req->comp_seg || req->cur_seg == req->comp_seg)
			goto bail;

		trace_hfi1_tid_req_make_tid_pkt(qp, 0, wqe->wr.opcode,
						wqe->psn, wqe->lpsn, req);
		last = hfi1_build_tid_rdma_packet(wqe, ohdr, &bth1, &bth2,
						  &len);

		if (last) {
			/* move pointer to next flow */
			req->clear_tail = CIRC_NEXT(req->clear_tail,
						    MAX_FLOWS);
			if (++req->cur_seg < req->total_segs) {
				if (!CIRC_CNT(req->setup_head, req->clear_tail,
					      MAX_FLOWS))
					qp->s_flags |= HFI1_S_WAIT_TID_RESP;
			} else {
				priv->s_state = TID_OP(WRITE_DATA_LAST);
				opcode = TID_OP(WRITE_DATA_LAST);

				/* Advance the s_tid_tail now */
				update_tid_tail(qp);
			}
		}
		hwords += sizeof(ohdr->u.tid_rdma.w_data) / sizeof(u32);
		ss = &priv->tid_ss;
		break;

	case TID_OP(RESYNC):
		trace_hfi1_sender_make_tid_pkt(qp);
		/* Use generation from the most recently received response */
		wqe = rvt_get_swqe_ptr(qp, priv->s_tid_cur);
		req = wqe_to_tid_req(wqe);
		/* If no responses for this WQE look at the previous one */
		if (!req->comp_seg) {
			wqe = rvt_get_swqe_ptr(qp,
					       (!priv->s_tid_cur ? qp->s_size :
						priv->s_tid_cur) - 1);
			req = wqe_to_tid_req(wqe);
		}
		hwords += hfi1_build_tid_rdma_resync(qp, wqe, ohdr, &bth1,
						     &bth2,
						     CIRC_PREV(req->setup_head,
							       MAX_FLOWS));
		ss = NULL;
		len = 0;
		opcode = TID_OP(RESYNC);
		break;

	default:
		goto bail;
	}
	if (priv->s_flags & RVT_S_SEND_ONE) {
		priv->s_flags &= ~RVT_S_SEND_ONE;
		priv->s_flags |= RVT_S_WAIT_ACK;
		bth2 |= IB_BTH_REQ_ACK;
	}
	qp->s_len -= len;
	ps->s_txreq->hdr_dwords = hwords;
	ps->s_txreq->sde = priv->s_sde;
	ps->s_txreq->ss = ss;
	ps->s_txreq->s_cur_size = len;
	hfi1_make_ruc_header(qp, ohdr, (opcode << 24), bth1, bth2,
			     middle, ps);
	return 1;
done_free_tx:
	hfi1_put_txreq(ps->s_txreq);
	ps->s_txreq = NULL;
	return 1;

bail:
	hfi1_put_txreq(ps->s_txreq);
bail_no_tx:
	ps->s_txreq = NULL;
	priv->s_flags &= ~RVT_S_BUSY;
	/*
	 * If we didn't get a txreq, the QP will be woken up later to try
	 * again, set the flags to the the wake up which work item to wake
	 * up.
	 * (A better algorithm should be found to do this and generalize the
	 * sleep/wakeup flags.)
	 */
	iowait_set_flag(&priv->s_iowait, IOWAIT_PENDING_TID);
	return 0;
}

static int make_tid_rdma_ack(struct rvt_qp *qp,
			     struct ib_other_headers *ohdr,
			     struct hfi1_pkt_state *ps)
{
	struct rvt_ack_entry *e;
	struct hfi1_qp_priv *qpriv = qp->priv;
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	u32 hwords, next;
	u32 len = 0;
	u32 bth1 = 0, bth2 = 0;
	int middle = 0;
	u16 flow;
	struct tid_rdma_request *req, *nreq;

	trace_hfi1_tid_write_rsp_make_tid_ack(qp);
	/* Don't send an ACK if we aren't supposed to. */
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK))
		goto bail;

	/* header size in 32-bit words LRH+BTH = (8+12)/4. */
	hwords = 5;

	e = &qp->s_ack_queue[qpriv->r_tid_ack];
	req = ack_to_tid_req(e);
	/*
	 * In the RESYNC case, we are exactly one segment past the
	 * previously sent ack or at the previously sent NAK. So to send
	 * the resync ack, we go back one segment (which might be part of
	 * the previous request) and let the do-while loop execute again.
	 * The advantage of executing the do-while loop is that any data
	 * received after the previous ack is automatically acked in the
	 * RESYNC ack. It turns out that for the do-while loop we only need
	 * to pull back qpriv->r_tid_ack, not the segment
	 * indices/counters. The scheme works even if the previous request
	 * was not a TID WRITE request.
	 */
	if (qpriv->resync) {
		if (!req->ack_seg || req->ack_seg == req->total_segs)
			qpriv->r_tid_ack = !qpriv->r_tid_ack ?
				rvt_size_atomic(&dev->rdi) :
				qpriv->r_tid_ack - 1;
		e = &qp->s_ack_queue[qpriv->r_tid_ack];
		req = ack_to_tid_req(e);
	}

	trace_hfi1_rsp_make_tid_ack(qp, e->psn);
	trace_hfi1_tid_req_make_tid_ack(qp, 0, e->opcode, e->psn, e->lpsn,
					req);
	/*
	 * If we've sent all the ACKs that we can, we are done
	 * until we get more segments...
	 */
	if (!qpriv->s_nak_state && !qpriv->resync &&
	    req->ack_seg == req->comp_seg)
		goto bail;

	do {
		/*
		 * To deal with coalesced ACKs, the acked_tail pointer
		 * into the flow array is used. The distance between it
		 * and the clear_tail is the number of flows that are
		 * being ACK'ed.
		 */
		req->ack_seg +=
			/* Get up-to-date value */
			CIRC_CNT(req->clear_tail, req->acked_tail,
				 MAX_FLOWS);
		/* Advance acked index */
		req->acked_tail = req->clear_tail;

		/*
		 * req->clear_tail points to the segment currently being
		 * received. So, when sending an ACK, the previous
		 * segment is being ACK'ed.
		 */
		flow = CIRC_PREV(req->acked_tail, MAX_FLOWS);
		if (req->ack_seg != req->total_segs)
			break;
		req->state = TID_REQUEST_COMPLETE;

		next = qpriv->r_tid_ack + 1;
		if (next > rvt_size_atomic(&dev->rdi))
			next = 0;
		qpriv->r_tid_ack = next;
		if (qp->s_ack_queue[next].opcode != TID_OP(WRITE_REQ))
			break;
		nreq = ack_to_tid_req(&qp->s_ack_queue[next]);
		if (!nreq->comp_seg || nreq->ack_seg == nreq->comp_seg)
			break;

		/* Move to the next ack entry now */
		e = &qp->s_ack_queue[qpriv->r_tid_ack];
		req = ack_to_tid_req(e);
	} while (1);

	/*
	 * At this point qpriv->r_tid_ack == qpriv->r_tid_tail but e and
	 * req could be pointing at the previous ack queue entry
	 */
	if (qpriv->s_nak_state ||
	    (qpriv->resync &&
	     !hfi1_tid_rdma_is_resync_psn(qpriv->r_next_psn_kdeth - 1) &&
	     (cmp_psn(qpriv->r_next_psn_kdeth - 1,
		      full_flow_psn(&req->flows[flow],
				    req->flows[flow].flow_state.lpsn)) > 0))) {
		/*
		 * A NAK will implicitly acknowledge all previous TID RDMA
		 * requests. Therefore, we NAK with the req->acked_tail
		 * segment for the request at qpriv->r_tid_ack (same at
		 * this point as the req->clear_tail segment for the
		 * qpriv->r_tid_tail request)
		 */
		e = &qp->s_ack_queue[qpriv->r_tid_ack];
		req = ack_to_tid_req(e);
		flow = req->acked_tail;
	} else if (req->ack_seg == req->total_segs &&
		   qpriv->s_flags & HFI1_R_TID_WAIT_INTERLCK)
		qpriv->s_flags &= ~HFI1_R_TID_WAIT_INTERLCK;

	trace_hfi1_tid_write_rsp_make_tid_ack(qp);
	trace_hfi1_tid_req_make_tid_ack(qp, 0, e->opcode, e->psn, e->lpsn,
					req);
	hwords += hfi1_build_tid_rdma_write_ack(qp, e, ohdr, flow, &bth1,
						&bth2);
	len = 0;
	qpriv->s_flags &= ~RVT_S_ACK_PENDING;
	ps->s_txreq->hdr_dwords = hwords;
	ps->s_txreq->sde = qpriv->s_sde;
	ps->s_txreq->s_cur_size = len;
	ps->s_txreq->ss = NULL;
	hfi1_make_ruc_header(qp, ohdr, (TID_OP(ACK) << 24), bth1, bth2, middle,
			     ps);
	ps->s_txreq->txreq.flags |= SDMA_TXREQ_F_VIP;
	return 1;
bail:
	/*
	 * Ensure s_rdma_ack_cnt changes are committed prior to resetting
	 * RVT_S_RESP_PENDING
	 */
	smp_wmb();
	qpriv->s_flags &= ~RVT_S_ACK_PENDING;
	return 0;
}

static int hfi1_send_tid_ok(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	return !(priv->s_flags & RVT_S_BUSY ||
		 qp->s_flags & HFI1_S_ANY_WAIT_IO) &&
		(verbs_txreq_queued(iowait_get_tid_work(&priv->s_iowait)) ||
		 (priv->s_flags & RVT_S_RESP_PENDING) ||
		 !(qp->s_flags & HFI1_S_ANY_TID_WAIT_SEND));
}

void _hfi1_do_tid_send(struct work_struct *work)
{
	struct iowait_work *w = container_of(work, struct iowait_work, iowork);
	struct rvt_qp *qp = iowait_to_qp(w->iow);

	hfi1_do_tid_send(qp);
}

static void hfi1_do_tid_send(struct rvt_qp *qp)
{
	struct hfi1_pkt_state ps;
	struct hfi1_qp_priv *priv = qp->priv;

	ps.dev = to_idev(qp->ibqp.device);
	ps.ibp = to_iport(qp->ibqp.device, qp->port_num);
	ps.ppd = ppd_from_ibp(ps.ibp);
	ps.wait = iowait_get_tid_work(&priv->s_iowait);
	ps.in_thread = false;
	ps.timeout_int = qp->timeout_jiffies / 8;

	trace_hfi1_rc_do_tid_send(qp, false);
	spin_lock_irqsave(&qp->s_lock, ps.flags);

	/* Return if we are already busy processing a work request. */
	if (!hfi1_send_tid_ok(qp)) {
		if (qp->s_flags & HFI1_S_ANY_WAIT_IO)
			iowait_set_flag(&priv->s_iowait, IOWAIT_PENDING_TID);
		spin_unlock_irqrestore(&qp->s_lock, ps.flags);
		return;
	}

	priv->s_flags |= RVT_S_BUSY;

	ps.timeout = jiffies + ps.timeout_int;
	ps.cpu = priv->s_sde ? priv->s_sde->cpu :
		cpumask_first(cpumask_of_node(ps.ppd->dd->node));
	ps.pkts_sent = false;

	/* insure a pre-built packet is handled  */
	ps.s_txreq = get_waiting_verbs_txreq(ps.wait);
	do {
		/* Check for a constructed packet to be sent. */
		if (ps.s_txreq) {
			if (priv->s_flags & HFI1_S_TID_BUSY_SET) {
				qp->s_flags |= RVT_S_BUSY;
				ps.wait = iowait_get_ib_work(&priv->s_iowait);
			}
			spin_unlock_irqrestore(&qp->s_lock, ps.flags);

			/*
			 * If the packet cannot be sent now, return and
			 * the send tasklet will be woken up later.
			 */
			if (hfi1_verbs_send(qp, &ps))
				return;

			/* allow other tasks to run */
			if (hfi1_schedule_send_yield(qp, &ps, true))
				return;

			spin_lock_irqsave(&qp->s_lock, ps.flags);
			if (priv->s_flags & HFI1_S_TID_BUSY_SET) {
				qp->s_flags &= ~RVT_S_BUSY;
				priv->s_flags &= ~HFI1_S_TID_BUSY_SET;
				ps.wait = iowait_get_tid_work(&priv->s_iowait);
				if (iowait_flag_set(&priv->s_iowait,
						    IOWAIT_PENDING_IB))
					hfi1_schedule_send(qp);
			}
		}
	} while (hfi1_make_tid_rdma_pkt(qp, &ps));
	iowait_starve_clear(ps.pkts_sent, &priv->s_iowait);
	spin_unlock_irqrestore(&qp->s_lock, ps.flags);
}

static bool _hfi1_schedule_tid_send(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ibport *ibp =
		to_iport(qp->ibqp.device, qp->port_num);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);

	return iowait_tid_schedule(&priv->s_iowait, ppd->hfi1_wq,
				   priv->s_sde ?
				   priv->s_sde->cpu :
				   cpumask_first(cpumask_of_node(dd->node)));
}

/**
 * hfi1_schedule_tid_send - schedule progress on TID RDMA state machine
 * @qp: the QP
 *
 * This schedules qp progress on the TID RDMA state machine. Caller
 * should hold the s_lock.
 * Unlike hfi1_schedule_send(), this cannot use hfi1_send_ok() because
 * the two state machines can step on each other with respect to the
 * RVT_S_BUSY flag.
 * Therefore, a modified test is used.
 * @return true if the second leg is scheduled;
 *  false if the second leg is not scheduled.
 */
bool hfi1_schedule_tid_send(struct rvt_qp *qp)
{
	lockdep_assert_held(&qp->s_lock);
	if (hfi1_send_tid_ok(qp)) {
		/*
		 * The following call returns true if the qp is not on the
		 * queue and false if the qp is already on the queue before
		 * this call. Either way, the qp will be on the queue when the
		 * call returns.
		 */
		_hfi1_schedule_tid_send(qp);
		return true;
	}
	if (qp->s_flags & HFI1_S_ANY_WAIT_IO)
		iowait_set_flag(&((struct hfi1_qp_priv *)qp->priv)->s_iowait,
				IOWAIT_PENDING_TID);
	return false;
}

bool hfi1_tid_rdma_ack_interlock(struct rvt_qp *qp, struct rvt_ack_entry *e)
{
	struct rvt_ack_entry *prev;
	struct tid_rdma_request *req;
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	struct hfi1_qp_priv *priv = qp->priv;
	u32 s_prev;

	s_prev = qp->s_tail_ack_queue == 0 ? rvt_size_atomic(&dev->rdi) :
		(qp->s_tail_ack_queue - 1);
	prev = &qp->s_ack_queue[s_prev];

	if ((e->opcode == TID_OP(READ_REQ) ||
	     e->opcode == OP(RDMA_READ_REQUEST)) &&
	    prev->opcode == TID_OP(WRITE_REQ)) {
		req = ack_to_tid_req(prev);
		if (req->ack_seg != req->total_segs) {
			priv->s_flags |= HFI1_R_TID_WAIT_INTERLCK;
			return true;
		}
	}
	return false;
}
