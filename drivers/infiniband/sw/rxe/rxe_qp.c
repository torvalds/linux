/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	   Redistribution and use in source and binary forms, with or
 *	   without modification, are permitted provided that the following
 *	   conditions are met:
 *
 *		- Redistributions of source code must retain the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer.
 *
 *		- Redistributions in binary form must reproduce the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer in the documentation and/or other materials
 *		  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"
#include "rxe_task.h"

static int rxe_qp_chk_cap(struct rxe_dev *rxe, struct ib_qp_cap *cap,
			  int has_srq)
{
	if (cap->max_send_wr > rxe->attr.max_qp_wr) {
		pr_warn("invalid send wr = %d > %d\n",
			cap->max_send_wr, rxe->attr.max_qp_wr);
		goto err1;
	}

	if (cap->max_send_sge > rxe->attr.max_send_sge) {
		pr_warn("invalid send sge = %d > %d\n",
			cap->max_send_sge, rxe->attr.max_send_sge);
		goto err1;
	}

	if (!has_srq) {
		if (cap->max_recv_wr > rxe->attr.max_qp_wr) {
			pr_warn("invalid recv wr = %d > %d\n",
				cap->max_recv_wr, rxe->attr.max_qp_wr);
			goto err1;
		}

		if (cap->max_recv_sge > rxe->attr.max_recv_sge) {
			pr_warn("invalid recv sge = %d > %d\n",
				cap->max_recv_sge, rxe->attr.max_recv_sge);
			goto err1;
		}
	}

	if (cap->max_inline_data > rxe->max_inline_data) {
		pr_warn("invalid max inline data = %d > %d\n",
			cap->max_inline_data, rxe->max_inline_data);
		goto err1;
	}

	return 0;

err1:
	return -EINVAL;
}

int rxe_qp_chk_init(struct rxe_dev *rxe, struct ib_qp_init_attr *init)
{
	struct ib_qp_cap *cap = &init->cap;
	struct rxe_port *port;
	int port_num = init->port_num;

	if (!init->recv_cq || !init->send_cq) {
		pr_warn("missing cq\n");
		goto err1;
	}

	if (rxe_qp_chk_cap(rxe, cap, !!init->srq))
		goto err1;

	if (init->qp_type == IB_QPT_SMI || init->qp_type == IB_QPT_GSI) {
		if (!rdma_is_port_valid(&rxe->ib_dev, port_num)) {
			pr_warn("invalid port = %d\n", port_num);
			goto err1;
		}

		port = &rxe->port;

		if (init->qp_type == IB_QPT_SMI && port->qp_smi_index) {
			pr_warn("SMI QP exists for port %d\n", port_num);
			goto err1;
		}

		if (init->qp_type == IB_QPT_GSI && port->qp_gsi_index) {
			pr_warn("GSI QP exists for port %d\n", port_num);
			goto err1;
		}
	}

	return 0;

err1:
	return -EINVAL;
}

static int alloc_rd_atomic_resources(struct rxe_qp *qp, unsigned int n)
{
	qp->resp.res_head = 0;
	qp->resp.res_tail = 0;
	qp->resp.resources = kcalloc(n, sizeof(struct resp_res), GFP_KERNEL);

	if (!qp->resp.resources)
		return -ENOMEM;

	return 0;
}

static void free_rd_atomic_resources(struct rxe_qp *qp)
{
	if (qp->resp.resources) {
		int i;

		for (i = 0; i < qp->attr.max_dest_rd_atomic; i++) {
			struct resp_res *res = &qp->resp.resources[i];

			free_rd_atomic_resource(qp, res);
		}
		kfree(qp->resp.resources);
		qp->resp.resources = NULL;
	}
}

void free_rd_atomic_resource(struct rxe_qp *qp, struct resp_res *res)
{
	if (res->type == RXE_ATOMIC_MASK) {
		rxe_drop_ref(qp);
		kfree_skb(res->atomic.skb);
	} else if (res->type == RXE_READ_MASK) {
		if (res->read.mr)
			rxe_drop_ref(res->read.mr);
	}
	res->type = 0;
}

