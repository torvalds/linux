// SPDX-License-Identifier: GPL-2.0
/*
 * Handle device page faults
 *
 * Copyright (C) 2020 ARM Ltd.
 */

#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "iommu-priv.h"

/*
 * Return the fault parameter of a device if it exists. Otherwise, return NULL.
 * On a successful return, the caller takes a reference of this parameter and
 * should put it after use by calling iopf_put_dev_fault_param().
 */
static struct iommu_fault_param *iopf_get_dev_fault_param(struct device *dev)
{
	struct dev_iommu *param = dev->iommu;
	struct iommu_fault_param *fault_param;

	rcu_read_lock();
	fault_param = rcu_dereference(param->fault_param);
	if (fault_param && !refcount_inc_not_zero(&fault_param->users))
		fault_param = NULL;
	rcu_read_unlock();

	return fault_param;
}

/* Caller must hold a reference of the fault parameter. */
static void iopf_put_dev_fault_param(struct iommu_fault_param *fault_param)
{
	if (refcount_dec_and_test(&fault_param->users))
		kfree_rcu(fault_param, rcu);
}

void iopf_free_group(struct iopf_group *group)
{
	struct iopf_fault *iopf, *next;

	list_for_each_entry_safe(iopf, next, &group->faults, list) {
		if (!(iopf->fault.prm.flags & IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE))
			kfree(iopf);
	}

	/* Pair with iommu_report_device_fault(). */
	iopf_put_dev_fault_param(group->fault_param);
	kfree(group);
}
EXPORT_SYMBOL_GPL(iopf_free_group);

static struct iommu_domain *get_domain_for_iopf(struct device *dev,
						struct iommu_fault *fault)
{
	struct iommu_domain *domain;

	if (fault->prm.flags & IOMMU_FAULT_PAGE_REQUEST_PASID_VALID) {
		domain = iommu_get_domain_for_dev_pasid(dev, fault->prm.pasid, 0);
		if (IS_ERR(domain))
			domain = NULL;
	} else {
		domain = iommu_get_domain_for_dev(dev);
	}

	if (!domain || !domain->iopf_handler) {
		dev_warn_ratelimited(dev,
			"iopf (pasid %d) without domain attached or handler installed\n",
			 fault->prm.pasid);

		return NULL;
	}

	return domain;
}

/**
 * iommu_handle_iopf - IO Page Fault handler
 * @fault: fault event
 * @iopf_param: the fault parameter of the device.
 *
 * Add a fault to the device workqueue, to be handled by mm.
 *
 * This module doesn't handle PCI PASID Stop Marker; IOMMU drivers must discard
 * them before reporting faults. A PASID Stop Marker (LRW = 0b100) doesn't
 * expect a response. It may be generated when disabling a PASID (issuing a
 * PASID stop request) by some PCI devices.
 *
 * The PASID stop request is issued by the device driver before unbind(). Once
 * it completes, no page request is generated for this PASID anymore and
 * outstanding ones have been pushed to the IOMMU (as per PCIe 4.0r1.0 - 6.20.1
 * and 10.4.1.2 - Managing PASID TLP Prefix Usage). Some PCI devices will wait
 * for all outstanding page requests to come back with a response before
 * completing the PASID stop request. Others do not wait for page responses, and
 * instead issue this Stop Marker that tells us when the PASID can be
 * reallocated.
 *
 * It is safe to discard the Stop Marker because it is an optimization.
 * a. Page requests, which are posted requests, have been flushed to the IOMMU
 *    when the stop request completes.
 * b. The IOMMU driver flushes all fault queues on unbind() before freeing the
 *    PASID.
 *
 * So even though the Stop Marker might be issued by the device *after* the stop
 * request completes, outstanding faults will have been dealt with by the time
 * the PASID is freed.
 *
 * Any valid page fault will be eventually routed to an iommu domain and the
 * page fault handler installed there will get called. The users of this
 * handling framework should guarantee that the iommu domain could only be
 * freed after the device has stopped generating page faults (or the iommu
 * hardware has been set to block the page faults) and the pending page faults
 * have been flushed.
 *
 * Return: 0 on success and <0 on error.
 */
static int iommu_handle_iopf(struct iommu_fault *fault,
			     struct iommu_fault_param *iopf_param)
{
	int ret;
	struct iopf_group *group;
	struct iommu_domain *domain;
	struct iopf_fault *iopf, *next;
	struct device *dev = iopf_param->dev;

