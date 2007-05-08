/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
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
 * This may be called with qp->s_lock held.
 */
void ipath_cq_enter(struct ipath_cq *cq, struct ib_wc *entry, int solicited)
{
	struct ipath_cq_wc *wc;
	unsigned long flags;
	u32 head;
	u32 next;

	spin_lock_irqsave(&cq->lock, flags);

	/*
	 * Note that the head pointer might be writable by user processes.
	 * Take care to verify it is a sane value.
	 */
	wc = cq->queue;
	head = wc->head;
	if (head >= (unsigned) cq->ibcq.cqe) {
		head = cq->ibcq.cqe;
		next = 0;
	} else
		next = head + 1;
	if (unlikely(next == wc->tail)) {
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
	wc->queue[head].wr_id = entry->wr_id;
	wc->queue[head].status = entry->status;
	wc->queue[head].opcode = entry->opcode;
	wc->queue[head].vendor_err = entry->vendor_err;
	wc->queue[head].byte_len = entry->byte_len;
	wc->queue[head].imm_data = (__u32 __force)entry->imm_data;
	wc->queue[head].qp_num = entry->qp->qp_num;
	wc->queue[head].src_qp = entry->src_qp;
	wc->queue[head].wc_flags = entry->wc_flags;
	wc->queue[head].pkey_index = entry->pkey_index;
	wc->queue[head].slid = entry->slid;
	wc->queue[head].sl = entry->sl;
	wc->queue[head].dlid_path_bits = entry->dlid_path_bits;
	wc->queue[head].port_num = entry->port_num;
	wc->head = next;

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
	struct ipath_cq_wc *wc;
	unsigned long flags;
	int npolled;
	u32 tail;

	spin_lock_irqsave(&cq->lock, flags);

	wc = cq->queue;
	tail = wc->tail;
	if (tail > (u32) cq->ibcq.cqe)
		tail = (u32) cq->ibcq.cqe;
	for (npolled = 0; npolled < num_entries; ++npolled, ++entry) {
		struct ipath_qp *qp;

		if (tail == wc->head)
			break;

		qp = ipath_lookup_qpn(&to_idev(cq->ibcq.device)->qp_table,
				      wc->queue[tail].qp_num);
		entry->qp = &qp->ibqp;
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);

		entry->wr_id = wc->queue[tail].wr_id;
		entry->status = wc->queue[tail].status;
		entry->opcode = wc->queue[tail].opcode;
		entry->vendor_err = wc->queue[tail].vendor_err;
		entry->byte_len = wc->queue[tail].byte_len;
		entry->imm_data = wc->queue[tail].imm_data;
		entry->src_qp = wc->queue[tail].src_qp;
		entry->wc_flags = wc->queue[tail].wc_flags;
		entry->pkey_index = wc->queue[tail].pkey_index;
		entry->slid = wc->queue[tail].slid;
		entry->sl = wc->queue[tail].sl;
		entry->dlid_path_bits = wc->queue[tail].dlid_path_bits;
		entry->port_num = wc->queue[tail].port_num;
		if (tail >= cq->ibcq.cqe)
			tail = 0;
		else
			tail++;
	}
	wc->tail = tail;

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
struct ib_cq *ipath_create_cq(struct ib_device *ibdev, int entries, int comp_vector,
			      struct ib_ucontext *context,
			      struct ib_udata *udata)
{
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_cq *cq;
	struct ipath_cq_wc *wc;
	struct ib_cq *ret;

	if (entries < 1 || entries > ib_ipath_max_cqes) {
		ret = ERR_PTR(-EINVAL);
		goto done;
	}

	/* Allocate the completion queue structure. */
	cq = kmalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		ret = ERR_PTR(-ENOMEM);
		goto done;
	}

	/*
	 * Allocate the completion queue entries and head/tail pointers.
	 * This is allocated separately so that it can be resized and
	 * also mapped into user space.
	 * We need to use vmalloc() in order to support mmap and large
	 * numbers of entries.
	 */
	wc = vmalloc_user(sizeof(*wc) + sizeof(struct ib_wc) * entries);
	if (!wc) {
		ret = ERR_PTR(-ENOMEM);
		goto bail_cq;
	}

	/*
	 * Return the address of the WC as the offset to mmap.
	 * See ipath_mmap() for details.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		int err;
		u32 s = sizeof *wc + sizeof(struct ib_wc) * entries;

		cq->ip = ipath_create_mmap_info(dev, s, context, wc);
		if (!cq->ip) {
			ret = ERR_PTR(-ENOMEM);
			goto bail_wc;
		}

		err = ib_copy_to_udata(udata, &cq->ip->offset,
				       sizeof(cq->ip->offset));
		if (err) {
			ret = ERR_PTR(err);
			goto bail_ip;
		}
	} else
		cq->ip = NULL;

	spin_lock(&dev->n_cqs_lock);
	if (dev->n_cqs_allocated == ib_ipath_max_cqs) {
		spin_unlock(&dev->n_cqs_lock);
		ret = ERR_PTR(-ENOMEM);
		goto bail_ip;
	}

	dev->n_cqs_allocated++;
	spin_unlock(&dev->n_cqs_lock);

	if (cq->ip) {
		spin_lock_irq(&dev->pending_lock);
		list_add(&cq->ip->pending_mmaps, &dev->pending_mmaps);
		spin_unlock_irq(&dev->pending_lock);
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
	wc->head = 0;
	wc->tail = 0;
	cq->queue = wc;

	ret = &cq->ibcq;

	goto done;

bail_ip:
	kfree(cq->ip);
bail_wc:
	vfree(wc);
bail_cq:
	kfree(cq);
done:
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
	struct ipath_ibdev *dev = to_idev(ibcq->device);
	struct ipath_cq *cq = to_icq(ibcq);

	tasklet_kill(&cq->comptask);
	spin_lock(&dev->n_cqs_lock);
	dev->n_cqs_allocated--;
	spin_unlock(&dev->n_cqs_lock);
	if (cq->ip)
		kref_put(&cq->ip->ref, ipath_release_mmap_info);
	else
		vfree(cq->queue);
	kfree(cq);

	return 0;
}

/**
 * ipath_req_notify_cq - change the notification type for a completion queue
 * @ibcq: the completion queue
 * @notify_flags: the type of notification to request
 *
 * Returns 0 for success.
 *
 * This may be called from interrupt context.  Also called by
 * ib_req_notify_cq() in the generic verbs code.
 */
int ipath_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags)
{
	struct ipath_cq *cq = to_icq(ibcq);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&cq->lock, flags);
	/*
	 * Don't change IB_CQ_NEXT_COMP to IB_CQ_SOLICITED but allow
	 * any other transitions (see C11-31 and C11-32 in ch. 11.4.2.2).
	 */
	if (cq->notify != IB_CQ_NEXT_COMP)
		cq->notify = notify_flags & IB_CQ_SOLICITED_MASK;

	if ((notify_flags & IB_CQ_REPORT_MISSED_EVENTS) &&
	    cq->queue->head != cq->queue->tail)
		ret = 1;

	spin_unlock_irqrestore(&cq->lock, flags);

	return ret;
}

