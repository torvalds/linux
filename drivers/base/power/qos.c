/*
 * Devices PM QoS constraints management
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * This module exposes the interface to kernel space for specifying
 * per-device PM QoS dependencies. It provides infrastructure for registration
 * of:
 *
 * Dependents on a QoS value : register requests
 * Watchers of QoS value : get notified when target QoS value changes
 *
 * This QoS design is best effort based. Dependents register their QoS needs.
 * Watchers register to keep track of the current QoS needs of the system.
 * Watchers can register different types of notification callbacks:
 *  . a per-device notification callback using the dev_pm_qos_*_notifier API.
 *    The notification chain data is stored in the per-device constraint
 *    data struct.
 *  . a system-wide notification callback using the dev_pm_qos_*_global_notifier
 *    API. The notification chain data is stored in a static variable.
 *
 * Note about the per-device constraint data struct allocation:
 * . The per-device constraints data struct ptr is tored into the device
 *    dev_pm_info.
 * . To minimize the data usage by the per-device constraints, the data struct
 *   is only allocated at the first call to dev_pm_qos_add_request.
 * . The data is later free'd when the device is removed from the system.
 * . The constraints_state variable from dev_pm_info tracks the data struct
 *    allocation state:
 *    DEV_PM_QOS_NO_DEVICE: No device present or device removed, no data
 *     allocated,
 *    DEV_PM_QOS_DEVICE_PRESENT: Device present, data not allocated and will be
 *     allocated at the first call to dev_pm_qos_add_request,
 *    DEV_PM_QOS_ALLOCATED: Device present, data allocated. The per-device
 *     PM QoS constraints framework is operational and constraints can be
 *     added, updated or removed using the dev_pm_qos_* API.
 *  . A global mutex protects the constraints users from the data being
 *     allocated and free'd.
 */

#include <linux/pm_qos.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mutex.h>


static DEFINE_MUTEX(dev_pm_qos_mtx);
static BLOCKING_NOTIFIER_HEAD(dev_pm_notifiers);

/*
 * apply_constraint
 * @req: constraint request to apply
 * @action: action to perform add/update/remove, of type enum pm_qos_req_action
 * @value: defines the qos request
 *
 * Internal function to update the constraints list using the PM QoS core
 * code and if needed call the per-device and the global notification
 * callbacks
 */
static int apply_constraint(struct dev_pm_qos_request *req,
			    enum pm_qos_req_action action, int value)
{
	int ret, curr_value;

	ret = pm_qos_update_target(req->dev->power.constraints,
				   &req->node, action, value);

	if (ret) {
		/* Call the global callbacks if needed */
		curr_value = pm_qos_read_value(req->dev->power.constraints);
		blocking_notifier_call_chain(&dev_pm_notifiers,
					     (unsigned long)curr_value,
					     req);
	}

	return ret;
}

/*
 * dev_pm_qos_constraints_allocate
 * @dev: device to allocate data for
 *
 * Called at the first call to add_request, for constraint data allocation
 * Must be called with the dev_pm_qos_mtx mutex held
 */
static int dev_pm_qos_constraints_allocate(struct device *dev)
{
	struct pm_qos_constraints *c;
	struct blocking_notifier_head *n;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n) {
		kfree(c);
		return -ENOMEM;
	}
	BLOCKING_INIT_NOTIFIER_HEAD(n);

	dev->power.constraints = c;
	plist_head_init(&dev->power.constraints->list);
	dev->power.constraints->target_value = PM_QOS_DEV_LAT_DEFAULT_VALUE;
	dev->power.constraints->default_value =	PM_QOS_DEV_LAT_DEFAULT_VALUE;
	dev->power.constraints->type = PM_QOS_MIN;
	dev->power.constraints->notifiers = n;
	dev->power.constraints_state = DEV_PM_QOS_ALLOCATED;

	return 0;
}

/**
 * dev_pm_qos_constraints_init
 * @dev: target device
 *
 * Called from the device PM subsystem at device insertion
 */
void dev_pm_qos_constraints_init(struct device *dev)
{
	mutex_lock(&dev_pm_qos_mtx);
	dev->power.constraints_state = DEV_PM_QOS_DEVICE_PRESENT;
	mutex_unlock(&dev_pm_qos_mtx);
}

