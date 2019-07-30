// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 HGST, a Western Digital Company.
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>

/* # of WCs to poll for with a single call to ib_poll_cq */
#define IB_POLL_BATCH			16
#define IB_POLL_BATCH_DIRECT		8

/* # of WCs to iterate over before yielding */
#define IB_POLL_BUDGET_IRQ		256
#define IB_POLL_BUDGET_WORKQUEUE	65536

#define IB_POLL_FLAGS \
	(IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS)

static int __ib_process_cq(struct ib_cq *cq, int budget, struct ib_wc *wcs,
			   int batch)
{
	int i, n, completed = 0;

	/*
	 * budget might be (-1) if the caller does not
	 * want to bound this call, thus we need unsigned
	 * minimum here.
	 */
	while ((n = ib_poll_cq(cq, min_t(u32, batch,
					 budget - completed), wcs)) > 0) {
		for (i = 0; i < n; i++) {
			struct ib_wc *wc = &wcs[i];

			if (wc->wr_cqe)
				wc->wr_cqe->done(cq, wc);
			else
				WARN_ON_ONCE(wc->status == IB_WC_SUCCESS);
		}

		completed += n;

		if (n != batch || (budget != -1 && completed >= budget))
			break;
	}

	return completed;
}

/**
 * ib_process_direct_cq - process a CQ in caller context
 * @cq:		CQ to process
 * @budget:	number of CQEs to poll for
 *
 * This function is used to process all outstanding CQ entries.
 * It does not offload CQ processing to a different context and does
 * not ask for completion interrupts from the HCA.
 * Using direct processing on CQ with non IB_POLL_DIRECT type may trigger
 * concurrent processing.
 *
 * Note: do not pass -1 as %budget unless it is guaranteed that the number
 * of completions that will be processed is small.
 */
int ib_process_cq_direct(struct ib_cq *cq, int budget)
{
	struct ib_wc wcs[IB_POLL_BATCH_DIRECT];

	return __ib_process_cq(cq, budget, wcs, IB_POLL_BATCH_DIRECT);
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

	completed = __ib_process_cq(cq, budget, cq->wc, IB_POLL_BATCH);
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

	completed = __ib_process_cq(cq, IB_POLL_BUDGET_WORKQUEUE, cq->wc,
				    IB_POLL_BATCH);
	if (completed >= IB_POLL_BUDGET_WORKQUEUE ||
	    ib_req_notify_cq(cq, IB_POLL_FLAGS) > 0)
		queue_work(cq->comp_wq, &cq->work);
}

static void ib_cq_completion_workqueue(struct ib_cq *cq, void *private)
{
	queue_work(cq->comp_wq, &cq->work);
}

/**
 * __ib_alloc_cq - allocate a completion queue
 * @dev:		device to allocate the CQ for
 * @private:		driver private data, accessible from cq->cq_context
 * @nr_cqe:		number of CQEs to allocate
 * @comp_vector:	HCA completion vectors for this CQ
 * @poll_ctx:		context to poll the CQ from.
 * @caller:		module owner name.
 * @udata:		Valid user data or NULL for kernel object
 *
 * This is the proper interface to allocate a CQ for in-kernel users. A
 * CQ allocated with this interface will automatically be polled from the
 * specified context. The ULP must use wr->wr_cqe instead of wr->wr_id
 * to use this CQ abstraction.
 */
struct ib_cq *__ib_alloc_cq_user(struct ib_device *dev, void *private,
				 int nr_cqe, int comp_vector,
				 enum ib_poll_context poll_ctx,
				 const char *caller, struct ib_udata *udata)
{
	struct ib_cq_init_attr cq_attr = {
		.cqe		= nr_cqe,
		.comp_vector	= comp_vector,
	};
	struct ib_cq *cq;
	int ret = -ENOMEM;

	cq = dev->ops.create_cq(dev, &cq_attr, NULL);
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

	cq->res.type = RDMA_RESTRACK_CQ;
	rdma_restrack_set_task(&cq->res, caller);
	rdma_restrack_kadd(&cq->res);

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
	case IB_POLL_UNBOUND_WORKQUEUE:
		cq->comp_handler = ib_cq_completion_workqueue;
		INIT_WORK(&cq->work, ib_cq_poll_work);
		ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
		cq->comp_wq = (cq->poll_ctx == IB_POLL_WORKQUEUE) ?
				ib_comp_wq : ib_comp_unbound_wq;
		break;
	default:
		ret = -EINVAL;
		goto out_free_wc;
	}

	return cq;

out_free_wc:
	kfree(cq->wc);
	rdma_restrack_del(&cq->res);
out_destroy_cq:
	cq->device->ops.destroy_cq(cq, udata);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(__ib_alloc_cq_user);

/**
 * ib_free_cq - free a completion queue
 * @cq:		completion queue to free.
 * @udata:	User data or NULL for kernel object
 */
void ib_free_cq_user(struct ib_cq *cq, struct ib_udata *udata)
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
	case IB_POLL_UNBOUND_WORKQUEUE:
		cancel_work_sync(&cq->work);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	kfree(cq->wc);
	rdma_restrack_del(&cq->res);
	ret = cq->device->ops.destroy_cq(cq, udata);
	WARN_ON_ONCE(ret);
}
EXPORT_SYMBOL(ib_free_cq_user);
