/*
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#include <linux/err.h>
#include <linux/vmalloc.h>

#include "ipath_verbs.h"

/**
 * ipath_post_srq_receive - post a receive on a shared receive queue
 * @ibsrq: the SRQ to post the receive on
 * @wr: the list of work requests to post
 * @bad_wr: the first WR to cause a problem is put here
 *
 * This may be called from interrupt context.
 */
int ipath_post_srq_receive(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			   struct ib_recv_wr **bad_wr)
{
	struct ipath_srq *srq = to_isrq(ibsrq);
	struct ipath_ibdev *dev = to_idev(ibsrq->device);
	unsigned long flags;
	int ret;

	for (; wr; wr = wr->next) {
		struct ipath_rwqe *wqe;
		u32 next;
		int i, j;

		if (wr->num_sge > srq->rq.max_sge) {
			*bad_wr = wr;
			ret = -ENOMEM;
			goto bail;
		}

		spin_lock_irqsave(&srq->rq.lock, flags);
		next = srq->rq.head + 1;
		if (next >= srq->rq.size)
			next = 0;
		if (next == srq->rq.tail) {
			spin_unlock_irqrestore(&srq->rq.lock, flags);
			*bad_wr = wr;
			ret = -ENOMEM;
			goto bail;
		}

		wqe = get_rwqe_ptr(&srq->rq, srq->rq.head);
		wqe->wr_id = wr->wr_id;
		wqe->sg_list[0].mr = NULL;
		wqe->sg_list[0].vaddr = NULL;
		wqe->sg_list[0].length = 0;
		wqe->sg_list[0].sge_length = 0;
		wqe->length = 0;
		for (i = 0, j = 0; i < wr->num_sge; i++) {
			/* Check LKEY */
			if (to_ipd(srq->ibsrq.pd)->user &&
			    wr->sg_list[i].lkey == 0) {
				spin_unlock_irqrestore(&srq->rq.lock,
						       flags);
				*bad_wr = wr;
				ret = -EINVAL;
				goto bail;
			}
			if (wr->sg_list[i].length == 0)
				continue;
			if (!ipath_lkey_ok(&dev->lk_table,
					   &wqe->sg_list[j],
					   &wr->sg_list[i],
					   IB_ACCESS_LOCAL_WRITE)) {
				spin_unlock_irqrestore(&srq->rq.lock,
						       flags);
				*bad_wr = wr;
				ret = -EINVAL;
				goto bail;
			}
			wqe->length += wr->sg_list[i].length;
			j++;
		}
		wqe->num_sge = j;
		srq->rq.head = next;
		spin_unlock_irqrestore(&srq->rq.lock, flags);
	}
	ret = 0;

bail:
	return ret;
}

/**
 * ipath_create_srq - create a shared receive queue
 * @ibpd: the protection domain of the SRQ to create
 * @attr: the attributes of the SRQ
 * @udata: not used by the InfiniPath verbs driver
 */
struct ib_srq *ipath_create_srq(struct ib_pd *ibpd,
				struct ib_srq_init_attr *srq_init_attr,
				struct ib_udata *udata)
{
	struct ipath_srq *srq;
	u32 sz;
	struct ib_srq *ret;

	if (srq_init_attr->attr.max_sge < 1) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	srq = kmalloc(sizeof(*srq), GFP_KERNEL);
	if (!srq) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	/*
	 * Need to use vmalloc() if we want to support large #s of entries.
	 */
	srq->rq.size = srq_init_attr->attr.max_wr + 1;
	sz = sizeof(struct ipath_sge) * srq_init_attr->attr.max_sge +
		sizeof(struct ipath_rwqe);
	srq->rq.wq = vmalloc(srq->rq.size * sz);
	if (!srq->rq.wq) {
		kfree(srq);
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	/*
	 * ib_create_srq() will initialize srq->ibsrq.
	 */
	spin_lock_init(&srq->rq.lock);
	srq->rq.head = 0;
	srq->rq.tail = 0;
	srq->rq.max_sge = srq_init_attr->attr.max_sge;
	srq->limit = srq_init_attr->attr.srq_limit;

	ret = &srq->ibsrq;

bail:
	return ret;
}

/**
 * ipath_modify_srq - modify a shared receive queue
 * @ibsrq: the SRQ to modify
 * @attr: the new attributes of the SRQ
 * @attr_mask: indicates which attributes to modify
 */
int ipath_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		     enum ib_srq_attr_mask attr_mask)
{
	struct ipath_srq *srq = to_isrq(ibsrq);
	unsigned long flags;
	int ret;

	if (attr_mask & IB_SRQ_LIMIT) {
		spin_lock_irqsave(&srq->rq.lock, flags);
		srq->limit = attr->srq_limit;
		spin_unlock_irqrestore(&srq->rq.lock, flags);
	}
	if (attr_mask & IB_SRQ_MAX_WR) {
		u32 size = attr->max_wr + 1;
		struct ipath_rwqe *wq, *p;
		u32 n;
		u32 sz;

		if (attr->max_sge < srq->rq.max_sge) {
			ret = -EINVAL;
			goto bail;
		}

		sz = sizeof(struct ipath_rwqe) +
			attr->max_sge * sizeof(struct ipath_sge);
		wq = vmalloc(size * sz);
		if (!wq) {
			ret = -ENOMEM;
			goto bail;
		}

		spin_lock_irqsave(&srq->rq.lock, flags);
		if (srq->rq.head < srq->rq.tail)
			n = srq->rq.size + srq->rq.head - srq->rq.tail;
		else
			n = srq->rq.head - srq->rq.tail;
		if (size <= n || size <= srq->limit) {
			spin_unlock_irqrestore(&srq->rq.lock, flags);
			vfree(wq);
			ret = -EINVAL;
			goto bail;
		}
		n = 0;
		p = wq;
		while (srq->rq.tail != srq->rq.head) {
			struct ipath_rwqe *wqe;
			int i;

			wqe = get_rwqe_ptr(&srq->rq, srq->rq.tail);
			p->wr_id = wqe->wr_id;
			p->length = wqe->length;
			p->num_sge = wqe->num_sge;
			for (i = 0; i < wqe->num_sge; i++)
				p->sg_list[i] = wqe->sg_list[i];
			n++;
			p = (struct ipath_rwqe *)((char *) p + sz);
			if (++srq->rq.tail >= srq->rq.size)
				srq->rq.tail = 0;
		}
		vfree(srq->rq.wq);
		srq->rq.wq = wq;
		srq->rq.size = size;
		srq->rq.head = n;
		srq->rq.tail = 0;
		srq->rq.max_sge = attr->max_sge;
		spin_unlock_irqrestore(&srq->rq.lock, flags);
	}

	ret = 0;

bail:
	return ret;
}

int ipath_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr)
{
	struct ipath_srq *srq = to_isrq(ibsrq);

	attr->max_wr = srq->rq.size - 1;
	attr->max_sge = srq->rq.max_sge;
	attr->srq_limit = srq->limit;
	return 0;
}

/**
 * ipath_destroy_srq - destroy a shared receive queue
 * @ibsrq: the SRQ to destroy
 */
int ipath_destroy_srq(struct ib_srq *ibsrq)
{
	struct ipath_srq *srq = to_isrq(ibsrq);

	vfree(srq->rq.wq);
	kfree(srq);

	return 0;
}