	lockdep_assert_held(&iopf_param->lock);

	if (fault->type != IOMMU_FAULT_PAGE_REQ)
		/* Not a recoverable page fault */
		return -EOPNOTSUPP;

	if (!(fault->prm.flags & IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE)) {
		iopf = kzalloc(sizeof(*iopf), GFP_KERNEL);
		if (!iopf)
			return -ENOMEM;

		iopf->fault = *fault;

		/* Non-last request of a group. Postpone until the last one */
		list_add(&iopf->list, &iopf_param->partial);

		return 0;
	}

	domain = get_domain_for_iopf(dev, fault);
	if (!domain) {
		ret = -EINVAL;
		goto cleanup_partial;
	}

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group) {
		/*
		 * The caller will send a response to the hardware. But we do
		 * need to clean up before leaving, otherwise partial faults
		 * will be stuck.
		 */
		ret = -ENOMEM;
		goto cleanup_partial;
	}

	group->fault_param = iopf_param;
	group->last_fault.fault = *fault;
	INIT_LIST_HEAD(&group->faults);
	group->domain = domain;
	list_add(&group->last_fault.list, &group->faults);

	/* See if we have partial faults for this group */
	list_for_each_entry_safe(iopf, next, &iopf_param->partial, list) {
		if (iopf->fault.prm.grpid == fault->prm.grpid)
			/* Insert *before* the last fault */
			list_move(&iopf->list, &group->faults);
	}

	mutex_unlock(&iopf_param->lock);
	ret = domain->iopf_handler(group);
	mutex_lock(&iopf_param->lock);
	if (ret)
		iopf_free_group(group);

	return ret;
cleanup_partial:
	list_for_each_entry_safe(iopf, next, &iopf_param->partial, list) {
		if (iopf->fault.prm.grpid == fault->prm.grpid) {
			list_del(&iopf->list);
			kfree(iopf);
		}
	}
	return ret;
}

/**
 * iommu_report_device_fault() - Report fault event to device driver
 * @dev: the device
 * @evt: fault event data
 *
 * Called by IOMMU drivers when a fault is detected, typically in a threaded IRQ
 * handler. When this function fails and the fault is recoverable, it is the
 * caller's responsibility to complete the fault.
 *
 * Return 0 on success, or an error.
 */
int iommu_report_device_fault(struct device *dev, struct iopf_fault *evt)
{
	bool last_prq = evt->fault.type == IOMMU_FAULT_PAGE_REQ &&
		(evt->fault.prm.flags & IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE);
	struct iommu_fault_param *fault_param;
	struct iopf_fault *evt_pending;
	int ret;

	fault_param = iopf_get_dev_fault_param(dev);
	if (!fault_param)
		return -EINVAL;

	mutex_lock(&fault_param->lock);
	if (last_prq) {
		evt_pending = kmemdup(evt, sizeof(struct iopf_fault),
				      GFP_KERNEL);
		if (!evt_pending) {
			ret = -ENOMEM;
			goto err_unlock;
		}
		list_add_tail(&evt_pending->list, &fault_param->faults);
	}

	ret = iommu_handle_iopf(&evt->fault, fault_param);
	if (ret)
		goto err_free;

	mutex_unlock(&fault_param->lock);
	/* The reference count of fault_param is now held by iopf_group. */
	if (!last_prq)
		iopf_put_dev_fault_param(fault_param);

	return 0;
err_free:
	if (last_prq) {
		list_del(&evt_pending->list);
		kfree(evt_pending);
	}
err_unlock:
	mutex_unlock(&fault_param->lock);
	iopf_put_dev_fault_param(fault_param);

	return ret;
}
EXPORT_SYMBOL_GPL(iommu_report_device_fault);

static int iommu_page_response(struct iopf_group *group,
			       struct iommu_page_response *msg)
{
	bool needs_pasid;
	int ret = -EINVAL;
	struct iopf_fault *evt;
	struct iommu_fault_page_request *prm;
	struct device *dev = group->fault_param->dev;
	const struct iommu_ops *ops = dev_iommu_ops(dev);
	bool has_pasid = msg->flags & IOMMU_PAGE_RESP_PASID_VALID;
	struct iommu_fault_param *fault_param = group->fault_param;

