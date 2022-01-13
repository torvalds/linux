// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2020 Hewlett Packard Enterprise, Inc. All rights reserved.
 */

#include "rxe.h"

int rxe_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct rxe_mw *mw = to_rmw(ibmw);
	struct rxe_pd *pd = to_rpd(ibmw->pd);
	struct rxe_dev *rxe = to_rdev(ibmw->device);
	int ret;

	rxe_add_ref(pd);

	ret = rxe_add_to_pool(&rxe->mw_pool, mw);
	if (ret) {
		rxe_drop_ref(pd);
		return ret;
	}

	rxe_add_index(mw);
	mw->rkey = ibmw->rkey = (mw->elem.index << 8) | rxe_get_next_key(-1);
	mw->state = (mw->ibmw.type == IB_MW_TYPE_2) ?
			RXE_MW_STATE_FREE : RXE_MW_STATE_VALID;
	spin_lock_init(&mw->lock);

	return 0;
}

static void rxe_do_dealloc_mw(struct rxe_mw *mw)
{
	if (mw->mr) {
		struct rxe_mr *mr = mw->mr;

		mw->mr = NULL;
		atomic_dec(&mr->num_mw);
		rxe_drop_ref(mr);
	}

	if (mw->qp) {
		struct rxe_qp *qp = mw->qp;

		mw->qp = NULL;
		rxe_drop_ref(qp);
	}

	mw->access = 0;
	mw->addr = 0;
	mw->length = 0;
	mw->state = RXE_MW_STATE_INVALID;
}

int rxe_dealloc_mw(struct ib_mw *ibmw)
{
	struct rxe_mw *mw = to_rmw(ibmw);
	struct rxe_pd *pd = to_rpd(ibmw->pd);

	spin_lock_bh(&mw->lock);
	rxe_do_dealloc_mw(mw);
	spin_unlock_bh(&mw->lock);

	rxe_drop_ref(mw);
	rxe_drop_ref(pd);

	return 0;
}

static int rxe_check_bind_mw(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
			 struct rxe_mw *mw, struct rxe_mr *mr)
{
	u32 key = wqe->wr.wr.mw.rkey & 0xff;

	if (mw->ibmw.type == IB_MW_TYPE_1) {
		if (unlikely(mw->state != RXE_MW_STATE_VALID)) {
			pr_err_once(
				"attempt to bind a type 1 MW not in the valid state\n");
			return -EINVAL;
		}

		/* o10-36.2.2 */
		if (unlikely((mw->access & IB_ZERO_BASED))) {
			pr_err_once("attempt to bind a zero based type 1 MW\n");
			return -EINVAL;
		}
	}

	if (mw->ibmw.type == IB_MW_TYPE_2) {
		/* o10-37.2.30 */
		if (unlikely(mw->state != RXE_MW_STATE_FREE)) {
			pr_err_once(
				"attempt to bind a type 2 MW not in the free state\n");
			return -EINVAL;
		}

		/* C10-72 */
		if (unlikely(qp->pd != to_rpd(mw->ibmw.pd))) {
			pr_err_once(
				"attempt to bind type 2 MW with qp with different PD\n");
			return -EINVAL;
		}

		/* o10-37.2.40 */
		if (unlikely(!mr || wqe->wr.wr.mw.length == 0)) {
			pr_err_once(
				"attempt to invalidate type 2 MW by binding with NULL or zero length MR\n");
			return -EINVAL;
		}
	}

	if (unlikely(key == (mw->rkey & 0xff))) {
		pr_err_once("attempt to bind MW with same key\n");
		return -EINVAL;
	}

	/* remaining checks only apply to a nonzero MR */
	if (!mr)
		return 0;

	if (unlikely(mr->access & IB_ZERO_BASED)) {
		pr_err_once("attempt to bind MW to zero based MR\n");
		return -EINVAL;
	}

	/* C10-73 */
	if (unlikely(!(mr->access & IB_ACCESS_MW_BIND))) {
		pr_err_once(
			"attempt to bind an MW to an MR without bind access\n");
		return -EINVAL;
	}

	/* C10-74 */
	if (unlikely((mw->access &
		      (IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_ATOMIC)) &&
		     !(mr->access & IB_ACCESS_LOCAL_WRITE))) {
		pr_err_once(
			"attempt to bind an writeable MW to an MR without local write access\n");
		return -EINVAL;
	}

