/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "cq.h"
#include "vt.h"
#include "trace.h"

static struct workqueue_struct *comp_vector_wq;

/**
 * rvt_cq_enter - add a new entry to the completion queue
 * @cq: completion queue
 * @entry: work completion entry to add
 * @solicited: true if @entry is solicited
 *
 * This may be called with qp->s_lock held.
 *
 * Return: return true on success, else return
 * false if cq is full.
 */
bool rvt_cq_enter(struct rvt_cq *cq, struct ib_wc *entry, bool solicited)
{
	struct ib_uverbs_wc *uqueue = NULL;
	struct ib_wc *kqueue = NULL;
	struct rvt_cq_wc *u_wc = NULL;
	struct rvt_k_cq_wc *k_wc = NULL;
	unsigned long flags;
	u32 head;
	u32 next;
	u32 tail;

	spin_lock_irqsave(&cq->lock, flags);

	if (cq->ip) {
		u_wc = cq->queue;
		uqueue = &u_wc->uqueue[0];
		head = RDMA_READ_UAPI_ATOMIC(u_wc->head);
		tail = RDMA_READ_UAPI_ATOMIC(u_wc->tail);
	} else {
		k_wc = cq->kqueue;
		kqueue = &k_wc->kqueue[0];
		head = k_wc->head;
		tail = k_wc->tail;
	}

	/*
	 * Note that the head pointer might be writable by
	 * user processes.Take care to verify it is a sane value.
	 */
	if (head >= (unsigned)cq->ibcq.cqe) {
		head = cq->ibcq.cqe;
		next = 0;
	} else {
		next = head + 1;
	}

	if (unlikely(next == tail || cq->cq_full)) {
		struct rvt_dev_info *rdi = cq->rdi;

		if (!cq->cq_full)
			rvt_pr_err_ratelimited(rdi, "CQ is full!\n");
		cq->cq_full = true;
		spin_unlock_irqrestore(&cq->lock, flags);
		if (cq->ibcq.event_handler) {
			struct ib_event ev;

			ev.device = cq->ibcq.device;
			ev.element.cq = &cq->ibcq;
			ev.event = IB_EVENT_CQ_ERR;
			cq->ibcq.event_handler(&ev, cq->ibcq.cq_context);
		}
		return false;
	}
	trace_rvt_cq_enter(cq, entry, head);
	if (uqueue) {
		uqueue[head].wr_id = entry->wr_id;
		uqueue[head].status = entry->status;
		uqueue[head].opcode = entry->opcode;
		uqueue[head].vendor_err = entry->vendor_err;
		uqueue[head].byte_len = entry->byte_len;
		uqueue[head].ex.imm_data = entry->ex.imm_data;
		uqueue[head].qp_num = entry->qp->qp_num;
		uqueue[head].src_qp = entry->src_qp;
		uqueue[head].wc_flags = entry->wc_flags;
		uqueue[head].pkey_index = entry->pkey_index;
		uqueue[head].slid = ib_lid_cpu16(entry->slid);
		uqueue[head].sl = entry->sl;
		uqueue[head].dlid_path_bits = entry->dlid_path_bits;
		uqueue[head].port_num = entry->port_num;
		/* Make sure entry is written before the head index. */
		RDMA_WRITE_UAPI_ATOMIC(u_wc->head, next);
	} else {
		kqueue[head] = *entry;
		k_wc->head = next;
	}

	if (cq->notify == IB_CQ_NEXT_COMP ||
	    (cq->notify == IB_CQ_SOLICITED &&
	     (solicited || entry->status != IB_WC_SUCCESS))) {
		/*
		 * This will cause send_complete() to be called in
		 * another thread.
		 */
		cq->notify = RVT_CQ_NONE;
		cq->triggered++;
		queue_work_on(cq->comp_vector_cpu, comp_vector_wq,
			      &cq->comptask);
	}

	spin_unlock_irqrestore(&cq->lock, flags);
	return true;
}
EXPORT_SYMBOL(rvt_cq_enter);