static void cleanup_rd_atomic_resources(struct rxe_qp *qp)
{
	int i;
	struct resp_res *res;

	if (qp->resp.resources) {
		for (i = 0; i < qp->attr.max_dest_rd_atomic; i++) {
			res = &qp->resp.resources[i];
			free_rd_atomic_resource(qp, res);
		}
	}
}

static void rxe_qp_init_misc(struct rxe_dev *rxe, struct rxe_qp *qp,
			     struct ib_qp_init_attr *init)
{
	struct rxe_port *port;
	u32 qpn;

	qp->sq_sig_type		= init->sq_sig_type;
	qp->attr.path_mtu	= 1;
	qp->mtu			= ib_mtu_enum_to_int(qp->attr.path_mtu);

	qpn			= qp->pelem.index;
	port			= &rxe->port;

	switch (init->qp_type) {
	case IB_QPT_SMI:
		qp->ibqp.qp_num		= 0;
		port->qp_smi_index	= qpn;
		qp->attr.port_num	= init->port_num;
		break;

	case IB_QPT_GSI:
		qp->ibqp.qp_num		= 1;
		port->qp_gsi_index	= qpn;
		qp->attr.port_num	= init->port_num;
		break;

	default:
		qp->ibqp.qp_num		= qpn;
		break;
	}

	INIT_LIST_HEAD(&qp->grp_list);

	skb_queue_head_init(&qp->send_pkts);

	spin_lock_init(&qp->grp_lock);
	spin_lock_init(&qp->state_lock);

	atomic_set(&qp->ssn, 0);
	atomic_set(&qp->skb_out, 0);
}

static int rxe_qp_init_req(struct rxe_dev *rxe, struct rxe_qp *qp,
			   struct ib_qp_init_attr *init,
			   struct ib_ucontext *context,
			   struct rxe_create_qp_resp __user *uresp)
{
	int err;
	int wqe_size;

	err = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, 0, &qp->sk);
	if (err < 0)
		return err;
	qp->sk->sk->sk_user_data = qp;

	/* pick a source UDP port number for this QP based on
	 * the source QPN. this spreads traffic for different QPs
	 * across different NIC RX queues (while using a single
	 * flow for a given QP to maintain packet order).
	 * the port number must be in the Dynamic Ports range
	 * (0xc000 - 0xffff).
	 */
	qp->src_port = RXE_ROCE_V2_SPORT +
		(hash_32_generic(qp_num(qp), 14) & 0x3fff);

	qp->sq.max_wr		= init->cap.max_send_wr;
	qp->sq.max_sge		= init->cap.max_send_sge;
	qp->sq.max_inline	= init->cap.max_inline_data;

	wqe_size = max_t(int, sizeof(struct rxe_send_wqe) +
			 qp->sq.max_sge * sizeof(struct ib_sge),
			 sizeof(struct rxe_send_wqe) +
			 qp->sq.max_inline);

	qp->sq.queue = rxe_queue_init(rxe,
				      &qp->sq.max_wr,
				      wqe_size);
	if (!qp->sq.queue)
		return -ENOMEM;

	err = do_mmap_info(rxe, uresp ? &uresp->sq_mi : NULL, context,
			   qp->sq.queue->buf, qp->sq.queue->buf_size,
			   &qp->sq.queue->ip);

	if (err) {
		vfree(qp->sq.queue->buf);
		kfree(qp->sq.queue);
		return err;
	}

	qp->req.wqe_index	= producer_index(qp->sq.queue);
	qp->req.state		= QP_STATE_RESET;
	qp->req.opcode		= -1;
	qp->comp.opcode		= -1;

	spin_lock_init(&qp->sq.sq_lock);
	skb_queue_head_init(&qp->req_pkts);

	rxe_init_task(rxe, &qp->req.task, qp,
		      rxe_requester, "req");
	rxe_init_task(rxe, &qp->comp.task, qp,
		      rxe_completer, "comp");

	qp->qp_timeout_jiffies = 0; /* Can't be set for UD/UC in modify_qp */
	if (init->qp_type == IB_QPT_RC) {
		timer_setup(&qp->rnr_nak_timer, rnr_nak_timer, 0);
		timer_setup(&qp->retrans_timer, retransmit_timer, 0);
	}
	return 0;
}

