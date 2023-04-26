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

int c4iw_fill_res_qp_entry(struct sk_buff *msg, struct ib_qp *ibqp)
{
	struct t4_swsqe *fsp = NULL, *lsp = NULL;
	struct c4iw_qp *qhp = to_c4iw_qp(ibqp);
	u16 first_sq_idx = 0, last_sq_idx = 0;
	struct t4_swsqe first_sqe, last_sqe;
	struct nlattr *table_attr;
	struct t4_wq wq;

	/* User qp state is not available, so don't dump user qps */
	if (qhp->ucontext)
		return 0;

	table_attr = nla_nest_start_noflag(msg, RDMA_NLDEV_ATTR_DRIVER);
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

int c4iw_fill_res_cm_id_entry(struct sk_buff *msg,
			      struct rdma_cm_id *cm_id)
{
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
	uep = kzalloc(sizeof(*uep), GFP_KERNEL);
	if (!uep)
		return 0;

	table_attr = nla_nest_start_noflag(msg, RDMA_NLDEV_ATTR_DRIVER);
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

	if (listen_ep) {
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

static int fill_cq(struct sk_buff *msg, struct t4_cq *cq)
{
	if (rdma_nl_put_driver_u32(msg, "cqid", cq->cqid))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "memsize", cq->memsize))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "size", cq->size))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "cidx", cq->cidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "cidx_inc", cq->cidx_inc))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "sw_cidx", cq->sw_cidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "sw_pidx", cq->sw_pidx))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "sw_in_use", cq->sw_in_use))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "vector", cq->vector))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "gen", cq->gen))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "error", cq->error))
		goto err;
	if (rdma_nl_put_driver_u64_hex(msg, "bits_type_ts",
					 be64_to_cpu(cq->bits_type_ts)))
		goto err;
	if (rdma_nl_put_driver_u64_hex(msg, "flags", cq->flags))
		goto err;

	return 0;

err:
	return -EMSGSIZE;
}

static int fill_cqe(struct sk_buff *msg, struct t4_cqe *cqe, u16 idx,
		    const char *qstr)
{
	if (rdma_nl_put_driver_u32(msg, qstr, idx))
		goto err;
	if (rdma_nl_put_driver_u32_hex(msg, "header",
					 be32_to_cpu(cqe->header)))
		goto err;
	if (rdma_nl_put_driver_u32(msg, "len", be32_to_cpu(cqe->len)))
		goto err;
	if (rdma_nl_put_driver_u32_hex(msg, "wrid_hi",
					 be32_to_cpu(cqe->u.gen.wrid_hi)))
		goto err;
	if (rdma_nl_put_driver_u32_hex(msg, "wrid_low",
					 be32_to_cpu(cqe->u.gen.wrid_low)))
		goto err;
	if (rdma_nl_put_driver_u64_hex(msg, "bits_type_ts",
					 be64_to_cpu(cqe->bits_type_ts)))
		goto err;

	return 0;

err:
	return -EMSGSIZE;
}

static int fill_hwcqes(struct sk_buff *msg, struct t4_cq *cq,
		       struct t4_cqe *cqes)
{
	u16 idx;

	idx = (cq->cidx > 0) ? cq->cidx - 1 : cq->size - 1;
	if (fill_cqe(msg, cqes, idx, "hwcq_idx"))
		goto err;
	idx = cq->cidx;
	if (fill_cqe(msg, cqes + 1, idx, "hwcq_idx"))
		goto err;

	return 0;
err:
	return -EMSGSIZE;
}

static int fill_swcqes(struct sk_buff *msg, struct t4_cq *cq,
		       struct t4_cqe *cqes)
{
	u16 idx;

	if (!cq->sw_in_use)
		return 0;

	idx = cq->sw_cidx;
	if (fill_cqe(msg, cqes, idx, "swcq_idx"))
		goto err;
	if (cq->sw_in_use == 1)
		goto out;
	idx = (cq->sw_pidx > 0) ? cq->sw_pidx - 1 : cq->size - 1;
	if (fill_cqe(msg, cqes + 1, idx, "swcq_idx"))
		goto err;
out:
	return 0;
err:
	return -EMSGSIZE;
}

