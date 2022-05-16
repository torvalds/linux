// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 HGST, a Western Digital Company.
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>

#include "core_priv.h"

#include <trace/events/rdma_core.h>
/* Max size for shared CQ, may require tuning */
#define IB_MAX_SHARED_CQ_SZ		4096U

/* # of WCs to poll for with a single call to ib_poll_cq */
#define IB_POLL_BATCH			16
#define IB_POLL_BATCH_DIRECT		8

/* # of WCs to iterate over before yielding */
#define IB_POLL_BUDGET_IRQ		256
#define IB_POLL_BUDGET_WORKQUEUE	65536

#define IB_POLL_FLAGS \
	(IB_CQ_NEXT_COMP | IB_CQ_REPORT_MISSED_EVENTS)

static const struct dim_cq_moder
rdma_dim_prof[RDMA_DIM_PARAMS_NUM_PROFILES] = {
	{1,   0, 1,  0},
	{1,   0, 4,  0},
	{2,   0, 4,  0},
	{2,   0, 8,  0},
	{4,   0, 8,  0},
	{16,  0, 8,  0},
	{16,  0, 16, 0},
	{32,  0, 16, 0},
	{32,  0, 32, 0},
};

static void ib_cq_rdma_dim_work(struct work_struct *w)
{
	struct dim *dim = container_of(w, struct dim, work);
	struct ib_cq *cq = dim->priv;

	u16 usec = rdma_dim_prof[dim->profile_ix].usec;
	u16 comps = rdma_dim_prof[dim->profile_ix].comps;

	dim->state = DIM_START_MEASURE;

	trace_cq_modify(cq, comps, usec);
	cq->device->ops.modify_cq(cq, comps, usec);
}

static void rdma_dim_init(struct ib_cq *cq)
{
	struct dim *dim;

	if (!cq->device->ops.modify_cq || !cq->device->use_cq_dim ||
	    cq->poll_ctx == IB_POLL_DIRECT)
		return;

	dim = kzalloc(sizeof(struct dim), GFP_KERNEL);
	if (!dim)
		return;

	dim->state = DIM_START_MEASURE;
	dim->tune_state = DIM_GOING_RIGHT;
	dim->profile_ix = RDMA_DIM_START_PROFILE;
	dim->priv = cq;
	cq->dim = dim;

	INIT_WORK(&dim->work, ib_cq_rdma_dim_work);
}

static void rdma_dim_destroy(struct ib_cq *cq)
{
	if (!cq->dim)
		return;

	cancel_work_sync(&cq->dim->work);
	kfree(cq->dim);
}

static int __poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc)
{
	int rc;

	rc = ib_poll_cq(cq, num_entries, wc);
	trace_cq_poll(cq, num_entries, rc);
	return rc;
}