static int rxe_qp_init_resp(struct rxe_dev *rxe, struct rxe_qp *qp,
			    struct ib_qp_init_attr *init,
			    struct ib_ucontext *context,
			    struct rxe_create_qp_resp __user *uresp)
{
	int err;
	int wqe_size;

	if (!qp->srq) {
		qp->rq.max_wr		= init->cap.max_recv_wr;
		qp->rq.max_sge		= init->cap.max_recv_sge;

		wqe_size = rcv_wqe_size(qp->rq.max_sge);

		pr_debug("qp#%d max_wr = %d, max_sge = %d, wqe_size = %d\n",
			 qp_num(qp), qp->rq.max_wr, qp->rq.max_sge, wqe_size);

		qp->rq.queue = rxe_queue_init(rxe,
					      &qp->rq.max_wr,
					      wqe_size);
		if (!qp->rq.queue)
			return -ENOMEM;

		err = do_mmap_info(rxe, uresp ? &uresp->rq_mi : NULL, context,
				   qp->rq.queue->buf, qp->rq.queue->buf_size,
				   &qp->rq.queue->ip);
		if (err) {
			vfree(qp->rq.queue->buf);
			kfree(qp->rq.queue);
			return err;
		}
	}

	spin_lock_init(&qp->rq.producer_lock);
	spin_lock_init(&qp->rq.consumer_lock);

	skb_queue_head_init(&qp->resp_pkts);

	rxe_init_task(rxe, &qp->resp.task, qp,
		      rxe_responder, "resp");

	qp->resp.opcode		= OPCODE_NONE;
	qp->resp.msn		= 0;
	qp->resp.state		= QP_STATE_RESET;

	return 0;
}

/* called by the create qp verb */
int rxe_qp_from_init(struct rxe_dev *rxe, struct rxe_qp *qp, struct rxe_pd *pd,
		     struct ib_qp_init_attr *init,
		     struct rxe_create_qp_resp __user *uresp,
		     struct ib_pd *ibpd,
		     struct ib_udata *udata)
{
	int err;
	struct rxe_cq *rcq = to_rcq(init->recv_cq);
	struct rxe_cq *scq = to_rcq(init->send_cq);
	struct rxe_srq *srq = init->srq ? to_rsrq(init->srq) : NULL;
	struct ib_ucontext *context = udata ? ibpd->uobject->context : NULL;

	rxe_add_ref(pd);
	rxe_add_ref(rcq);
	rxe_add_ref(scq);
	if (srq)
		rxe_add_ref(srq);

	qp->pd			= pd;
	qp->rcq			= rcq;
	qp->scq			= scq;
	qp->srq			= srq;

	rxe_qp_init_misc(rxe, qp, init);

	err = rxe_qp_init_req(rxe, qp, init, context, uresp);
	if (err)
		goto err1;

	err = rxe_qp_init_resp(rxe, qp, init, context, uresp);
	if (err)
		goto err2;

	qp->attr.qp_state = IB_QPS_RESET;
	qp->valid = 1;

	return 0;

err2:
	rxe_queue_cleanup(qp->sq.queue);
err1:
	if (srq)
		rxe_drop_ref(srq);
	rxe_drop_ref(scq);
	rxe_drop_ref(rcq);
	rxe_drop_ref(pd);

	return err;
}