int c4iw_fill_res_cq_entry(struct sk_buff *msg, struct ib_cq *ibcq)
{
	struct c4iw_cq *chp = to_c4iw_cq(ibcq);
	struct nlattr *table_attr;
	struct t4_cqe hwcqes[2];
	struct t4_cqe swcqes[2];
	struct t4_cq cq;
	u16 idx;

	/* User cq state is not available, so don't dump user cqs */
	if (ibcq->uobject)
		return 0;

	table_attr = nla_nest_start_noflag(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		goto err;

	/* Get a consistent snapshot */
	spin_lock_irq(&chp->lock);

	/* t4_cq struct */
	cq = chp->cq;

	/* get 2 hw cqes: cidx-1, and cidx */
	idx = (cq.cidx > 0) ? cq.cidx - 1 : cq.size - 1;
	hwcqes[0] = chp->cq.queue[idx];

	idx = cq.cidx;
	hwcqes[1] = chp->cq.queue[idx];

	/* get first and last sw cqes */
	if (cq.sw_in_use) {
		swcqes[0] = chp->cq.sw_queue[cq.sw_cidx];
		if (cq.sw_in_use > 1) {
			idx = (cq.sw_pidx > 0) ? cq.sw_pidx - 1 : cq.size - 1;
			swcqes[1] = chp->cq.sw_queue[idx];
		}
	}

	spin_unlock_irq(&chp->lock);

	if (fill_cq(msg, &cq))
		goto err_cancel_table;

	if (fill_swcqes(msg, &cq, swcqes))
		goto err_cancel_table;

	if (fill_hwcqes(msg, &cq, hwcqes))
		goto err_cancel_table;

	nla_nest_end(msg, table_attr);
	return 0;

err_cancel_table:
	nla_nest_cancel(msg, table_attr);
err:
	return -EMSGSIZE;
}

int c4iw_fill_res_mr_entry(struct sk_buff *msg, struct ib_mr *ibmr)
{
	struct c4iw_mr *mhp = to_c4iw_mr(ibmr);
	struct c4iw_dev *dev = mhp->rhp;
	u32 stag = mhp->attr.stag;
	struct nlattr *table_attr;
	struct fw_ri_tpte tpte;
	int ret;

	if (!stag)
		return 0;

	table_attr = nla_nest_start_noflag(msg, RDMA_NLDEV_ATTR_DRIVER);
	if (!table_attr)
		goto err;

	ret = cxgb4_read_tpte(dev->rdev.lldi.ports[0], stag, (__be32 *)&tpte);
	if (ret) {
		dev_err(&dev->rdev.lldi.pdev->dev,
			"%s cxgb4_read_tpte err %d\n", __func__, ret);
		return 0;
	}

	if (rdma_nl_put_driver_u32_hex(msg, "idx", stag >> 8))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32(msg, "valid",
			FW_RI_TPTE_VALID_G(ntohl(tpte.valid_to_pdid))))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32_hex(msg, "key", stag & 0xff))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32(msg, "state",
			FW_RI_TPTE_STAGSTATE_G(ntohl(tpte.valid_to_pdid))))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32(msg, "pdid",
			FW_RI_TPTE_PDID_G(ntohl(tpte.valid_to_pdid))))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32_hex(msg, "perm",
			FW_RI_TPTE_PERM_G(ntohl(tpte.locread_to_qpid))))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32(msg, "ps",
			FW_RI_TPTE_PS_G(ntohl(tpte.locread_to_qpid))))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u64(msg, "len",
		      ((u64)ntohl(tpte.len_hi) << 32) | ntohl(tpte.len_lo)))
		goto err_cancel_table;
	if (rdma_nl_put_driver_u32_hex(msg, "pbl_addr",
			FW_RI_TPTE_PBLADDR_G(ntohl(tpte.nosnoop_pbladdr))))
		goto err_cancel_table;

	nla_nest_end(msg, table_attr);
	return 0;

err_cancel_table:
	nla_nest_cancel(msg, table_attr);
err:
	return -EMSGSIZE;
}