static void send_complete(struct work_struct *work)
{
	struct rvt_cq *cq = container_of(work, struct rvt_cq, comptask);

	/*
	 * The completion handler will most likely rearm the notification
	 * and poll for all pending entries.  If a new completion entry
	 * is added while we are in this routine, queue_work()
	 * won't call us again until we return so we check triggered to
	 * see if we need to call the handler again.
	 */
	for (;;) {
		u8 triggered = cq->triggered;

		/*
		 * IPoIB connected mode assumes the callback is from a
		 * soft IRQ. We simulate this by blocking "bottom halves".
		 * See the implementation for ipoib_cm_handle_tx_wc(),
		 * netif_tx_lock_bh() and netif_tx_lock().
		 */
		local_bh_disable();
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
		local_bh_enable();

		if (cq->triggered == triggered)
			return;
	}
}

/**
 * rvt_create_cq - create a completion queue
 * @ibcq: Allocated CQ
 * @attr: creation attributes
 * @udata: user data for libibverbs.so
 *
 * Called by ib_create_cq() in the generic verbs code.
 *
 * Return: 0 on success
 */
int rvt_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata)
{
	struct ib_device *ibdev = ibcq->device;
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	struct rvt_cq *cq = ibcq_to_rvtcq(ibcq);
	struct rvt_cq_wc *u_wc = NULL;
	struct rvt_k_cq_wc *k_wc = NULL;
	u32 sz;
	unsigned int entries = attr->cqe;
	int comp_vector = attr->comp_vector;
	int err;

	if (attr->flags)
		return -EINVAL;

	if (entries < 1 || entries > rdi->dparms.props.max_cqe)
		return -EINVAL;

	if (comp_vector < 0)
		comp_vector = 0;

	comp_vector = comp_vector % rdi->ibdev.num_comp_vectors;

	/*
	 * Allocate the completion queue entries and head/tail pointers.
	 * This is allocated separately so that it can be resized and
	 * also mapped into user space.
	 * We need to use vmalloc() in order to support mmap and large
	 * numbers of entries.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		sz = sizeof(struct ib_uverbs_wc) * (entries + 1);
		sz += sizeof(*u_wc);
		u_wc = vmalloc_user(sz);
		if (!u_wc)
			return -ENOMEM;
	} else {
		sz = sizeof(struct ib_wc) * (entries + 1);
		sz += sizeof(*k_wc);
		k_wc = vzalloc_node(sz, rdi->dparms.node);
		if (!k_wc)
			return -ENOMEM;
	}

	/*
	 * Return the address of the WC as the offset to mmap.
	 * See rvt_mmap() for details.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		cq->ip = rvt_create_mmap_info(rdi, sz, udata, u_wc);
		if (!cq->ip) {
			err = -ENOMEM;
			goto bail_wc;
		}

		err = ib_copy_to_udata(udata, &cq->ip->offset,
				       sizeof(cq->ip->offset));
		if (err)
			goto bail_ip;
	}

	spin_lock_irq(&rdi->n_cqs_lock);
	if (rdi->n_cqs_allocated == rdi->dparms.props.max_cq) {
		spin_unlock_irq(&rdi->n_cqs_lock);
		err = -ENOMEM;
		goto bail_ip;
	}

	rdi->n_cqs_allocated++;
	spin_unlock_irq(&rdi->n_cqs_lock);

	if (cq->ip) {
		spin_lock_irq(&rdi->pending_lock);
		list_add(&cq->ip->pending_mmaps, &rdi->pending_mmaps);
		spin_unlock_irq(&rdi->pending_lock);
	}

	/*
	 * ib_create_cq() will initialize cq->ibcq except for cq->ibcq.cqe.
	 * The number of entries should be >= the number requested or return
	 * an error.
	 */
	cq->rdi = rdi;
	if (rdi->driver_f.comp_vect_cpu_lookup)
		cq->comp_vector_cpu =
			rdi->driver_f.comp_vect_cpu_lookup(rdi, comp_vector);
	else
		cq->comp_vector_cpu =
			cpumask_first(cpumask_of_node(rdi->dparms.node));

	cq->ibcq.cqe = entries;
	cq->notify = RVT_CQ_NONE;
	spin_lock_init(&cq->lock);
	INIT_WORK(&cq->comptask, send_complete);
	if (u_wc)
		cq->queue = u_wc;
	else
		cq->kqueue = k_wc;

	trace_rvt_create_cq(cq, attr);
	return 0;

bail_ip:
	kfree(cq->ip);
bail_wc:
	vfree(u_wc);
	vfree(k_wc);
	return err;
}