	/* Only send response if there is a fault report pending */
	mutex_lock(&fault_param->lock);
	if (list_empty(&fault_param->faults)) {
		dev_warn_ratelimited(dev, "no pending PRQ, drop response\n");
		goto done_unlock;
	}
	/*
	 * Check if we have a matching page request pending to respond,
	 * otherwise return -EINVAL
	 */
	list_for_each_entry(evt, &fault_param->faults, list) {
		prm = &evt->fault.prm;
		if (prm->grpid != msg->grpid)
			continue;

		/*
		 * If the PASID is required, the corresponding request is
		 * matched using the group ID, the PASID valid bit and the PASID
		 * value. Otherwise only the group ID matches request and
		 * response.
		 */
		needs_pasid = prm->flags & IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID;
		if (needs_pasid && (!has_pasid || msg->pasid != prm->pasid))
			continue;

		if (!needs_pasid && has_pasid) {
			/* No big deal, just clear it. */
			msg->flags &= ~IOMMU_PAGE_RESP_PASID_VALID;
			msg->pasid = 0;
		}

		ret = ops->page_response(dev, evt, msg);
		list_del(&evt->list);
		kfree(evt);
		break;
	}

done_unlock:
	mutex_unlock(&fault_param->lock);

	return ret;
}

/**
 * iopf_queue_flush_dev - Ensure that all queued faults have been processed
 * @dev: the endpoint whose faults need to be flushed.
 *
 * The IOMMU driver calls this before releasing a PASID, to ensure that all
 * pending faults for this PASID have been handled, and won't hit the address
 * space of the next process that uses this PASID. The driver must make sure
 * that no new fault is added to the queue. In particular it must flush its
 * low-level queue before calling this function.
 *
 * Return: 0 on success and <0 on error.
 */
int iopf_queue_flush_dev(struct device *dev)
{
	struct iommu_fault_param *iopf_param;

	/*
	 * It's a driver bug to be here after iopf_queue_remove_device().
	 * Therefore, it's safe to dereference the fault parameter without
	 * holding the lock.
	 */
	iopf_param = rcu_dereference_check(dev->iommu->fault_param, true);
	if (WARN_ON(!iopf_param))
		return -ENODEV;

	flush_workqueue(iopf_param->queue->wq);

	return 0;
}
EXPORT_SYMBOL_GPL(iopf_queue_flush_dev);

/**
 * iopf_group_response - Respond a group of page faults
 * @group: the group of faults with the same group id
 * @status: the response code
 *
 * Return 0 on success and <0 on error.
 */
int iopf_group_response(struct iopf_group *group,
			enum iommu_page_response_code status)
{
	struct iopf_fault *iopf = &group->last_fault;
	struct iommu_page_response resp = {
		.pasid = iopf->fault.prm.pasid,
		.grpid = iopf->fault.prm.grpid,
		.code = status,
	};

	if ((iopf->fault.prm.flags & IOMMU_FAULT_PAGE_REQUEST_PASID_VALID) &&
	    (iopf->fault.prm.flags & IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID))
		resp.flags = IOMMU_PAGE_RESP_PASID_VALID;

	return iommu_page_response(group, &resp);
}
EXPORT_SYMBOL_GPL(iopf_group_response);

/**
 * iopf_queue_discard_partial - Remove all pending partial fault
 * @queue: the queue whose partial faults need to be discarded
 *
 * When the hardware queue overflows, last page faults in a group may have been
 * lost and the IOMMU driver calls this to discard all partial faults. The
 * driver shouldn't be adding new faults to this queue concurrently.
 *
 * Return: 0 on success and <0 on error.
 */
