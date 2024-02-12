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

#include "iommu-sva.h"

/**
 * struct iopf_queue - IO Page Fault queue
 * @wq: the fault workqueue
 * @devices: devices attached to this queue
 * @lock: protects the device list
 */
struct iopf_queue {
	struct workqueue_struct		*wq;
	struct list_head		devices;
	struct mutex			lock;
};

struct iopf_group {
	struct iopf_fault		last_fault;
	struct list_head		faults;
	struct work_struct		work;
	struct device			*dev;
};

static int iopf_complete_group(struct device *dev, struct iopf_fault *iopf,
			       enum iommu_page_response_code status)
{
	struct iommu_page_response resp = {
		.pasid			= iopf->fault.prm.pasid,
		.grpid			= iopf->fault.prm.grpid,
		.code			= status,
	};

	if ((iopf->fault.prm.flags & IOMMU_FAULT_PAGE_REQUEST_PASID_VALID) &&
	    (iopf->fault.prm.flags & IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID))
		resp.flags = IOMMU_PAGE_RESP_PASID_VALID;

	return iommu_page_response(dev, &resp);
}

static void iopf_handler(struct work_struct *work)
{
	struct iopf_group *group;
	struct iommu_domain *domain;
	struct iopf_fault *iopf, *next;
	enum iommu_page_response_code status = IOMMU_PAGE_RESP_SUCCESS;

	group = container_of(work, struct iopf_group, work);
	domain = iommu_get_domain_for_dev_pasid(group->dev,
				group->last_fault.fault.prm.pasid, 0);
	if (!domain || !domain->iopf_handler)
		status = IOMMU_PAGE_RESP_INVALID;

	list_for_each_entry_safe(iopf, next, &group->faults, list) {
		/*
		 * For the moment, errors are sticky: don't handle subsequent
		 * faults in the group if there is an error.
		 */
		if (status == IOMMU_PAGE_RESP_SUCCESS)
			status = domain->iopf_handler(&iopf->fault,
						      domain->fault_data);

		if (!(iopf->fault.prm.flags &
		      IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE))
			kfree(iopf);
	}

	iopf_complete_group(group->dev, &group->last_fault, status);
	kfree(group);
}

/**
 * iommu_queue_iopf - IO Page Fault handler
 * @fault: fault event
 * @dev: struct device.
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
int iommu_queue_iopf(struct iommu_fault *fault, struct device *dev)
{
	int ret;
	struct iopf_group *group;
	struct iopf_fault *iopf, *next;
	struct iommu_fault_param *iopf_param;
	struct dev_iommu *param = dev->iommu;

	lockdep_assert_held(&param->lock);

	if (fault->type != IOMMU_FAULT_PAGE_REQ)
		/* Not a recoverable page fault */
		return -EOPNOTSUPP;

	/*
	 * As long as we're holding param->lock, the queue can't be unlinked
	 * from the device and therefore cannot disappear.
	 */
	iopf_param = param->fault_param;
	if (!iopf_param)
		return -ENODEV;

	if (!(fault->prm.flags & IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE)) {
		iopf = kzalloc(sizeof(*iopf), GFP_KERNEL);
		if (!iopf)
			return -ENOMEM;

		iopf->fault = *fault;

		/* Non-last request of a group. Postpone until the last one */
		list_add(&iopf->list, &iopf_param->partial);

		return 0;
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

	group->dev = dev;
	group->last_fault.fault = *fault;
	INIT_LIST_HEAD(&group->faults);
	list_add(&group->last_fault.list, &group->faults);
	INIT_WORK(&group->work, iopf_handler);

	/* See if we have partial faults for this group */
	list_for_each_entry_safe(iopf, next, &iopf_param->partial, list) {
		if (iopf->fault.prm.grpid == fault->prm.grpid)
			/* Insert *before* the last fault */
			list_move(&iopf->list, &group->faults);
	}

	queue_work(iopf_param->queue->wq, &group->work);
	return 0;

cleanup_partial:
	list_for_each_entry_safe(iopf, next, &iopf_param->partial, list) {
		if (iopf->fault.prm.grpid == fault->prm.grpid) {
			list_del(&iopf->list);
			kfree(iopf);
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(iommu_queue_iopf);

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
	int ret = 0;
	struct iommu_fault_param *iopf_param;
	struct dev_iommu *param = dev->iommu;

	if (!param)
		return -ENODEV;

	mutex_lock(&param->lock);
	iopf_param = param->fault_param;
	if (iopf_param)
		flush_workqueue(iopf_param->queue->wq);
	else
		ret = -ENODEV;
	mutex_unlock(&param->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(iopf_queue_flush_dev);

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
		list_for_each_entry_safe(iopf, next, &iopf_param->partial,
					 list) {
			list_del(&iopf->list);
			kfree(iopf);
		}
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

	mutex_lock(&queue->lock);
	mutex_lock(&param->lock);
	if (param->fault_param) {
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
	list_add(&fault_param->queue_list, &queue->devices);
	fault_param->queue = queue;

	param->fault_param = fault_param;

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
	struct iommu_fault_param *fault_param = param->fault_param;

	mutex_lock(&queue->lock);
	mutex_lock(&param->lock);
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

	param->fault_param = NULL;
	kfree(fault_param);
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