static int __ib_process_cq(struct ib_cq *cq, int budget, struct ib_wc *wcs,
			   int batch)
{
	int i, n, completed = 0;

	trace_cq_process(cq);

	/*
	 * budget might be (-1) if the caller does not
	 * want to bound this call, thus we need unsigned
	 * minimum here.
	 */
	while ((n = __poll_cq(cq, min_t(u32, batch,
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
	struct dim *dim = cq->dim;
	int completed;

	completed = __ib_process_cq(cq, budget, cq->wc, IB_POLL_BATCH);
	if (completed < budget) {
		irq_poll_complete(&cq->iop);
		if (ib_req_notify_cq(cq, IB_POLL_FLAGS) > 0) {
			trace_cq_reschedule(cq);
			irq_poll_sched(&cq->iop);
		}
	}

	if (dim)
		rdma_dim(dim, completed);

	return completed;
}

static void ib_cq_completion_softirq(struct ib_cq *cq, void *private)
{
	trace_cq_schedule(cq);
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
	else if (cq->dim)
		rdma_dim(cq->dim, completed);
}

static void ib_cq_completion_workqueue(struct ib_cq *cq, void *private)
{
	trace_cq_schedule(cq);
	queue_work(cq->comp_wq, &cq->work);
}

/**
 * __ib_alloc_cq        allocate a completion queue
 * @dev:		device to allocate the CQ for
 * @private:		driver private data, accessible from cq->cq_context
 * @nr_cqe:		number of CQEs to allocate
 * @comp_vector:	HCA completion vectors for this CQ
 * @poll_ctx:		context to poll the CQ from.
 * @caller:		module owner name.
 *
 * This is the proper interface to allocate a CQ for in-kernel users. A
 * CQ allocated with this interface will automatically be polled from the
 * specified context. The ULP must use wr->wr_cqe instead of wr->wr_id
 * to use this CQ abstraction.
 */
struct ib_cq *__ib_alloc_cq(struct ib_device *dev, void *private, int nr_cqe,
			    int comp_vector, enum ib_poll_context poll_ctx,
			    const char *caller)
{
	struct ib_cq_init_attr cq_attr = {
		.cqe		= nr_cqe,
		.comp_vector	= comp_vector,
	};
	struct ib_cq *cq;
	int ret = -ENOMEM;

	cq = rdma_zalloc_drv_obj(dev, ib_cq);
	if (!cq)
		return ERR_PTR(ret);

	cq->device = dev;
	cq->cq_context = private;
	cq->poll_ctx = poll_ctx;
	atomic_set(&cq->usecnt, 0);
	cq->comp_vector = comp_vector;

	cq->wc = kmalloc_array(IB_POLL_BATCH, sizeof(*cq->wc), GFP_KERNEL);
	if (!cq->wc)
		goto out_free_cq;

	rdma_restrack_new(&cq->res, RDMA_RESTRACK_CQ);
	rdma_restrack_set_name(&cq->res, caller);

	ret = dev->ops.create_cq(cq, &cq_attr, NULL);
	if (ret)
		goto out_free_wc;

	rdma_dim_init(cq);

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
		goto out_destroy_cq;
	}

	rdma_restrack_add(&cq->res);
	trace_cq_alloc(cq, nr_cqe, comp_vector, poll_ctx);
	return cq;

out_destroy_cq:
	rdma_dim_destroy(cq);
	cq->device->ops.destroy_cq(cq, NULL);
out_free_wc:
	rdma_restrack_put(&cq->res);
	kfree(cq->wc);
out_free_cq:
	kfree(cq);
	trace_cq_alloc_error(nr_cqe, comp_vector, poll_ctx, ret);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(__ib_alloc_cq);

/**
 * __ib_alloc_cq_any - allocate a completion queue
 * @dev:		device to allocate the CQ for
 * @private:		driver private data, accessible from cq->cq_context
 * @nr_cqe:		number of CQEs to allocate
 * @poll_ctx:		context to poll the CQ from
 * @caller:		module owner name
 *
 * Attempt to spread ULP Completion Queues over each device's interrupt
 * vectors. A simple best-effort mechanism is used.
 */
struct ib_cq *__ib_alloc_cq_any(struct ib_device *dev, void *private,
				int nr_cqe, enum ib_poll_context poll_ctx,
				const char *caller)
{
	static atomic_t counter;
	int comp_vector = 0;

	if (dev->num_comp_vectors > 1)
		comp_vector =
			atomic_inc_return(&counter) %
			min_t(int, dev->num_comp_vectors, num_online_cpus());

	return __ib_alloc_cq(dev, private, nr_cqe, comp_vector, poll_ctx,
			     caller);
}
EXPORT_SYMBOL(__ib_alloc_cq_any);

/**
 * ib_free_cq - free a completion queue
 * @cq:		completion queue to free.
 */
void ib_free_cq(struct ib_cq *cq)
{
	int ret;

	if (WARN_ON_ONCE(atomic_read(&cq->usecnt)))
		return;
	if (WARN_ON_ONCE(cq->cqe_used))
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

	rdma_dim_destroy(cq);
	trace_cq_free(cq);
	ret = cq->device->ops.destroy_cq(cq, NULL);
	WARN_ONCE(ret, "Destroy of kernel CQ shouldn't fail");
	rdma_restrack_del(&cq->res);
	kfree(cq->wc);
	kfree(cq);
}
EXPORT_SYMBOL(ib_free_cq);

void ib_cq_pool_init(struct ib_device *dev)
{
	unsigned int i;

	spin_lock_init(&dev->cq_pools_lock);
	for (i = 0; i < ARRAY_SIZE(dev->cq_pools); i++)
		INIT_LIST_HEAD(&dev->cq_pools[i]);
}

void ib_cq_pool_destroy(struct ib_device *dev)
{
	struct ib_cq *cq, *n;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dev->cq_pools); i++) {
		list_for_each_entry_safe(cq, n, &dev->cq_pools[i],
					 pool_entry) {
			WARN_ON(cq->cqe_used);
			cq->shared = false;
			ib_free_cq(cq);
		}
	}
}

static int ib_alloc_cqs(struct ib_device *dev, unsigned int nr_cqes,
			enum ib_poll_context poll_ctx)
{
	LIST_HEAD(tmp_list);
	unsigned int nr_cqs, i;
	struct ib_cq *cq, *n;
	int ret;

	if (poll_ctx > IB_POLL_LAST_POOL_TYPE) {
		WARN_ON_ONCE(poll_ctx > IB_POLL_LAST_POOL_TYPE);
		return -EINVAL;
	}

	/*
	 * Allocate at least as many CQEs as requested, and otherwise
	 * a reasonable batch size so that we can share CQs between
	 * multiple users instead of allocating a larger number of CQs.
	 */
	nr_cqes = min_t(unsigned int, dev->attrs.max_cqe,
			max(nr_cqes, IB_MAX_SHARED_CQ_SZ));
	nr_cqs = min_t(unsigned int, dev->num_comp_vectors, num_online_cpus());
	for (i = 0; i < nr_cqs; i++) {
		cq = ib_alloc_cq(dev, NULL, nr_cqes, i, poll_ctx);
		if (IS_ERR(cq)) {
			ret = PTR_ERR(cq);
			goto out_free_cqs;
		}
		cq->shared = true;
		list_add_tail(&cq->pool_entry, &tmp_list);
	}

	spin_lock_irq(&dev->cq_pools_lock);
	list_splice(&tmp_list, &dev->cq_pools[poll_ctx]);
	spin_unlock_irq(&dev->cq_pools_lock);

	return 0;

out_free_cqs:
	list_for_each_entry_safe(cq, n, &tmp_list, pool_entry) {
		cq->shared = false;
		ib_free_cq(cq);
	}
	return ret;
}

/**
 * ib_cq_pool_get() - Find the least used completion queue that matches
 *   a given cpu hint (or least used for wild card affinity) and fits
 *   nr_cqe.
 * @dev: rdma device
 * @nr_cqe: number of needed cqe entries
 * @comp_vector_hint: completion vector hint (-1) for the driver to assign
 *   a comp vector based on internal counter
 * @poll_ctx: cq polling context
 *
 * Finds a cq that satisfies @comp_vector_hint and @nr_cqe requirements and
 * claim entries in it for us.  In case there is no available cq, allocate
 * a new cq with the requirements and add it to the device pool.
 * IB_POLL_DIRECT cannot be used for shared cqs so it is not a valid value
 * for @poll_ctx.
 */
struct ib_cq *ib_cq_pool_get(struct ib_device *dev, unsigned int nr_cqe,
			     int comp_vector_hint,
			     enum ib_poll_context poll_ctx)
{
	static unsigned int default_comp_vector;
	unsigned int vector, num_comp_vectors;
	struct ib_cq *cq, *found = NULL;
	int ret;

	if (poll_ctx > IB_POLL_LAST_POOL_TYPE) {
		WARN_ON_ONCE(poll_ctx > IB_POLL_LAST_POOL_TYPE);
		return ERR_PTR(-EINVAL);
	}

	num_comp_vectors =
		min_t(unsigned int, dev->num_comp_vectors, num_online_cpus());
	/* Project the affinty to the device completion vector range */
	if (comp_vector_hint < 0) {
		comp_vector_hint =
			(READ_ONCE(default_comp_vector) + 1) % num_comp_vectors;
		WRITE_ONCE(default_comp_vector, comp_vector_hint);
	}
	vector = comp_vector_hint % num_comp_vectors;

	/*
	 * Find the least used CQ with correct affinity and
	 * enough free CQ entries
	 */
	while (!found) {
		spin_lock_irq(&dev->cq_pools_lock);
		list_for_each_entry(cq, &dev->cq_pools[poll_ctx],
				    pool_entry) {
			/*
			 * Check to see if we have found a CQ with the
			 * correct completion vector
			 */
			if (vector != cq->comp_vector)
				continue;
			if (cq->cqe_used + nr_cqe > cq->cqe)
				continue;
			found = cq;
			break;
		}

		if (found) {
			found->cqe_used += nr_cqe;
			spin_unlock_irq(&dev->cq_pools_lock);

			return found;
		}
		spin_unlock_irq(&dev->cq_pools_lock);

		/*
		 * Didn't find a match or ran out of CQs in the device
		 * pool, allocate a new array of CQs.
		 */
		ret = ib_alloc_cqs(dev, nr_cqe, poll_ctx);
		if (ret)
			return ERR_PTR(ret);
	}

	return found;
}
EXPORT_SYMBOL(ib_cq_pool_get);

/**
 * ib_cq_pool_put - Return a CQ taken from a shared pool.
 * @cq: The CQ to return.
 * @nr_cqe: The max number of cqes that the user had requested.
 */
void ib_cq_pool_put(struct ib_cq *cq, unsigned int nr_cqe)
{
	if (WARN_ON_ONCE(nr_cqe > cq->cqe_used))
		return;

	spin_lock_irq(&cq->device->cq_pools_lock);
	cq->cqe_used -= nr_cqe;
	spin_unlock_irq(&cq->device->cq_pools_lock);
}
EXPORT_SYMBOL(ib_cq_pool_put);