/**
 * rvt_destroy_cq - destroy a completion queue
 * @ibcq: the completion queue to destroy.
 * @udata: user data or NULL for kernel object
 *
 * Called by ib_destroy_cq() in the generic verbs code.
 */
void rvt_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct rvt_cq *cq = ibcq_to_rvtcq(ibcq);
	struct rvt_dev_info *rdi = cq->rdi;

	flush_work(&cq->comptask);
	spin_lock_irq(&rdi->n_cqs_lock);
	rdi->n_cqs_allocated--;
	spin_unlock_irq(&rdi->n_cqs_lock);
	if (cq->ip)
		kref_put(&cq->ip->ref, rvt_release_mmap_info);
	else
		vfree(cq->queue);
}

/**
 * rvt_req_notify_cq - change the notification type for a completion queue
 * @ibcq: the completion queue
 * @notify_flags: the type of notification to request
 *
 * This may be called from interrupt context.  Also called by
 * ib_req_notify_cq() in the generic verbs code.
 *
 * Return: 0 for success.
 */
int rvt_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags)
{
	struct rvt_cq *cq = ibcq_to_rvtcq(ibcq);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&cq->lock, flags);
	/*
	 * Don't change IB_CQ_NEXT_COMP to IB_CQ_SOLICITED but allow
	 * any other transitions (see C11-31 and C11-32 in ch. 11.4.2.2).
	 */
	if (cq->notify != IB_CQ_NEXT_COMP)
		cq->notify = notify_flags & IB_CQ_SOLICITED_MASK;

	if (notify_flags & IB_CQ_REPORT_MISSED_EVENTS) {
		if (cq->queue) {
			if (RDMA_READ_UAPI_ATOMIC(cq->queue->head) !=
				RDMA_READ_UAPI_ATOMIC(cq->queue->tail))
				ret = 1;
		} else {
			if (cq->kqueue->head != cq->kqueue->tail)
				ret = 1;
		}
	}

	spin_unlock_irqrestore(&cq->lock, flags);

	return ret;
}

/**
 * rvt_resize_cq - change the size of the CQ
 * @ibcq: the completion queue
 *
 * Return: 0 for success.
 */
