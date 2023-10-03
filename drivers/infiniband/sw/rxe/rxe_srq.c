// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/vmalloc.h>
#include "rxe.h"
#include "rxe_queue.h"

int rxe_srq_chk_init(struct rxe_dev *rxe, struct ib_srq_init_attr *init)
{
	struct ib_srq_attr *attr = &init->attr;

	if (attr->max_wr > rxe->attr.max_srq_wr) {
		rxe_dbg_dev(rxe, "max_wr(%d) > max_srq_wr(%d)\n",
			attr->max_wr, rxe->attr.max_srq_wr);
		goto err1;
	}

	if (attr->max_wr <= 0) {
		rxe_dbg_dev(rxe, "max_wr(%d) <= 0\n", attr->max_wr);
		goto err1;
	}

	if (attr->max_wr < RXE_MIN_SRQ_WR)
		attr->max_wr = RXE_MIN_SRQ_WR;

	if (attr->max_sge > rxe->attr.max_srq_sge) {
		rxe_dbg_dev(rxe, "max_sge(%d) > max_srq_sge(%d)\n",
			attr->max_sge, rxe->attr.max_srq_sge);
		goto err1;
	}

	if (attr->max_sge < RXE_MIN_SRQ_SGE)
		attr->max_sge = RXE_MIN_SRQ_SGE;

	return 0;

err1:
	return -EINVAL;
}

int rxe_srq_from_init(struct rxe_dev *rxe, struct rxe_srq *srq,
		      struct ib_srq_init_attr *init, struct ib_udata *udata,
		      struct rxe_create_srq_resp __user *uresp)
{
	struct rxe_queue *q;
	int wqe_size;
	int err;

	srq->ibsrq.event_handler = init->event_handler;
	srq->ibsrq.srq_context = init->srq_context;
	srq->limit = init->attr.srq_limit;
	srq->srq_num = srq->elem.index;
	srq->rq.max_wr = init->attr.max_wr;
	srq->rq.max_sge = init->attr.max_sge;

	wqe_size = sizeof(struct rxe_recv_wqe) +
			srq->rq.max_sge*sizeof(struct ib_sge);

	spin_lock_init(&srq->rq.producer_lock);
	spin_lock_init(&srq->rq.consumer_lock);

	q = rxe_queue_init(rxe, &srq->rq.max_wr, wqe_size,
			   QUEUE_TYPE_FROM_CLIENT);
	if (!q) {
		rxe_dbg_srq(srq, "Unable to allocate queue\n");
		err = -ENOMEM;
		goto err_out;
	}

	err = do_mmap_info(rxe, uresp ? &uresp->mi : NULL, udata, q->buf,
			   q->buf_size, &q->ip);
	if (err) {
		rxe_dbg_srq(srq, "Unable to init mmap info for caller\n");
		goto err_free;
	}

	srq->rq.queue = q;
	init->attr.max_wr = srq->rq.max_wr;

	if (uresp) {
		if (copy_to_user(&uresp->srq_num, &srq->srq_num,
				 sizeof(uresp->srq_num))) {
			rxe_queue_cleanup(q);
			return -EFAULT;
		}
	}

	return 0;

err_free:
	vfree(q->buf);
	kfree(q);
err_out:
	return err;
}

int rxe_srq_chk_attr(struct rxe_dev *rxe, struct rxe_srq *srq,
		     struct ib_srq_attr *attr, enum ib_srq_attr_mask mask)
{
	if (srq->error) {
		rxe_dbg_srq(srq, "in error state\n");
		goto err1;
	}

	if (mask & IB_SRQ_MAX_WR) {
		if (attr->max_wr > rxe->attr.max_srq_wr) {
			rxe_dbg_srq(srq, "max_wr(%d) > max_srq_wr(%d)\n",
				attr->max_wr, rxe->attr.max_srq_wr);
			goto err1;
		}

		if (attr->max_wr <= 0) {
			rxe_dbg_srq(srq, "max_wr(%d) <= 0\n", attr->max_wr);
			goto err1;
		}

		if (srq->limit && (attr->max_wr < srq->limit)) {
			rxe_dbg_srq(srq, "max_wr (%d) < srq->limit (%d)\n",
				attr->max_wr, srq->limit);
			goto err1;
		}

		if (attr->max_wr < RXE_MIN_SRQ_WR)
			attr->max_wr = RXE_MIN_SRQ_WR;
	}

	if (mask & IB_SRQ_LIMIT) {
		if (attr->srq_limit > rxe->attr.max_srq_wr) {
			rxe_dbg_srq(srq, "srq_limit(%d) > max_srq_wr(%d)\n",
				attr->srq_limit, rxe->attr.max_srq_wr);
			goto err1;
		}

		if (attr->srq_limit > srq->rq.queue->buf->index_mask) {
			rxe_dbg_srq(srq, "srq_limit (%d) > cur limit(%d)\n",
				attr->srq_limit,
				srq->rq.queue->buf->index_mask);
			goto err1;
		}
	}

	return 0;

err1:
	return -EINVAL;
}

int rxe_srq_from_attr(struct rxe_dev *rxe, struct rxe_srq *srq,
		      struct ib_srq_attr *attr, enum ib_srq_attr_mask mask,
		      struct rxe_modify_srq_cmd *ucmd, struct ib_udata *udata)
{
	struct rxe_queue *q = srq->rq.queue;
	struct mminfo __user *mi = NULL;
	int wqe_size;
	int err;

	if (mask & IB_SRQ_MAX_WR) {
		/*
		 * This is completely screwed up, the response is supposed to
		 * be in the outbuf not like this.
		 */
		mi = u64_to_user_ptr(ucmd->mmap_info_addr);

		wqe_size = sizeof(struct rxe_recv_wqe) +
				srq->rq.max_sge*sizeof(struct ib_sge);

		err = rxe_queue_resize(q, &attr->max_wr, wqe_size,
				       udata, mi, &srq->rq.producer_lock,
				       &srq->rq.consumer_lock);
		if (err)
			goto err_free;

		srq->rq.max_wr = attr->max_wr;
	}

	if (mask & IB_SRQ_LIMIT)
		srq->limit = attr->srq_limit;

	return 0;

err_free:
	rxe_queue_cleanup(q);
	srq->rq.queue = NULL;
	return err;
}

void rxe_srq_cleanup(struct rxe_pool_elem *elem)
{
	struct rxe_srq *srq = container_of(elem, typeof(*srq), elem);

	if (srq->pd)
		rxe_put(srq->pd);

	if (srq->rq.queue)
		rxe_queue_cleanup(srq->rq.queue);
}