/**
 * dev_pm_qos_constraints_destroy
 * @dev: target device
 *
 * Called from the device PM subsystem at device removal
 */
void dev_pm_qos_constraints_destroy(struct device *dev)
{
	struct dev_pm_qos_request *req, *tmp;

	mutex_lock(&dev_pm_qos_mtx);

	if (dev->power.constraints_state == DEV_PM_QOS_ALLOCATED) {
		/* Flush the constraints list for the device */
		plist_for_each_entry_safe(req, tmp,
					  &dev->power.constraints->list,
					  node) {
			/*
			 * Update constraints list and call the notification
			 * callbacks if needed
			 */
			apply_constraint(req, PM_QOS_REMOVE_REQ,
					 PM_QOS_DEFAULT_VALUE);
			memset(req, 0, sizeof(*req));
		}

		kfree(dev->power.constraints->notifiers);
		kfree(dev->power.constraints);
		dev->power.constraints = NULL;
	}
	dev->power.constraints_state = DEV_PM_QOS_NO_DEVICE;

	mutex_unlock(&dev_pm_qos_mtx);
}

/**
 * dev_pm_qos_add_request - inserts new qos request into the list
 * @dev: target device for the constraint
 * @req: pointer to a preallocated handle
 * @value: defines the qos request
 *
 * This function inserts a new entry in the device constraints list of
 * requested qos performance characteristics. It recomputes the aggregate
 * QoS expectations of parameters and initializes the dev_pm_qos_request
 * handle.  Caller needs to save this handle for later use in updates and
 * removal.
 *
 * Returns 1 if the aggregated constraint value has changed,
 * 0 if the aggregated constraint value has not changed,
 * -EINVAL in case of wrong parameters, -ENODEV if the device has been
 * removed from the system
 */
int dev_pm_qos_add_request(struct device *dev, struct dev_pm_qos_request *req,
			   s32 value)
{
	int ret = 0;

	if (!dev || !req) /*guard against callers passing in null */
		return -EINVAL;

	if (dev_pm_qos_request_active(req)) {
		WARN(1, KERN_ERR "dev_pm_qos_add_request() called for already "
			"added request\n");
		return -EINVAL;
	}

	mutex_lock(&dev_pm_qos_mtx);
	req->dev = dev;

	/* Return if the device has been removed */
	if (req->dev->power.constraints_state == DEV_PM_QOS_NO_DEVICE) {
		ret = -ENODEV;
		goto out;
	}

	/*
	 * Allocate the constraints data on the first call to add_request,
	 * i.e. only if the data is not already allocated and if the device has
	 * not been removed
	 */
	if (dev->power.constraints_state == DEV_PM_QOS_DEVICE_PRESENT)
		ret = dev_pm_qos_constraints_allocate(dev);

	if (!ret)
		ret = apply_constraint(req, PM_QOS_ADD_REQ, value);

out:
	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_add_request);

/**
 * dev_pm_qos_update_request - modifies an existing qos request
 * @req : handle to list element holding a dev_pm_qos request to use
 * @new_value: defines the qos request
 *
 * Updates an existing dev PM qos request along with updating the
 * target value.
 *
 * Attempts are made to make this code callable on hot code paths.
 *
 * Returns 1 if the aggregated constraint value has changed,
 * 0 if the aggregated constraint value has not changed,
 * -EINVAL in case of wrong parameters, -ENODEV if the device has been
 * removed from the system
 */
int dev_pm_qos_update_request(struct dev_pm_qos_request *req,
			      s32 new_value)
{
	int ret = 0;

	if (!req) /*guard against callers passing in null */
		return -EINVAL;

	if (!dev_pm_qos_request_active(req)) {
		WARN(1, KERN_ERR "dev_pm_qos_update_request() called for "
			"unknown object\n");
		return -EINVAL;
	}

	mutex_lock(&dev_pm_qos_mtx);

	if (req->dev->power.constraints_state == DEV_PM_QOS_ALLOCATED) {
		if (new_value != req->node.prio)
			ret = apply_constraint(req, PM_QOS_UPDATE_REQ,
					       new_value);
	} else {
		/* Return if the device has been removed */
		ret = -ENODEV;
	}

	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_update_request);

