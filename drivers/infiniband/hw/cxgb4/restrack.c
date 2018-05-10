/*
 * Copyright (c) 2018 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <rdma/rdma_cm.h>

#include "iw_cxgb4.h"
#include <rdma/restrack.h>
#include <uapi/rdma/rdma_netlink.h>

static int fill_sq(struct sk_buff *msg, struct t4_wq *wq)
{
	/* WQ+SQ */
	if (rdma_nl_put_driver_u32(msg, "sqid", wq->sq.qid))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "flushed", wq->flushed))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "memsize", wq->sq.memsize))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "cidx", wq->sq.cidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "pidx", wq->sq.pidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "wq_pidx", wq->sq.wq_pidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "flush_cidx", wq->sq.flush_cidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "in_use", wq->sq.in_use))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "size", wq->sq.size))
		goto err;
	if (rdma_nl_put_driver_u32_hex(msg, "flags", wq->sq.flags))
		goto err;
	return 0;
err:
	return -EMSGSIZE;
}

static int fill_rq(struct sk_buff *msg, struct t4_wq *wq)
{
	/* RQ */
	if (rdma_nl_put_driver_u32(msg, "rqid", wq->rq.qid))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "memsize", wq->rq.memsize))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "cidx", wq->rq.cidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "pidx", wq->rq.pidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "wq_pidx", wq->rq.wq_pidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "msn", wq->rq.msn))
		goto err;
	if (rdma_nl_put_driver_u32_hex(msg, "rqt_hwaddr", wq->rq.rqt_hwaddr))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "rqt_size", wq->rq.rqt_size))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "in_use", wq->rq.in_use))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "size", wq->rq.size))
		goto err;
	return 0;
err:
	return -EMSGSIZE;
}

static int fill_swsqe(struct sk_buff *msg, struct t4_sq *sq, u16 idx,
		      struct t4_swsqe *sqe)
{
	if (rdma_nl_put_driver_u32(msg, "idx", idx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "opcode", sqe->opcode))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "complete", sqe->complete))
		goto err;
	if (sqe->complete &&
	    rdma_nl_put_driver_u32(msg, "cqe_status", CQE_STATUS(&sqe->cqe)))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "signaled", sqe->signaled))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "flushed", sqe->flushed))
		goto err;
	return 0;
err:
	return -EMSGSIZE;
}

/*
 * Dump the first and last pending sqes.
 */
static int fill_swsqes(struct sk_buff *msg, struct t4_sq *sq,
		       u16 first_idx, struct t4_swsqe *first_sqe,
		       u16 last_idx, struct t4_swsqe *last_sqe)
{
	if (!first_sqe)
		goto out;
	if (fill_swsqe(msg, sq, first_idx, first_sqe))
		goto err;
	if (!last_sqe)
		goto out;
	if (fill_swsqe(msg, sq, last_idx, last_sqe))
		goto err;
out:
	return 0;
err:
	return -EMSGSIZE;
}

static int fill_res_qp_entry(struct sk_buff *msg,
			     struct rdma_restrack_entry *res)
{
	struct ib_qp *ibqp = container_of(res, struct ib_qp, res);
	struct t4_swsqe *fsp = NULL, *lsp = NULL;
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);
	u16 first_sq_idx = 0, last_sq_idx = 0;
	struct t4_swsqe first_sqe, last_sqe;
	struct nlattr *table_attr;
	struct t4_wq wq;

	/* User qp state is not available, so don't dump user qps */
	if (qhp->ucontext)
		return 0;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		goto err;

	/* Get a consistent snapshot */
	spin_lock_irq(&qhp->lock);
	wq = qhp->wq;

	/* If there are any pending sqes, copy the first and last */
	if (wq.sq.cidx != wq.sq.pidx) {
		first_sq_idx = wq.sq.cidx;
		first_sqe = qhp->wq.sq.sw_sq[first_sq_idx];
		fsp = &first_sqe;
		last_sq_idx = wq.sq.pidx;
		if (last_sq_idx-- == 0)
			last_sq_idx = wq.sq.size - 1;
		if (last_sq_idx != first_sq_idx) {
			last_sqe = qhp->wq.sq.sw_sq[last_sq_idx];
			lsp = &last_sqe;
		}
	}
	spin_unlock_irq(&qhp->lock);

	if (fill_sq(msg, &wq))
		goto err_cancel_table;

	if (fill_swsqes(msg, &wq.sq, first_sq_idx, fsp, last_sq_idx, lsp))
		goto err_cancel_table;

	if (fill_rq(msg, &wq))
		goto err_cancel_table;

	nla_nest_end(msg, table_attr);
	return 0;