	/* C10-75 */
	if (mw->access & IB_ZERO_BASED) {
		if (unlikely(wqe->wr.wr.mw.length > mr->cur_map_set->length)) {
			pr_err_once(
				"attempt to bind a ZB MW outside of the MR\n");
			return -EINVAL;
		}
	} else {
		if (unlikely((wqe->wr.wr.mw.addr < mr->cur_map_set->iova) ||
			     ((wqe->wr.wr.mw.addr + wqe->wr.wr.mw.length) >
			      (mr->cur_map_set->iova + mr->cur_map_set->length)))) {
			pr_err_once(
				"attempt to bind a VA MW outside of the MR\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void rxe_do_bind_mw(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
		      struct rxe_mw *mw, struct rxe_mr *mr)
{
	u32 key = wqe->wr.wr.mw.rkey & 0xff;

	mw->rkey = (mw->rkey & ~0xff) | key;
	mw->access = wqe->wr.wr.mw.access;
	mw->state = RXE_MW_STATE_VALID;
	mw->addr = wqe->wr.wr.mw.addr;
	mw->length = wqe->wr.wr.mw.length;

	if (mw->mr) {
		rxe_drop_ref(mw->mr);
		atomic_dec(&mw->mr->num_mw);
		mw->mr = NULL;
	}

	if (mw->length) {
		mw->mr = mr;
		atomic_inc(&mr->num_mw);
		rxe_add_ref(mr);
	}

	if (mw->ibmw.type == IB_MW_TYPE_2) {
		rxe_add_ref(qp);
		mw->qp = qp;
	}
}

int rxe_bind_mw(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	int ret;
	struct rxe_mw *mw;
	struct rxe_mr *mr;
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	u32 mw_rkey = wqe->wr.wr.mw.mw_rkey;
	u32 mr_lkey = wqe->wr.wr.mw.mr_lkey;

	mw = rxe_pool_get_index(&rxe->mw_pool, mw_rkey >> 8);
	if (unlikely(!mw)) {
		ret = -EINVAL;
		goto err;
	}

	if (unlikely(mw->rkey != mw_rkey)) {
		ret = -EINVAL;
		goto err_drop_mw;
	}

	if (likely(wqe->wr.wr.mw.length)) {
		mr = rxe_pool_get_index(&rxe->mr_pool, mr_lkey >> 8);
		if (unlikely(!mr)) {
			ret = -EINVAL;
			goto err_drop_mw;
		}

		if (unlikely(mr->lkey != mr_lkey)) {
			ret = -EINVAL;
			goto err_drop_mr;
		}
	} else {
		mr = NULL;
	}

	spin_lock_bh(&mw->lock);

	ret = rxe_check_bind_mw(qp, wqe, mw, mr);
	if (ret)
		goto err_unlock;

	rxe_do_bind_mw(qp, wqe, mw, mr);
err_unlock:
	spin_unlock_bh(&mw->lock);
err_drop_mr:
	if (mr)
		rxe_drop_ref(mr);
err_drop_mw:
	rxe_drop_ref(mw);
err:
	return ret;
}

static int rxe_check_invalidate_mw(struct rxe_qp *qp, struct rxe_mw *mw)
{
	if (unlikely(mw->state == RXE_MW_STATE_INVALID))
		return -EINVAL;

	/* o10-37.2.26 */
	if (unlikely(mw->ibmw.type == IB_MW_TYPE_1))
		return -EINVAL;

	return 0;
}

static void rxe_do_invalidate_mw(struct rxe_mw *mw)
{
	struct rxe_qp *qp;
	struct rxe_mr *mr;

	/* valid type 2 MW will always have a QP pointer */
	qp = mw->qp;
	mw->qp = NULL;
	rxe_drop_ref(qp);

	/* valid type 2 MW will always have an MR pointer */
	mr = mw->mr;
	mw->mr = NULL;
	atomic_dec(&mr->num_mw);
	rxe_drop_ref(mr);

	mw->access = 0;
	mw->addr = 0;
	mw->length = 0;
	mw->state = RXE_MW_STATE_FREE;
}

int rxe_invalidate_mw(struct rxe_qp *qp, u32 rkey)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_mw *mw;
	int ret;

	mw = rxe_pool_get_index(&rxe->mw_pool, rkey >> 8);
	if (!mw) {
		ret = -EINVAL;
		goto err;
	}

	if (rkey != mw->rkey) {
		ret = -EINVAL;
		goto err_drop_ref;
	}

	spin_lock_bh(&mw->lock);

	ret = rxe_check_invalidate_mw(qp, mw);
	if (ret)
		goto err_unlock;

	rxe_do_invalidate_mw(mw);
err_unlock:
	spin_unlock_bh(&mw->lock);
err_drop_ref:
	rxe_drop_ref(mw);
err:
	return ret;
}

struct rxe_mw *rxe_lookup_mw(struct rxe_qp *qp, int access, u32 rkey)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	struct rxe_pd *pd = to_rpd(qp->ibqp.pd);
	struct rxe_mw *mw;
	int index = rkey >> 8;

	mw = rxe_pool_get_index(&rxe->mw_pool, index);
	if (!mw)
		return NULL;

	if (unlikely((mw->rkey != rkey) || rxe_mw_pd(mw) != pd ||
		     (mw->ibmw.type == IB_MW_TYPE_2 && mw->qp != qp) ||
		     (mw->length == 0) ||
		     (access && !(access & mw->access)) ||
		     mw->state != RXE_MW_STATE_VALID)) {
		rxe_drop_ref(mw);
		return NULL;
	}

	return mw;
}

void rxe_mw_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_mw *mw = container_of(elem, typeof(*mw), elem);

	rxe_drop_index(mw);
}