/**
 * dev_pm_qos_remove_request - modifies an existing qos request
 * @req: handle to request list element
 *
 * Will remove pm qos request from the list of constraints and
 * recompute the current target value. Call this on slow code paths.
 *
 * Returns 1 if the aggregated constraint value has changed,
 * 0 if the aggregated constraint value has not changed,
 * -EINVAL in case of wrong parameters, -ENODEV if the device has been
 * removed from the system
 */
int dev_pm_qos_remove_request(struct dev_pm_qos_request *req)
{
	int ret = 0;

	if (!req) /*guard against callers passing in null */
		return -EINVAL;

	if (!dev_pm_qos_request_active(req)) {
		WARN(1, KERN_ERR "dev_pm_qos_remove_request() called for "
			"unknown object\n");
		return -EINVAL;
	}

	mutex_lock(&dev_pm_qos_mtx);

	if (req->dev->power.constraints_state == DEV_PM_QOS_ALLOCATED) {
		ret = apply_constraint(req, PM_QOS_REMOVE_REQ,
				       PM_QOS_DEFAULT_VALUE);
		memset(req, 0, sizeof(*req));
	} else {
		/* Return if the device has been removed */
		ret = -ENODEV;
	}

	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_remove_request);

/**
 * dev_pm_qos_add_notifier - sets notification entry for changes to target value
 * of per-device PM QoS constraints
 *
 * @dev: target device for the constraint
 * @notifier: notifier block managed by caller.
 *
 * Will register the notifier into a notification chain that gets called
 * upon changes to the target value for the device.
 */
int dev_pm_qos_add_notifier(struct device *dev, struct notifier_block *notifier)
{
	int retval = 0;

	mutex_lock(&dev_pm_qos_mtx);

	/* Silently return if the device has been removed */
	if (dev->power.constraints_state != DEV_PM_QOS_ALLOCATED)
		goto out;

	retval = blocking_notifier_chain_register(
			dev->power.constraints->notifiers,
			notifier);

out:
	mutex_unlock(&dev_pm_qos_mtx);
	return retval;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_add_notifier);

/**
 * dev_pm_qos_remove_notifier - deletes notification for changes to target value
 * of per-device PM QoS constraints
 *
 * @dev: target device for the constraint
 * @notifier: notifier block to be removed.
 *
 * Will remove the notifier from the notification chain that gets called
 * upon changes to the target value.
 */
int dev_pm_qos_remove_notifier(struct device *dev,
			       struct notifier_block *notifier)
{
	int retval = 0;

	mutex_lock(&dev_pm_qos_mtx);

	/* Silently return if the device has been removed */
	if (dev->power.constraints_state != DEV_PM_QOS_ALLOCATED)
		goto out;

	retval = blocking_notifier_chain_unregister(
			dev->power.constraints->notifiers,
			notifier);

out:
	mutex_unlock(&dev_pm_qos_mtx);
	return retval;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_remove_notifier);

/**
 * dev_pm_qos_add_global_notifier - sets notification entry for changes to
 * target value of the PM QoS constraints for any device
 *
 * @notifier: notifier block managed by caller.
 *
 * Will register the notifier into a notification chain that gets called
 * upon changes to the target value for any device.
 */
int dev_pm_qos_add_global_notifier(struct notifier_block *notifier)
{
	return blocking_notifier_chain_register(&dev_pm_notifiers, notifier);
}
EXPORT_SYMBOL_GPL(dev_pm_qos_add_global_notifier);

/**
 * dev_pm_qos_remove_global_notifier - deletes notification for changes to
 * target value of PM QoS constraints for any device
 *
 * @notifier: notifier block to be removed.
 *
 * Will remove the notifier from the notification chain that gets called
 * upon changes to the target value for any device.
 */
int dev_pm_qos_remove_global_notifier(struct notifier_block *notifier)
{
	return blocking_notifier_chain_unregister(&dev_pm_notifiers, notifier);
}
EXPORT_SYMBOL_GPL(dev_pm_qos_remove_global_notifier);