err_cancel_table:
	nla_nest_cancel(msg, table_attr);
err:
	return -EMSGSIZE;
}

union union_ep {
	struct c4iw_listen_ep lep;
	struct c4iw_ep ep;
};

static int fill_res_ep_entry(struct sk_buff *msg,
			     struct rdma_restrack_entry *res)
{
	struct rdma_cm_id *cm_id = rdma_res_to_id(res);
	struct nlattr *table_attr;
	struct c4iw_ep_common *epcp;
	struct c4iw_listen_ep *listen_ep = NULL;
	struct c4iw_ep *ep = NULL;
	struct iw_cm_id *iw_cm_id;
	union union_ep *uep;

	iw_cm_id = rdma_iw_cm_id(cm_id);
	if (!iw_cm_id)
		return 0;
	epcp = (struct c4iw_ep_common *)iw_cm_id->provider_data;
	if (!epcp)
		return 0;
	uep = kcalloc(1, sizeof(*uep), GFP_KERNEL);
	if (!uep)
		return 0;

	table_attr = nla_nest_start(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		goto err_free_uep;

	/* Get a consistent snapshot */
	mutex_lock(&epcp->mutex);
	if (epcp->state == LISTEN) {
		uep->lep = *(struct c4iw_listen_ep *)epcp;
		mutex_unlock(&epcp->mutex);
		listen_ep = &uep->lep;
		epcp = &listen_ep->com;
	} else {
		uep->ep = *(struct c4iw_ep *)epcp;
		mutex_unlock(&epcp->mutex);
		ep = &uep->ep;
		epcp = &ep->com;
	}

	if (rdma_nl_put_driver_u32(msg, "state", epcp->state))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u64_hex(msg, "flags", epcp->flags))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u64_hex(msg, "history", epcp->history))
		goto err_cancel_table;

	if (epcp->state == LISTEN) {
		if (rdma_nl_put_driver_u32(msg, "stid", listen_ep->stid))
			goto err_cancel_table;
		if (rdma_nl_put_driver_u32(msg, "backlog", listen_ep->backlog))
			goto err_cancel_table;
	} else {
		if (rdma_nl_put_driver_u32(msg, "hwtid", ep->hwtid))
			goto err_cancel_table;
		if (rdma_nl_put_driver_u32(msg, "ord", ep->ord))
			goto err_cancel_table;
		if (rdma_nl_put_driver_u32(msg, "ird", ep->ird))
			goto err_cancel_table;
		if (rdma_nl_put_driver_u32(msg, "emss", ep->emss))
			goto err_cancel_table;

		if (!ep->parent_ep && rdma_nl_put_driver_u32(msg, "atid",
							     ep->atid))
			goto err_cancel_table;
	}
	nla_nest_end(msg, table_attr);
	kfree(uep);
	return 0;

err_cancel_table:
	nla_nest_cancel(msg, table_attr);
err_free_uep:
	kfree(uep);
	return -EMSGSIZE;
}

c4iw_restrack_func *c4iw_restrack_funcs[RDMA_RESTRACK_MAX] = {
	[RDMA_RESTRACK_QP]	= fill_res_qp_entry,
	[RDMA_RESTRACK_CM_ID]	= fill_res_ep_entry,
};
