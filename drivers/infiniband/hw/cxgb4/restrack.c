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
	if (rdma_nl_put_driver_u64_hex(msg, "wr_id", sqe->wr_id))
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

static int fill_swrqe(struct sk_buff *msg, struct t4_rq *rq, u16 idx,
		      struct t4_swrqe *rqe)
{
	if (rdma_nl_put_driver_u32(msg, "idx", idx))
		goto err;
	if (rdma_nl_put_driver_u64_hex(msg, "wr_id", rqe->wr_id))
		goto err;
	return 0;
err:
	return -EMSGSIZE;
}

/*
 * Dump the first and last pending rqes.
 */
static int fill_swrqes(struct sk_buff *msg, struct t4_rq *rq,
		       u16 first_idx, struct t4_swrqe *first_rqe,
		       u16 last_idx, struct t4_swrqe *last_rqe)
{
	if (!first_rqe)
		goto out;
	if (fill_swrqe(msg, rq, first_idx, first_rqe))
		goto err;
	if (!last_rqe)
		goto out;
	if (fill_swrqe(msg, rq, last_idx, last_rqe))
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
	struct t4_swrqe *frp = NULL, *lrp = NULL;
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);
	struct t4_swsqe first_sqe, last_sqe;
	struct t4_swrqe first_rqe, last_rqe;
	u16 first_sq_idx, last_sq_idx;
	u16 first_rq_idx, last_rq_idx;
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

	/* If there are any pending rqes, copy the first and last */
	if (wq.rq.cidx != wq.rq.pidx) {
		first_rq_idx = wq.rq.cidx;
		first_rqe = qhp->wq.rq.sw_rq[first_rq_idx];
		frp = &first_rqe;
		last_rq_idx = wq.rq.pidx;
		if (last_rq_idx-- == 0)
			last_rq_idx = wq.rq.size - 1;
		if (last_rq_idx != first_rq_idx) {
			last_rqe = qhp->wq.rq.sw_rq[last_rq_idx];
			lrp = &last_rqe;
		}
	}
	spin_unlock_irq(&qhp->lock);

	if (fill_sq(msg, &wq))
		goto err_cancel_table;

	if (fill_swsqes(msg, &wq.sq, first_sq_idx, fsp, last_sq_idx, lsp))
		goto err_cancel_table;

	if (fill_rq(msg, &wq))
		goto err_cancel_table;

	if (fill_swrqes(msg, &wq.rq, first_rq_idx, frp, last_rq_idx, lrp))
		goto err_cancel_table;

	nla_nest_end(msg, table_attr);
	return 0;

err_cancel_table:
	nla_nest_cancel(msg, table_attr);
err:
	return -EMSGSIZE;
}

c4iw_restrack_func *c4iw_restrack_funcs[RDMA_RESTRACK_MAX] = {
	[RDMA_RESTRACK_QP]	= fill_res_qp_entry,
};
