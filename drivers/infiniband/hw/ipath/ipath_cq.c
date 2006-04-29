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
 * ipath_cq_enter - add a new entry to the completion queue
 * @cq: completion queue
 * @entry: work completion entry to add
 * @sig: true if @entry is a solicitated entry
 *
 * This may be called with one of the qp->s_lock or qp->r_rq.lock held.
 */
void ipath_cq_enter(struct ipath_cq *cq, struct ib_wc *entry, int solicited)
{
	unsigned long flags;
	u32 next;

	spin_lock_irqsave(&cq->lock, flags);

	if (cq->head == cq->ibcq.cqe)
		next = 0;
	else
		next = cq->head + 1;
	if (unlikely(next == cq->tail)) {
		spin_unlock_irqrestore(&cq->lock, flags);
		if (cq->ibcq.event_handler) {
			struct ib_event ev;

			ev.device = cq->ibcq.device;
			ev.element.cq = &cq->ibcq;
			ev.event = IB_EVENT_CQ_ERR;
			cq->ibcq.event_handler(&ev, cq->ibcq.cq_context);
		}
		return;
	}
	cq->queue[cq->head] = *entry;
	cq->head = next;

	if (cq->notify == IB_CQ_NEXT_COMP ||
	    (cq->notify == IB_CQ_SOLICITED && solicited)) {
		cq->notify = IB_CQ_NONE;
		cq->triggered++;
		/*
		 * This will cause send_complete() to be called in
		 * another thread.
		 */
		tasklet_hi_schedule(&cq->comptask);
	}

	spin_unlock_irqrestore(&cq->lock, flags);

	if (entry->status != IB_WC_SUCCESS)
		to_idev(cq->ibcq.device)->n_wqe_errs++;
}

/**
 * ipath_poll_cq - poll for work completion entries
 * @ibcq: the completion queue to poll
 * @num_entries: the maximum number of entries to return
 * @entry: pointer to array where work completions are placed
 *
 * Returns the number of completion entries polled.
 *
 * This may be called from interrupt context.  Also called by ib_poll_cq()
 * in the generic verbs code.
 */
int ipath_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry)
{
	struct ipath_cq *cq = to_icq(ibcq);
	unsigned long flags;
	int npolled;

	spin_lock_irqsave(&cq->lock, flags);

	for (npolled = 0; npolled < num_entries; ++npolled, ++entry) {
		if (cq->tail == cq->head)
			break;
		*entry = cq->queue[cq->tail];
		if (cq->tail == cq->ibcq.cqe)
			cq->tail = 0;
		else
			cq->tail++;
	}

	spin_unlock_irqrestore(&cq->lock, flags);

	return npolled;
}

static void send_complete(unsigned long data)
{
	struct ipath_cq *cq = (struct ipath_cq *)data;

	/*
	 * The completion handler will most likely rearm the notification
	 * and poll for all pending entries.  If a new completion entry
	 * is added while we are in this routine, tasklet_hi_schedule()
	 * won't call us again until we return so we check triggered to
	 * see if we need to call the handler again.
	 */
	for (;;) {
		u8 triggered = cq->triggered;

		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);

		if (cq->triggered == triggered)
			return;
	}
}

/**
 * ipath_create_cq - create a completion queue
 * @ibdev: the device this completion queue is attached to
 * @entries: the minimum size of the completion queue
 * @context: unused by the InfiniPath driver
 * @udata: unused by the InfiniPath driver
 *
 * Returns a pointer to the completion queue or negative errno values
 * for failure.
 *
 * Called by ib_create_cq() in the generic verbs code.
 */
struct ib_cq *ipath_create_cq(struct ib_device *ibdev, int entries,
			      struct ib_ucontext *context,
			      struct ib_udata *udata)
{
	struct ipath_cq *cq;
	struct ib_wc *wc;
	struct ib_cq *ret;

	/*
	 * Need to use vmalloc() if we want to support large #s of
	 * entries.
	 */
	cq = kmalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	/*
	 * Need to use vmalloc() if we want to support large #s of entries.
	 */
	wc = vmalloc(sizeof(*wc) * (entries + 1));
	if (!wc) {
		kfree(cq);
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}
	/*
	 * ib_create_cq() will initialize cq->ibcq except for cq->ibcq.cqe.
	 * The number of entries should be >= the number requested or return
	 * an error.
	 */
	cq->ibcq.cqe = entries;
	cq->notify = IB_CQ_NONE;
	cq->triggered = 0;
	spin_lock_init(&cq->lock);
	tasklet_init(&cq->comptask, send_complete, (unsigned long)cq);
	cq->head = 0;
	cq->tail = 0;
	cq->queue = wc;

	ret = &cq->ibcq;

bail:
	return ret;
}

/**
 * ipath_destroy_cq - destroy a completion queue
 * @ibcq: the completion queue to destroy.
 *
 * Returns 0 for success.
 *
 * Called by ib_destroy_cq() in the generic verbs code.
 */
int ipath_destroy_cq(struct ib_cq *ibcq)
{
	struct ipath_cq *cq = to_icq(ibcq);

	tasklet_kill(&cq->comptask);
	vfree(cq->queue);
	kfree(cq);

	return 0;
}

/**
 * ipath_req_notify_cq - change the notification type for a completion queue
 * @ibcq: the completion queue
 * @notify: the type of notification to request
 *
 * Returns 0 for success.
 *
 * This may be called from interrupt context.  Also called by
 * ib_req_notify_cq() in the generic verbs code.
 */
int ipath_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify notify)
{
	struct ipath_cq *cq = to_icq(ibcq);
	unsigned long flags;

	spin_lock_irqsave(&cq->lock, flags);
	/*
	 * Don't change IB_CQ_NEXT_COMP to IB_CQ_SOLICITED but allow
	 * any other transitions.
	 */
	if (cq->notify != IB_CQ_NEXT_COMP)
		cq->notify = notify;
	spin_unlock_irqrestore(&cq->lock, flags);
	return 0;
}

int ipath_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata)
{
	struct ipath_cq *cq = to_icq(ibcq);
	struct ib_wc *wc, *old_wc;
	u32 n;
	int ret;

	/*
	 * Need to use vmalloc() if we want to support large #s of entries.
	 */
	wc = vmalloc(sizeof(*wc) * (cqe + 1));
	if (!wc) {
		ret = -ENOMEM;
		goto bail;
	}

	spin_lock_irq(&cq->lock);
	if (cq->head < cq->tail)
		n = cq->ibcq.cqe + 1 + cq->head - cq->tail;
	else
		n = cq->head - cq->tail;
	if (unlikely((u32)cqe < n)) {
		spin_unlock_irq(&cq->lock);
		vfree(wc);
		ret = -EOVERFLOW;
		goto bail;
	}
	for (n = 0; cq->tail != cq->head; n++) {
		wc[n] = cq->queue[cq->tail];
		if (cq->tail == cq->ibcq.cqe)
			cq->tail = 0;
		else
			cq->tail++;
	}
	cq->ibcq.cqe = cqe;
	cq->head = n;
	cq->tail = 0;
	old_wc = cq->queue;
	cq->queue = wc;
	spin_unlock_irq(&cq->lock);

	vfree(old_wc);

	ret = 0;

bail:
	return ret;
}