/* called by the query qp verb */
int rxe_qp_to_init(struct rxe_qp *qp, struct ib_qp_init_attr *init)
{
	init->event_handler		= qp->ibqp.event_handler;
	init->qp_context		= qp->ibqp.qp_context;
	init->send_cq			= qp->ibqp.send_cq;
	init->recv_cq			= qp->ibqp.recv_cq;
	init->srq			= qp->ibqp.srq;

	init->cap.max_send_wr		= qp->sq.max_wr;
	init->cap.max_send_sge		= qp->sq.max_sge;
	init->cap.max_inline_data	= qp->sq.max_inline;

	if (!qp->srq) {
		init->cap.max_recv_wr		= qp->rq.max_wr;
		init->cap.max_recv_sge		= qp->rq.max_sge;
	}

	init->sq_sig_type		= qp->sq_sig_type;

	init->qp_type			= qp->ibqp.qp_type;
	init->port_num			= 1;

	return 0;
}

/* called by the modify qp verb, this routine checks all the parameters before
 * making any changes
 */
int rxe_qp_chk_attr(struct rxe_dev *rxe, struct rxe_qp *qp,
		    struct ib_qp_attr *attr, int mask)
{
	enum ib_qp_state cur_state = (mask & IB_QP_CUR_STATE) ?
					attr->cur_qp_state : qp->attr.qp_state;
	enum ib_qp_state new_state = (mask & IB_QP_STATE) ?
					attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, new_state, qp_type(qp), mask)) {
		pr_warn("invalid mask or state for qp\n");
		goto err1;
	}

	if (mask & IB_QP_STATE) {
		if (cur_state == IB_QPS_SQD) {
			if (qp->req.state == QP_STATE_DRAIN &&
			    new_state != IB_QPS_ERR)
				goto err1;
		}
	}

	if (mask & IB_QP_PORT) {
		if (!rdma_is_port_valid(&rxe->ib_dev, attr->port_num)) {
			pr_warn("invalid port %d\n", attr->port_num);
			goto err1;
		}
	}

	if (mask & IB_QP_CAP && rxe_qp_chk_cap(rxe, &attr->cap, !!qp->srq))
		goto err1;

	if (mask & IB_QP_AV && rxe_av_chk_attr(rxe, &attr->ah_attr))
		goto err1;

	if (mask & IB_QP_ALT_PATH) {
		if (rxe_av_chk_attr(rxe, &attr->alt_ah_attr))
			goto err1;
		if (!rdma_is_port_valid(&rxe->ib_dev, attr->alt_port_num))  {
			pr_warn("invalid alt port %d\n", attr->alt_port_num);
			goto err1;
		}
		if (attr->alt_timeout > 31) {
			pr_warn("invalid QP alt timeout %d > 31\n",
				attr->alt_timeout);
			goto err1;
		}
	}

	if (mask & IB_QP_PATH_MTU) {
		struct rxe_port *port = &rxe->port;

		enum ib_mtu max_mtu = port->attr.max_mtu;
		enum ib_mtu mtu = attr->path_mtu;

		if (mtu > max_mtu) {
			pr_debug("invalid mtu (%d) > (%d)\n",
				 ib_mtu_enum_to_int(mtu),
				 ib_mtu_enum_to_int(max_mtu));
			goto err1;
		}
	}

	if (mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic > rxe->attr.max_qp_rd_atom) {
			pr_warn("invalid max_rd_atomic %d > %d\n",
				attr->max_rd_atomic,
				rxe->attr.max_qp_rd_atom);
			goto err1;
		}
	}

	if (mask & IB_QP_TIMEOUT) {
		if (attr->timeout > 31) {
			pr_warn("invalid QP timeout %d > 31\n",
				attr->timeout);
			goto err1;
		}
	}

	return 0;

err1:
	return -EINVAL;
}