/**
 * ipath_resize_cq - change the size of the CQ
 * @ibcq: the completion queue
 *
 * Returns 0 for success.
 */
int ipath_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata)
{
	struct ipath_cq *cq = to_icq(ibcq);
	struct ipath_cq_wc *old_wc;
	struct ipath_cq_wc *wc;
	u32 head, tail, n;
	int ret;

	if (cqe < 1 || cqe > ib_ipath_max_cqes) {
		ret = -EINVAL;
		goto bail;
	}

	/*
	 * Need to use vmalloc() if we want to support large #s of entries.
	 */
	wc = vmalloc_user(sizeof(*wc) + sizeof(struct ib_wc) * cqe);
	if (!wc) {
		ret = -ENOMEM;
		goto bail;
	}

	/*
	 * Return the address of the WC as the offset to mmap.
	 * See ipath_mmap() for details.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		__u64 offset = (__u64) wc;

		ret = ib_copy_to_udata(udata, &offset, sizeof(offset));
		if (ret)
			goto bail;
	}

	spin_lock_irq(&cq->lock);
	/*
	 * Make sure head and tail are sane since they
	 * might be user writable.
	 */
	old_wc = cq->queue;
	head = old_wc->head;
	if (head > (u32) cq->ibcq.cqe)
		head = (u32) cq->ibcq.cqe;
	tail = old_wc->tail;
	if (tail > (u32) cq->ibcq.cqe)
		tail = (u32) cq->ibcq.cqe;
	if (head < tail)
		n = cq->ibcq.cqe + 1 + head - tail;
	else
		n = head - tail;
	if (unlikely((u32)cqe < n)) {
		spin_unlock_irq(&cq->lock);
		vfree(wc);
		ret = -EOVERFLOW;
		goto bail;
	}
	for (n = 0; tail != head; n++) {
		wc->queue[n] = old_wc->queue[tail];
		if (tail == (u32) cq->ibcq.cqe)
			tail = 0;
		else
			tail++;
	}
	cq->ibcq.cqe = cqe;
	wc->head = n;
	wc->tail = 0;
	cq->queue = wc;
	spin_unlock_irq(&cq->lock);

	vfree(old_wc);

	if (cq->ip) {
		struct ipath_ibdev *dev = to_idev(ibcq->device);
		struct ipath_mmap_info *ip = cq->ip;
		u32 s = sizeof *wc + sizeof(struct ib_wc) * cqe;

		ipath_update_mmap_info(dev, ip, s, wc);
		spin_lock_irq(&dev->pending_lock);
		if (list_empty(&ip->pending_mmaps))
			list_add(&ip->pending_mmaps, &dev->pending_mmaps);
		spin_unlock_irq(&dev->pending_lock);
	}

	ret = 0;

bail:
	return ret;
}
