/*
 * Copyright (c) 2015 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>

/* # of WCs to poll for with a single call to ib_poll_cq */
#define IB_POLL_BATCH			16

/* # of WCs to iterate over before yielding */
#define IB_POLL_BUDGET_IRQ		256
#define IB_POLL_BUDGET_WORKQUEUE	65536

#define IB_POLL_FLAGS \
	(IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS)

static int __ib_process_cq(struct ib_cq *cq, int budget)
{
	int i, n, completed = 0;

	while ((n = ib_poll_cq(cq, IB_POLL_BATCH, cq->wc)) > 0) {
		for (i = 0; i < n; i++) {
			struct ib_wc *wc = &cq->wc[i];

			if (wc->wr_cqe)
				wc->wr_cqe->done(cq, wc);
			else
				WARN_ON_ONCE(wc->status == IB_WC_SUCCESS);
		}

		completed += n;

		if (n != IB_POLL_BATCH ||
		    (budget != -1 && completed >= budget))
			break;
	}

	return completed;
}

/**
 * ib_process_direct_cq - process a CQ in caller context
 * @cq:		CQ to process
 * @budget:	number of CQEs to poll for
 *
 * This function is used to process all outstanding CQ entries on a
 * %IB_POLL_DIRECT CQ.  It does not offload CQ processing to a different
 * context and does not ask for completion interrupts from the HCA.
 *
 * Note: do not pass -1 as %budget unless it is guaranteed that the number
 * of completions that will be processed is small.
 */
int ib_process_cq_direct(struct ib_cq *cq, int budget)
{
	WARN_ON_ONCE(cq->poll_ctx != IB_POLL_DIRECT);

	return __ib_process_cq(cq, budget);
}
EXPORT_SYMBOL(ib_process_cq_direct);

static void ib_cq_completion_direct(struct ib_cq *cq, void *private)
{
	WARN_ONCE(1, "got unsolicited completion for CQ 0x%p\n", cq);
}

static int ib_poll_handler(struct irq_poll *iop, int budget)
{
	struct ib_cq *cq = container_of(iop, struct ib_cq, iop);
	int completed;

	completed = __ib_process_cq(cq, budget);
	if (completed < budget) {
		irq_poll_complete(&cq->iop);
		if (ib_req_notify_cq(cq, IB_POLL_FLAGS) > 0)
			irq_poll_sched(&cq->iop);
	}

	return completed;
}

static void ib_cq_completion_softirq(struct ib_cq *cq, void *private)
{
	irq_poll_sched(&cq->iop);
}

static void ib_cq_poll_work(struct work_struct *work)
{
	struct ib_cq *cq = container_of(work, struct ib_cq, work);
	int completed;

	completed = __ib_process_cq(cq, IB_POLL_BUDGET_WORKQUEUE);
	if (completed >= IB_POLL_BUDGET_WORKQUEUE ||
	    ib_req_notify_cq(cq, IB_POLL_FLAGS) > 0)
		queue_work(ib_comp_wq, &cq->work);
}

static void ib_cq_completion_workqueue(struct ib_cq *cq, void *private)
{
	queue_work(ib_comp_wq, &cq->work);
}

/**
 * ib_alloc_cq - allocate a completion queue
 * @dev:		device to allocate the CQ for
 * @private:		driver private data, accessible from cq->cq_context
 * @nr_cqe:		number of CQEs to allocate
 * @comp_vector:	HCA completion vectors for this CQ
 * @poll_ctx:		context to poll the CQ from.
 *
 * This is the proper interface to allocate a CQ for in-kernel users. A
 * CQ allocated with this interface will automatically be polled from the
 * specified context. The ULP must use wr->wr_cqe instead of wr->wr_id
 * to use this CQ abstraction.
 */
struct ib_cq *ib_alloc_cq(struct ib_device *dev, void *private,
		int nr_cqe, int comp_vector, enum ib_poll_context poll_ctx)
{
	struct ib_cq_init_attr cq_attr = {
		.cqe		= nr_cqe,
		.comp_vector	= comp_vector,
	};
	struct ib_cq *cq;
	int ret = -ENOMEM;

	cq = dev->create_cq(dev, &cq_attr, NULL, NULL);
	if (IS_ERR(cq))
		return cq;

	cq->device = dev;
	cq->uobject = NULL;
	cq->event_handler = NULL;
	cq->cq_context = private;
	cq->poll_ctx = poll_ctx;
	atomic_set(&cq->usecnt, 0);

	cq->wc = kmalloc_array(IB_POLL_BATCH, sizeof(*cq->wc), GFP_KERNEL);
	if (!cq->wc)
		goto out_destroy_cq;

	switch (cq->poll_ctx) {
	case IB_POLL_DIRECT:
		cq->comp_handler = ib_cq_completion_direct;
		break;
	case IB_POLL_SOFTIRQ:
		cq->comp_handler = ib_cq_completion_softirq;

		irq_poll_init(&cq->iop, IB_POLL_BUDGET_IRQ, ib_poll_handler);
		ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
		break;
	case IB_POLL_WORKQUEUE:
		cq->comp_handler = ib_cq_completion_workqueue;
		INIT_WORK(&cq->work, ib_cq_poll_work);
		ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
		break;
	default:
		ret = -EINVAL;
		goto out_free_wc;
	}

	return cq;

out_free_wc:
	kfree(cq->wc);
out_destroy_cq:
	cq->device->destroy_cq(cq);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(ib_alloc_cq);

/**
 * ib_free_cq - free a completion queue
 * @cq:		completion queue to free.
 */
void ib_free_cq(struct ib_cq *cq)
{
	int ret;

	if (WARN_ON_ONCE(atomic_read(&cq->usecnt)))
		return;

	switch (cq->poll_ctx) {
	case IB_POLL_DIRECT:
		break;
	case IB_POLL_SOFTIRQ:
		irq_poll_disable(&cq->iop);
		break;
	case IB_POLL_WORKQUEUE:
		flush_work(&cq->work);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	kfree(cq->wc);
	ret = cq->device->destroy_cq(cq);
	WARN_ON_ONCE(ret);
}
EXPORT_SYMBOL(ib_free_cq);