/* move the qp to the reset state */
static void rxe_qp_reset(struct rxe_qp *qp)
{
	/* stop tasks from running */
	rxe_disable_task(&qp->resp.task);

	/* stop request/comp */
	if (qp->sq.queue) {
		if (qp_type(qp) == IB_QPT_RC)
			rxe_disable_task(&qp->comp.task);
		rxe_disable_task(&qp->req.task);
	}

	/* move qp to the reset state */
	qp->req.state = QP_STATE_RESET;
	qp->resp.state = QP_STATE_RESET;

	/* let state machines reset themselves drain work and packet queues
	 * etc.
	 */
	__rxe_do_task(&qp->resp.task);

	if (qp->sq.queue) {
		__rxe_do_task(&qp->comp.task);
		__rxe_do_task(&qp->req.task);
		rxe_queue_reset(qp->sq.queue);
	}

	/* cleanup attributes */
	atomic_set(&qp->ssn, 0);
	qp->req.opcode = -1;
	qp->req.need_retry = 0;
	qp->req.noack_pkts = 0;
	qp->resp.msn = 0;
	qp->resp.opcode = -1;
	qp->resp.drop_msg = 0;
	qp->resp.goto_error = 0;
	qp->resp.sent_psn_nak = 0;

	if (qp->resp.mr) {
		rxe_drop_ref(qp->resp.mr);
		qp->resp.mr = NULL;
	}

	cleanup_rd_atomic_resources(qp);

	/* reenable tasks */
	rxe_enable_task(&qp->resp.task);

	if (qp->sq.queue) {
		if (qp_type(qp) == IB_QPT_RC)
			rxe_enable_task(&qp->comp.task);

		rxe_enable_task(&qp->req.task);
	}
}

/* drain the send queue */
static void rxe_qp_drain(struct rxe_qp *qp)
{
	if (qp->sq.queue) {
		if (qp->req.state != QP_STATE_DRAINED) {
			qp->req.state = QP_STATE_DRAIN;
			if (qp_type(qp) == IB_QPT_RC)
				rxe_run_task(&qp->comp.task, 1);
			else
				__rxe_do_task(&qp->comp.task);
			rxe_run_task(&qp->req.task, 1);
		}
	}
}

/* move the qp to the error state */
void rxe_qp_error(struct rxe_qp *qp)
{
	qp->req.state = QP_STATE_ERROR;
	qp->resp.state = QP_STATE_ERROR;
	qp->attr.qp_state = IB_QPS_ERR;

	/* drain work and packet queues */
	rxe_run_task(&qp->resp.task, 1);

	if (qp_type(qp) == IB_QPT_RC)
		rxe_run_task(&qp->comp.task, 1);
	else
		__rxe_do_task(&qp->comp.task);
	rxe_run_task(&qp->req.task, 1);
}

/* called by the modify qp verb */
int rxe_qp_from_attr(struct rxe_qp *qp, struct ib_qp_attr *attr, int mask,
		     struct ib_udata *udata)
{
	int err;

	if (mask & IB_QP_MAX_QP_RD_ATOMIC) {
		int max_rd_atomic = __roundup_pow_of_two(attr->max_rd_atomic);

		qp->attr.max_rd_atomic = max_rd_atomic;
		atomic_set(&qp->req.rd_atomic, max_rd_atomic);
	}

	if (mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		int max_dest_rd_atomic =
			__roundup_pow_of_two(attr->max_dest_rd_atomic);

		qp->attr.max_dest_rd_atomic = max_dest_rd_atomic;

		free_rd_atomic_resources(qp);

		err = alloc_rd_atomic_resources(qp, max_dest_rd_atomic);
		if (err)
			return err;
	}

	if (mask & IB_QP_CUR_STATE)
		qp->attr.cur_qp_state = attr->qp_state;

	if (mask & IB_QP_EN_SQD_ASYNC_NOTIFY)
		qp->attr.en_sqd_async_notify = attr->en_sqd_async_notify;

	if (mask & IB_QP_ACCESS_FLAGS)
		qp->attr.qp_access_flags = attr->qp_access_flags;

	if (mask & IB_QP_PKEY_INDEX)
		qp->attr.pkey_index = attr->pkey_index;

	if (mask & IB_QP_PORT)
		qp->attr.port_num = attr->port_num;

	if (mask & IB_QP_QKEY)
		qp->attr.qkey = attr->qkey;