int rvt_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata)
{
	struct rvt_cq *cq = ibcq_to_rvtcq(ibcq);
	u32 head, tail, n;
	int ret;
	u32 sz;
	struct rvt_dev_info *rdi = cq->rdi;
	struct rvt_cq_wc *u_wc = NULL;
	struct rvt_cq_wc *old_u_wc = NULL;
	struct rvt_k_cq_wc *k_wc = NULL;
	struct rvt_k_cq_wc *old_k_wc = NULL;

	if (cqe < 1 || cqe > rdi->dparms.props.max_cqe)
		return -EINVAL;

	/*
	 * Need to use vmalloc() if we want to support large #s of entries.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		sz = sizeof(struct ib_uverbs_wc) * (cqe + 1);
		sz += sizeof(*u_wc);
		u_wc = vmalloc_user(sz);
		if (!u_wc)
			return -ENOMEM;
	} else {
		sz = sizeof(struct ib_wc) * (cqe + 1);
		sz += sizeof(*k_wc);
		k_wc = vzalloc_node(sz, rdi->dparms.node);
		if (!k_wc)
			return -ENOMEM;
	}
	/* Check that we can write the offset to mmap. */
	if (udata && udata->outlen >= sizeof(__u64)) {
		__u64 offset = 0;

		ret = ib_copy_to_udata(udata, &offset, sizeof(offset));
		if (ret)
			goto bail_free;
	}

	spin_lock_irq(&cq->lock);
	/*
	 * Make sure head and tail are sane since they
	 * might be user writable.
	 */
	if (u_wc) {
		old_u_wc = cq->queue;
		head = RDMA_READ_UAPI_ATOMIC(old_u_wc->head);
		tail = RDMA_READ_UAPI_ATOMIC(old_u_wc->tail);
	} else {
		old_k_wc = cq->kqueue;
		head = old_k_wc->head;
		tail = old_k_wc->tail;
	}

	if (head > (u32)cq->ibcq.cqe)
		head = (u32)cq->ibcq.cqe;
	if (tail > (u32)cq->ibcq.cqe)
		tail = (u32)cq->ibcq.cqe;
	if (head < tail)
		n = cq->ibcq.cqe + 1 + head - tail;
	else
		n = head - tail;
	if (unlikely((u32)cqe < n)) {
		ret = -EINVAL;
		goto bail_unlock;
	}
	for (n = 0; tail != head; n++) {
		if (u_wc)
			u_wc->uqueue[n] = old_u_wc->uqueue[tail];
		else
			k_wc->kqueue[n] = old_k_wc->kqueue[tail];
		if (tail == (u32)cq->ibcq.cqe)
			tail = 0;
		else
			tail++;
	}
	cq->ibcq.cqe = cqe;
	if (u_wc) {
		RDMA_WRITE_UAPI_ATOMIC(u_wc->head, n);
		RDMA_WRITE_UAPI_ATOMIC(u_wc->tail, 0);
		cq->queue = u_wc;
	} else {
		k_wc->head = n;
		k_wc->tail = 0;
		cq->kqueue = k_wc;
	}
	spin_unlock_irq(&cq->lock);

	if (u_wc)
		vfree(old_u_wc);
	else
		vfree(old_k_wc);

	if (cq->ip) {
		struct rvt_mmap_info *ip = cq->ip;

		rvt_update_mmap_info(rdi, ip, sz, u_wc);

		/*
		 * Return the offset to mmap.
		 * See rvt_mmap() for details.
		 */
		if (udata && udata->outlen >= sizeof(__u64)) {
			ret = ib_copy_to_udata(udata, &ip->offset,
					       sizeof(ip->offset));
			if (ret)
				return ret;
		}

		spin_lock_irq(&rdi->pending_lock);
		if (list_empty(&ip->pending_mmaps))
			list_add(&ip->pending_mmaps, &rdi->pending_mmaps);
		spin_unlock_irq(&rdi->pending_lock);
	}

	return 0;

bail_unlock:
	spin_unlock_irq(&cq->lock);
bail_free:
	vfree(u_wc);
	vfree(k_wc);

	return ret;
}

/**
 * rvt_poll_cq - poll for work completion entries
 * @ibcq: the completion queue to poll
 * @num_entries: the maximum number of entries to return
 * @entry: pointer to array where work completions are placed
 *
 * This may be called from interrupt context.  Also called by ib_poll_cq()
 * in the generic verbs code.
 *
 * Return: the number of completion entries polled.
 */
int rvt_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry)
{
	struct rvt_cq *cq = ibcq_to_rvtcq(ibcq);
	struct rvt_k_cq_wc *wc;
	unsigned long flags;
	int npolled;
	u32 tail;

	/* The kernel can only poll a kernel completion queue */
	if (cq->ip)
		return -EINVAL;

	spin_lock_irqsave(&cq->lock, flags);

	wc = cq->kqueue;
	tail = wc->tail;
	if (tail > (u32)cq->ibcq.cqe)
		tail = (u32)cq->ibcq.cqe;
	for (npolled = 0; npolled < num_entries; ++npolled, ++entry) {
		if (tail == wc->head)
			break;
		/* The kernel doesn't need a RMB since it has the lock. */
		trace_rvt_cq_poll(cq, &wc->kqueue[tail], npolled);
		*entry = wc->kqueue[tail];
		if (tail >= cq->ibcq.cqe)
			tail = 0;
		else
			tail++;
	}
	wc->tail = tail;

	spin_unlock_irqrestore(&cq->lock, flags);

	return npolled;
}

/**
 * rvt_driver_cq_init - Init cq resources on behalf of driver
 * @rdi: rvt dev structure
 *
 * Return: 0 on success
 */
int rvt_driver_cq_init(void)
{
	comp_vector_wq = alloc_workqueue("%s", WQ_HIGHPRI | WQ_CPU_INTENSIVE,
					 0, "rdmavt_cq");
	if (!comp_vector_wq)
		return -ENOMEM;

	return 0;
}

/**
 * rvt_cq_exit - tear down cq reources
 * @rdi: rvt dev structure
 */
void rvt_cq_exit(void)
{
	destroy_workqueue(comp_vector_wq);
	comp_vector_wq = NULL;
}