int iopf_queue_discard_partial(struct iopf_queue *queue)
{
	struct iopf_fault *iopf, *next;
	struct iommu_fault_param *iopf_param;

	if (!queue)
		return -EINVAL;

	mutex_lock(&queue->lock);
	list_for_each_entry(iopf_param, &queue->devices, queue_list) {
		mutex_lock(&iopf_param->lock);
		list_for_each_entry_safe(iopf, next, &iopf_param->partial,
					 list) {
			list_del(&iopf->list);
			kfree(iopf);
		}
		mutex_unlock(&iopf_param->lock);
	}
	mutex_unlock(&queue->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(iopf_queue_discard_partial);

/**
 * iopf_queue_add_device - Add producer to the fault queue
 * @queue: IOPF queue
 * @dev: device to add
 *
 * Return: 0 on success and <0 on error.
 */
int iopf_queue_add_device(struct iopf_queue *queue, struct device *dev)
{
	int ret = 0;
	struct dev_iommu *param = dev->iommu;
	struct iommu_fault_param *fault_param;
	const struct iommu_ops *ops = dev_iommu_ops(dev);

	if (!ops->page_response)
		return -ENODEV;

	mutex_lock(&queue->lock);
	mutex_lock(&param->lock);
	if (rcu_dereference_check(param->fault_param,
				  lockdep_is_held(&param->lock))) {
		ret = -EBUSY;
		goto done_unlock;
	}

	fault_param = kzalloc(sizeof(*fault_param), GFP_KERNEL);
	if (!fault_param) {
		ret = -ENOMEM;
		goto done_unlock;
	}

	mutex_init(&fault_param->lock);
	INIT_LIST_HEAD(&fault_param->faults);
	INIT_LIST_HEAD(&fault_param->partial);
	fault_param->dev = dev;
	refcount_set(&fault_param->users, 1);
	list_add(&fault_param->queue_list, &queue->devices);
	fault_param->queue = queue;

	rcu_assign_pointer(param->fault_param, fault_param);

done_unlock:
	mutex_unlock(&param->lock);
	mutex_unlock(&queue->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iopf_queue_add_device);

/**
 * iopf_queue_remove_device - Remove producer from fault queue
 * @queue: IOPF queue
 * @dev: device to remove
 *
 * Caller makes sure that no more faults are reported for this device.
 *
 * Return: 0 on success and <0 on error.
 */
int iopf_queue_remove_device(struct iopf_queue *queue, struct device *dev)
{
	int ret = 0;
	struct iopf_fault *iopf, *next;
	struct dev_iommu *param = dev->iommu;
	struct iommu_fault_param *fault_param;

	mutex_lock(&queue->lock);
	mutex_lock(&param->lock);
	fault_param = rcu_dereference_check(param->fault_param,
					    lockdep_is_held(&param->lock));
	if (!fault_param) {
		ret = -ENODEV;
		goto unlock;
	}

	if (fault_param->queue != queue) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!list_empty(&fault_param->faults)) {
		ret = -EBUSY;
		goto unlock;
	}

	list_del(&fault_param->queue_list);

	/* Just in case some faults are still stuck */
	list_for_each_entry_safe(iopf, next, &fault_param->partial, list)
		kfree(iopf);

	/* dec the ref owned by iopf_queue_add_device() */
	rcu_assign_pointer(param->fault_param, NULL);
	iopf_put_dev_fault_param(fault_param);
unlock:
	mutex_unlock(&param->lock);
	mutex_unlock(&queue->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iopf_queue_remove_device);

/**
 * iopf_queue_alloc - Allocate and initialize a fault queue
 * @name: a unique string identifying the queue (for workqueue)
 *
 * Return: the queue on success and NULL on error.
 */
struct iopf_queue *iopf_queue_alloc(const char *name)
{
	struct iopf_queue *queue;

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return NULL;

	/*
	 * The WQ is unordered because the low-level handler enqueues faults by
	 * group. PRI requests within a group have to be ordered, but once
	 * that's dealt with, the high-level function can handle groups out of
	 * order.
	 */
	queue->wq = alloc_workqueue("iopf_queue/%s", WQ_UNBOUND, 0, name);
	if (!queue->wq) {
		kfree(queue);
		return NULL;
	}

	INIT_LIST_HEAD(&queue->devices);
	mutex_init(&queue->lock);

	return queue;
}
EXPORT_SYMBOL_GPL(iopf_queue_alloc);

/**
 * iopf_queue_free - Free IOPF queue
 * @queue: queue to free
 *
 * Counterpart to iopf_queue_alloc(). The driver must not be queuing faults or
 * adding/removing devices on this queue anymore.
 */
void iopf_queue_free(struct iopf_queue *queue)
{
	struct iommu_fault_param *iopf_param, *next;

	if (!queue)
		return;

	list_for_each_entry_safe(iopf_param, next, &queue->devices, queue_list)
		iopf_queue_remove_device(queue, iopf_param->dev);

	destroy_workqueue(queue->wq);
	kfree(queue);
}
EXPORT_SYMBOL_GPL(iopf_queue_free);