	if (mask & IB_QP_AV) {
		rxe_av_from_attr(attr->port_num, &qp->pri_av, &attr->ah_attr);
		rxe_av_fill_ip_info(&qp->pri_av, &attr->ah_attr);
	}

	if (mask & IB_QP_ALT_PATH) {
		rxe_av_from_attr(attr->alt_port_num, &qp->alt_av,
				 &attr->alt_ah_attr);
		rxe_av_fill_ip_info(&qp->alt_av, &attr->alt_ah_attr);
		qp->attr.alt_port_num = attr->alt_port_num;
		qp->attr.alt_pkey_index = attr->alt_pkey_index;
		qp->attr.alt_timeout = attr->alt_timeout;
	}

	if (mask & IB_QP_PATH_MTU) {
		qp->attr.path_mtu = attr->path_mtu;
		qp->mtu = ib_mtu_enum_to_int(attr->path_mtu);
	}

	if (mask & IB_QP_TIMEOUT) {
		qp->attr.timeout = attr->timeout;
		if (attr->timeout == 0) {
			qp->qp_timeout_jiffies = 0;
		} else {
			/* According to the spec, timeout = 4.096 * 2 ^ attr->timeout [us] */
			int j = nsecs_to_jiffies(4096ULL << attr->timeout);

			qp->qp_timeout_jiffies = j ? j : 1;
		}
	}

	if (mask & IB_QP_RETRY_CNT) {
		qp->attr.retry_cnt = attr->retry_cnt;
		qp->comp.retry_cnt = attr->retry_cnt;
		pr_debug("qp#%d set retry count = %d\n", qp_num(qp),
			 attr->retry_cnt);
	}

	if (mask & IB_QP_RNR_RETRY) {
		qp->attr.rnr_retry = attr->rnr_retry;
		qp->comp.rnr_retry = attr->rnr_retry;
		pr_debug("qp#%d set rnr retry count = %d\n", qp_num(qp),
			 attr->rnr_retry);
	}

	if (mask & IB_QP_RQ_PSN) {
		qp->attr.rq_psn = (attr->rq_psn & BTH_PSN_MASK);
		qp->resp.psn = qp->attr.rq_psn;
		pr_debug("qp#%d set resp psn = 0x%x\n", qp_num(qp),
			 qp->resp.psn);
	}

	if (mask & IB_QP_MIN_RNR_TIMER) {
		qp->attr.min_rnr_timer = attr->min_rnr_timer;
		pr_debug("qp#%d set min rnr timer = 0x%x\n", qp_num(qp),
			 attr->min_rnr_timer);
	}

	if (mask & IB_QP_SQ_PSN) {
		qp->attr.sq_psn = (attr->sq_psn & BTH_PSN_MASK);
		qp->req.psn = qp->attr.sq_psn;
		qp->comp.psn = qp->attr.sq_psn;
		pr_debug("qp#%d set req psn = 0x%x\n", qp_num(qp), qp->req.psn);
	}

	if (mask & IB_QP_PATH_MIG_STATE)
		qp->attr.path_mig_state = attr->path_mig_state;

	if (mask & IB_QP_DEST_QPN)
		qp->attr.dest_qp_num = attr->dest_qp_num;

	if (mask & IB_QP_STATE) {
		qp->attr.qp_state = attr->qp_state;

		switch (attr->qp_state) {
		case IB_QPS_RESET:
			pr_debug("qp#%d state -> RESET\n", qp_num(qp));
			rxe_qp_reset(qp);
			break;

		case IB_QPS_INIT:
			pr_debug("qp#%d state -> INIT\n", qp_num(qp));
			qp->req.state = QP_STATE_INIT;
			qp->resp.state = QP_STATE_INIT;
			break;

		case IB_QPS_RTR:
			pr_debug("qp#%d state -> RTR\n", qp_num(qp));
			qp->resp.state = QP_STATE_READY;
			break;

		case IB_QPS_RTS:
			pr_debug("qp#%d state -> RTS\n", qp_num(qp));
			qp->req.state = QP_STATE_READY;
			break;

		case IB_QPS_SQD:
			pr_debug("qp#%d state -> SQD\n", qp_num(qp));
			rxe_qp_drain(qp);
			break;

		case IB_QPS_SQE:
			pr_warn("qp#%d state -> SQE !!?\n", qp_num(qp));
			/* Not possible from modify_qp. */
			break;

		case IB_QPS_ERR:
			pr_debug("qp#%d state -> ERR\n", qp_num(qp));
			rxe_qp_error(qp);
			break;
		}
	}

