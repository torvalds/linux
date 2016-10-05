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
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"

int rxe_srq_chk_attr(struct rxe_dev *rxe, struct rxe_srq *srq,
		     struct ib_srq_attr *attr, enum ib_srq_attr_mask mask)
{
	if (srq && srq->error) {
		pr_warn("srq in error state\n");
		goto err1;
	}

	if (mask & IB_SRQ_MAX_WR) {
		if (attr->max_wr > rxe->attr.max_srq_wr) {
			pr_warn("max_wr(%d) > max_srq_wr(%d)\n",
				attr->max_wr, rxe->attr.max_srq_wr);
			goto err1;
		}

		if (attr->max_wr <= 0) {
			pr_warn("max_wr(%d) <= 0\n", attr->max_wr);
			goto err1;
		}

		if (srq && srq->limit && (attr->max_wr < srq->limit)) {
			pr_warn("max_wr (%d) < srq->limit (%d)\n",
				attr->max_wr, srq->limit);
			goto err1;
		}

		if (attr->max_wr < RXE_MIN_SRQ_WR)
			attr->max_wr = RXE_MIN_SRQ_WR;
	}

	if (mask & IB_SRQ_LIMIT) {
		if (attr->srq_limit > rxe->attr.max_srq_wr) {
			pr_warn("srq_limit(%d) > max_srq_wr(%d)\n",
				attr->srq_limit, rxe->attr.max_srq_wr);
			goto err1;
		}

		if (srq && (attr->srq_limit > srq->rq.queue->buf->index_mask)) {
			pr_warn("srq_limit (%d) > cur limit(%d)\n",
				attr->srq_limit,
				 srq->rq.queue->buf->index_mask);
			goto err1;
		}
	}

	if (mask == IB_SRQ_INIT_MASK) {
		if (attr->max_sge > rxe->attr.max_srq_sge) {
			pr_warn("max_sge(%d) > max_srq_sge(%d)\n",
				attr->max_sge, rxe->attr.max_srq_sge);
			goto err1;
		}

		if (attr->max_sge < RXE_MIN_SRQ_SGE)
			attr->max_sge = RXE_MIN_SRQ_SGE;
	}

	return 0;

err1:
	return -EINVAL;
}

int rxe_srq_from_init(struct rxe_dev *rxe, struct rxe_srq *srq,
		      struct ib_srq_init_attr *init,
		      struct ib_ucontext *context, struct ib_udata *udata)
{
	int err;
	int srq_wqe_size;
	struct rxe_queue *q;

	srq->ibsrq.event_handler	= init->event_handler;
	srq->ibsrq.srq_context		= init->srq_context;
	srq->limit		= init->attr.srq_limit;
	srq->srq_num		= srq->pelem.index;
	srq->rq.max_wr		= init->attr.max_wr;
	srq->rq.max_sge		= init->attr.max_sge;

	srq_wqe_size		= rcv_wqe_size(srq->rq.max_sge);

	spin_lock_init(&srq->rq.producer_lock);
	spin_lock_init(&srq->rq.consumer_lock);

	q = rxe_queue_init(rxe, &srq->rq.max_wr,
			   srq_wqe_size);
	if (!q) {
		pr_warn("unable to allocate queue for srq\n");
		return -ENOMEM;
	}

	srq->rq.queue = q;

	err = do_mmap_info(rxe, udata, false, context, q->buf,
			   q->buf_size, &q->ip);
	if (err)
		return err;

	if (udata && udata->outlen >= sizeof(struct mminfo) + sizeof(u32)) {
		if (copy_to_user(udata->outbuf + sizeof(struct mminfo),
				 &srq->srq_num, sizeof(u32)))
			return -EFAULT;
	}
	return 0;
}

int rxe_srq_from_attr(struct rxe_dev *rxe, struct rxe_srq *srq,
		      struct ib_srq_attr *attr, enum ib_srq_attr_mask mask,
		      struct ib_udata *udata)
{
	int err;
	struct rxe_queue *q = srq->rq.queue;
	struct mminfo mi = { .offset = 1, .size = 0};

	if (mask & IB_SRQ_MAX_WR) {
		/* Check that we can write the mminfo struct to user space */
		if (udata && udata->inlen >= sizeof(__u64)) {
			__u64 mi_addr;

			/* Get address of user space mminfo struct */
			err = ib_copy_from_udata(&mi_addr, udata,
						 sizeof(mi_addr));
			if (err)
				goto err1;

			udata->outbuf = (void __user *)(unsigned long)mi_addr;
			udata->outlen = sizeof(mi);

			if (!access_ok(VERIFY_WRITE,
				       (void __user *)udata->outbuf,
					udata->outlen)) {
				err = -EFAULT;
				goto err1;
			}
		}

		err = rxe_queue_resize(q, (unsigned int *)&attr->max_wr,
				       rcv_wqe_size(srq->rq.max_sge),
				       srq->rq.queue->ip ?
						srq->rq.queue->ip->context :
						NULL,
				       udata, &srq->rq.producer_lock,
				       &srq->rq.consumer_lock);
		if (err)
			goto err2;
	}

	if (mask & IB_SRQ_LIMIT)
		srq->limit = attr->srq_limit;

	return 0;

err2:
	rxe_queue_cleanup(q);
	srq->rq.queue = NULL;
err1:
	return err;
}