	return 0;
}

/* called by the query qp verb */
int rxe_qp_to_attr(struct rxe_qp *qp, struct ib_qp_attr *attr, int mask)
{
	*attr = qp->attr;

	attr->rq_psn				= qp->resp.psn;
	attr->sq_psn				= qp->req.psn;

	attr->cap.max_send_wr			= qp->sq.max_wr;
	attr->cap.max_send_sge			= qp->sq.max_sge;
	attr->cap.max_inline_data		= qp->sq.max_inline;

	if (!qp->srq) {
		attr->cap.max_recv_wr		= qp->rq.max_wr;
		attr->cap.max_recv_sge		= qp->rq.max_sge;
	}

	rxe_av_to_attr(&qp->pri_av, &attr->ah_attr);
	rxe_av_to_attr(&qp->alt_av, &attr->alt_ah_attr);

	if (qp->req.state == QP_STATE_DRAIN) {
		attr->sq_draining = 1;
		/* applications that get this state
		 * typically spin on it. yield the
		 * processor
		 */
		cond_resched();
	} else {
		attr->sq_draining = 0;
	}

	pr_debug("attr->sq_draining = %d\n", attr->sq_draining);

	return 0;
}

/* called by the destroy qp verb */
void rxe_qp_destroy(struct rxe_qp *qp)
{
	qp->valid = 0;
	qp->qp_timeout_jiffies = 0;
	rxe_cleanup_task(&qp->resp.task);

	if (qp_type(qp) == IB_QPT_RC) {
		del_timer_sync(&qp->retrans_timer);
		del_timer_sync(&qp->rnr_nak_timer);
	}

	rxe_cleanup_task(&qp->req.task);
	rxe_cleanup_task(&qp->comp.task);

	/* flush out any receive wr's or pending requests */
	__rxe_do_task(&qp->req.task);
	if (qp->sq.queue) {
		__rxe_do_task(&qp->comp.task);
		__rxe_do_task(&qp->req.task);
	}
}

/* called when the last reference to the qp is dropped */
static void rxe_qp_do_cleanup(struct work_struct *work)
{
	struct rxe_qp *qp = container_of(work, typeof(*qp), cleanup_work.work);

	rxe_drop_all_mcast_groups(qp);

	if (qp->sq.queue)
		rxe_queue_cleanup(qp->sq.queue);

	if (qp->srq)
		rxe_drop_ref(qp->srq);

	if (qp->rq.queue)
		rxe_queue_cleanup(qp->rq.queue);

	if (qp->scq)
		rxe_drop_ref(qp->scq);
	if (qp->rcq)
		rxe_drop_ref(qp->rcq);
	if (qp->pd)
		rxe_drop_ref(qp->pd);

	if (qp->resp.mr) {
		rxe_drop_ref(qp->resp.mr);
		qp->resp.mr = NULL;
	}

	if (qp_type(qp) == IB_QPT_RC)
		sk_dst_reset(qp->sk->sk);

	free_rd_atomic_resources(qp);

	kernel_sock_shutdown(qp->sk, SHUT_RDWR);
	sock_release(qp->sk);
}

/* called when the last reference to the qp is dropped */
void rxe_qp_cleanup(struct rxe_pool_entry *arg)
{
	struct rxe_qp *qp = container_of(arg, typeof(*qp), pelem);

	execute_in_process_context(rxe_qp_do_cleanup, &qp->cleanup_work);
}
