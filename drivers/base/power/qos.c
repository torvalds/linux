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
 *  . A global mutex protects the constraints users from the data being
 *     allocated and free'd.
 */

#include <linux/pm_qos.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <trace/events/power.h>

#include "power.h"

static DEFINE_MUTEX(dev_pm_qos_mtx);
static DEFINE_MUTEX(dev_pm_qos_sysfs_mtx);

static BLOCKING_NOTIFIER_HEAD(dev_pm_notifiers);

/**
 * __dev_pm_qos_flags - Check PM QoS flags for a given device.
 * @dev: Device to check the PM QoS flags for.
 * @mask: Flags to check against.
 *
 * This routine must be called with dev->power.lock held.
 */
enum pm_qos_flags_status __dev_pm_qos_flags(struct device *dev, s32 mask)
{
	struct dev_pm_qos *qos = dev->power.qos;
	struct pm_qos_flags *pqf;
	s32 val;

	if (IS_ERR_OR_NULL(qos))
		return PM_QOS_FLAGS_UNDEFINED;

	pqf = &qos->flags;
	if (list_empty(&pqf->list))
		return PM_QOS_FLAGS_UNDEFINED;

	val = pqf->effective_flags & mask;
	if (val)
		return (val == mask) ? PM_QOS_FLAGS_ALL : PM_QOS_FLAGS_SOME;

	return PM_QOS_FLAGS_NONE;
}

/**
 * dev_pm_qos_flags - Check PM QoS flags for a given device (locked).
 * @dev: Device to check the PM QoS flags for.
 * @mask: Flags to check against.
 */
enum pm_qos_flags_status dev_pm_qos_flags(struct device *dev, s32 mask)
{
	unsigned long irqflags;
	enum pm_qos_flags_status ret;

	spin_lock_irqsave(&dev->power.lock, irqflags);
	ret = __dev_pm_qos_flags(dev, mask);
	spin_unlock_irqrestore(&dev->power.lock, irqflags);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_flags);

/**
 * __dev_pm_qos_read_value - Get PM QoS constraint for a given device.
 * @dev: Device to get the PM QoS constraint value for.
 *
 * This routine must be called with dev->power.lock held.
 */
s32 __dev_pm_qos_read_value(struct device *dev)
{
	return IS_ERR_OR_NULL(dev->power.qos) ?
		0 : pm_qos_read_value(&dev->power.qos->resume_latency);
}

/**
 * dev_pm_qos_read_value - Get PM QoS constraint for a given device (locked).
 * @dev: Device to get the PM QoS constraint value for.
 */
s32 dev_pm_qos_read_value(struct device *dev)
{
	unsigned long flags;
	s32 ret;

	spin_lock_irqsave(&dev->power.lock, flags);
	ret = __dev_pm_qos_read_value(dev);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return ret;
}

/**
 * apply_constraint - Add/modify/remove device PM QoS request.
 * @req: Constraint request to apply
 * @action: Action to perform (add/update/remove).
 * @value: Value to assign to the QoS request.
 *
 * Internal function to update the constraints list using the PM QoS core
 * code and if needed call the per-device and the global notification
 * callbacks
 */
static int apply_constraint(struct dev_pm_qos_request *req,
			    enum pm_qos_req_action action, s32 value)
{
	struct dev_pm_qos *qos = req->dev->power.qos;
	int ret;

	switch(req->type) {
	case DEV_PM_QOS_RESUME_LATENCY:
		ret = pm_qos_update_target(&qos->resume_latency,
					   &req->data.pnode, action, value);
		if (ret) {
			value = pm_qos_read_value(&qos->resume_latency);
			blocking_notifier_call_chain(&dev_pm_notifiers,
						     (unsigned long)value,
						     req);
		}
		break;
	case DEV_PM_QOS_LATENCY_TOLERANCE:
		ret = pm_qos_update_target(&qos->latency_tolerance,
					   &req->data.pnode, action, value);
		if (ret) {
			value = pm_qos_read_value(&qos->latency_tolerance);
			req->dev->power.set_latency_tolerance(req->dev, value);
		}
		break;
	case DEV_PM_QOS_FLAGS:
		ret = pm_qos_update_flags(&qos->flags, &req->data.flr,
					  action, value);
		break;
	default:
		ret = -EINVAL;
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
	struct dev_pm_qos *qos;
	struct pm_qos_constraints *c;
	struct blocking_notifier_head *n;

	qos = kzalloc(sizeof(*qos), GFP_KERNEL);
	if (!qos)
		return -ENOMEM;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n) {
		kfree(qos);
		return -ENOMEM;
	}
	BLOCKING_INIT_NOTIFIER_HEAD(n);

	c = &qos->resume_latency;
	plist_head_init(&c->list);
	c->target_value = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	c->default_value = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	c->no_constraint_value = PM_QOS_RESUME_LATENCY_DEFAULT_VALUE;
	c->type = PM_QOS_MIN;
	c->notifiers = n;

	c = &qos->latency_tolerance;
	plist_head_init(&c->list);
	c->target_value = PM_QOS_LATENCY_TOLERANCE_DEFAULT_VALUE;
	c->default_value = PM_QOS_LATENCY_TOLERANCE_DEFAULT_VALUE;
	c->no_constraint_value = PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT;
	c->type = PM_QOS_MIN;

	INIT_LIST_HEAD(&qos->flags.list);

	spin_lock_irq(&dev->power.lock);
	dev->power.qos = qos;
	spin_unlock_irq(&dev->power.lock);

	return 0;
}

static void __dev_pm_qos_hide_latency_limit(struct device *dev);
static void __dev_pm_qos_hide_flags(struct device *dev);

/**
 * dev_pm_qos_constraints_destroy
 * @dev: target device
 *
 * Called from the device PM subsystem on device removal under device_pm_lock().
 */
void dev_pm_qos_constraints_destroy(struct device *dev)
{
	struct dev_pm_qos *qos;
	struct dev_pm_qos_request *req, *tmp;
	struct pm_qos_constraints *c;
	struct pm_qos_flags *f;

	mutex_lock(&dev_pm_qos_sysfs_mtx);

	/*
	 * If the device's PM QoS resume latency limit or PM QoS flags have been
	 * exposed to user space, they have to be hidden at this point.
	 */
	pm_qos_sysfs_remove_resume_latency(dev);
	pm_qos_sysfs_remove_flags(dev);

	mutex_lock(&dev_pm_qos_mtx);

	__dev_pm_qos_hide_latency_limit(dev);
	__dev_pm_qos_hide_flags(dev);

	qos = dev->power.qos;
	if (!qos)
		goto out;

	/* Flush the constraints lists for the device. */
	c = &qos->resume_latency;
	plist_for_each_entry_safe(req, tmp, &c->list, data.pnode) {
		/*
		 * Update constraints list and call the notification
		 * callbacks if needed
		 */
		apply_constraint(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);
		memset(req, 0, sizeof(*req));
	}
	c = &qos->latency_tolerance;
	plist_for_each_entry_safe(req, tmp, &c->list, data.pnode) {
		apply_constraint(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);
		memset(req, 0, sizeof(*req));
	}
	f = &qos->flags;
	list_for_each_entry_safe(req, tmp, &f->list, data.flr.node) {
		apply_constraint(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);
		memset(req, 0, sizeof(*req));
	}

	spin_lock_irq(&dev->power.lock);
	dev->power.qos = ERR_PTR(-ENODEV);
	spin_unlock_irq(&dev->power.lock);

	kfree(c->notifiers);
	kfree(qos);

 out:
	mutex_unlock(&dev_pm_qos_mtx);

	mutex_unlock(&dev_pm_qos_sysfs_mtx);
}

static bool dev_pm_qos_invalid_request(struct device *dev,
				       struct dev_pm_qos_request *req)
{
	return !req || (req->type == DEV_PM_QOS_LATENCY_TOLERANCE
			&& !dev->power.set_latency_tolerance);
}

static int __dev_pm_qos_add_request(struct device *dev,
				    struct dev_pm_qos_request *req,
				    enum dev_pm_qos_req_type type, s32 value)
{
	int ret = 0;

	if (!dev || dev_pm_qos_invalid_request(dev, req))
		return -EINVAL;

	if (WARN(dev_pm_qos_request_active(req),
		 "%s() called for already added request\n", __func__))
		return -EINVAL;

	if (IS_ERR(dev->power.qos))
		ret = -ENODEV;
	else if (!dev->power.qos)
		ret = dev_pm_qos_constraints_allocate(dev);

	trace_dev_pm_qos_add_request(dev_name(dev), type, value);
	if (!ret) {
		req->dev = dev;
		req->type = type;
		ret = apply_constraint(req, PM_QOS_ADD_REQ, value);
	}
	return ret;
}

/**
 * dev_pm_qos_add_request - inserts new qos request into the list
 * @dev: target device for the constraint
 * @req: pointer to a preallocated handle
 * @type: type of the request
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
 * -EINVAL in case of wrong parameters, -ENOMEM if there's not enough memory
 * to allocate for data structures, -ENODEV if the device has just been removed
 * from the system.
 *
 * Callers should ensure that the target device is not RPM_SUSPENDED before
 * using this function for requests of type DEV_PM_QOS_FLAGS.
 */
int dev_pm_qos_add_request(struct device *dev, struct dev_pm_qos_request *req,
			   enum dev_pm_qos_req_type type, s32 value)
{
	int ret;

	mutex_lock(&dev_pm_qos_mtx);
	ret = __dev_pm_qos_add_request(dev, req, type, value);
	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_add_request);

/**
 * __dev_pm_qos_update_request - Modify an existing device PM QoS request.
 * @req : PM QoS request to modify.
 * @new_value: New value to request.
 */
static int __dev_pm_qos_update_request(struct dev_pm_qos_request *req,
				       s32 new_value)
{
	s32 curr_value;
	int ret = 0;

	if (!req) /*guard against callers passing in null */
		return -EINVAL;

	if (WARN(!dev_pm_qos_request_active(req),
		 "%s() called for unknown object\n", __func__))
		return -EINVAL;

	if (IS_ERR_OR_NULL(req->dev->power.qos))
		return -ENODEV;

	switch(req->type) {
	case DEV_PM_QOS_RESUME_LATENCY:
	case DEV_PM_QOS_LATENCY_TOLERANCE:
		curr_value = req->data.pnode.prio;
		break;
	case DEV_PM_QOS_FLAGS:
		curr_value = req->data.flr.flags;
		break;
	default:
		return -EINVAL;
	}

	trace_dev_pm_qos_update_request(dev_name(req->dev), req->type,
					new_value);
	if (curr_value != new_value)
		ret = apply_constraint(req, PM_QOS_UPDATE_REQ, new_value);

	return ret;
}

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
 *
 * Callers should ensure that the target device is not RPM_SUSPENDED before
 * using this function for requests of type DEV_PM_QOS_FLAGS.
 */
int dev_pm_qos_update_request(struct dev_pm_qos_request *req, s32 new_value)
{
	int ret;

	mutex_lock(&dev_pm_qos_mtx);
	ret = __dev_pm_qos_update_request(req, new_value);
	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_update_request);

static int __dev_pm_qos_remove_request(struct dev_pm_qos_request *req)
{
	int ret;

	if (!req) /*guard against callers passing in null */
		return -EINVAL;

	if (WARN(!dev_pm_qos_request_active(req),
		 "%s() called for unknown object\n", __func__))
		return -EINVAL;

	if (IS_ERR_OR_NULL(req->dev->power.qos))
		return -ENODEV;

	trace_dev_pm_qos_remove_request(dev_name(req->dev), req->type,
					PM_QOS_DEFAULT_VALUE);
	ret = apply_constraint(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);
	memset(req, 0, sizeof(*req));
	return ret;
}

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
 *
 * Callers should ensure that the target device is not RPM_SUSPENDED before
 * using this function for requests of type DEV_PM_QOS_FLAGS.
 */
int dev_pm_qos_remove_request(struct dev_pm_qos_request *req)
{
	int ret;

	mutex_lock(&dev_pm_qos_mtx);
	ret = __dev_pm_qos_remove_request(req);
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
 *
 * If the device's constraints object doesn't exist when this routine is called,
 * it will be created (or error code will be returned if that fails).
 */
int dev_pm_qos_add_notifier(struct device *dev, struct notifier_block *notifier)
{
	int ret = 0;

	mutex_lock(&dev_pm_qos_mtx);

	if (IS_ERR(dev->power.qos))
		ret = -ENODEV;
	else if (!dev->power.qos)
		ret = dev_pm_qos_constraints_allocate(dev);

	if (!ret)
		ret = blocking_notifier_chain_register(dev->power.qos->resume_latency.notifiers,
						       notifier);

	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
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

	/* Silently return if the constraints object is not present. */
	if (!IS_ERR_OR_NULL(dev->power.qos))
		retval = blocking_notifier_chain_unregister(dev->power.qos->resume_latency.notifiers,
							    notifier);

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

/**
 * dev_pm_qos_add_ancestor_request - Add PM QoS request for device's ancestor.
 * @dev: Device whose ancestor to add the request for.
 * @req: Pointer to the preallocated handle.
 * @type: Type of the request.
 * @value: Constraint latency value.
 */
int dev_pm_qos_add_ancestor_request(struct device *dev,
				    struct dev_pm_qos_request *req,
				    enum dev_pm_qos_req_type type, s32 value)
{
	struct device *ancestor = dev->parent;
	int ret = -ENODEV;

	switch (type) {
	case DEV_PM_QOS_RESUME_LATENCY:
		while (ancestor && !ancestor->power.ignore_children)
			ancestor = ancestor->parent;

		break;
	case DEV_PM_QOS_LATENCY_TOLERANCE:
		while (ancestor && !ancestor->power.set_latency_tolerance)
			ancestor = ancestor->parent;

		break;
	default:
		ancestor = NULL;
	}
	if (ancestor)
		ret = dev_pm_qos_add_request(ancestor, req, type, value);

	if (ret < 0)
		req->dev = NULL;

	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_add_ancestor_request);

#ifdef CONFIG_PM_RUNTIME
static void __dev_pm_qos_drop_user_request(struct device *dev,
					   enum dev_pm_qos_req_type type)
{
	struct dev_pm_qos_request *req = NULL;

	switch(type) {
	case DEV_PM_QOS_RESUME_LATENCY:
		req = dev->power.qos->resume_latency_req;
		dev->power.qos->resume_latency_req = NULL;
		break;
	case DEV_PM_QOS_LATENCY_TOLERANCE:
		req = dev->power.qos->latency_tolerance_req;
		dev->power.qos->latency_tolerance_req = NULL;
		break;
	case DEV_PM_QOS_FLAGS:
		req = dev->power.qos->flags_req;
		dev->power.qos->flags_req = NULL;
		break;
	}
	__dev_pm_qos_remove_request(req);
	kfree(req);
}

static void dev_pm_qos_drop_user_request(struct device *dev,
					 enum dev_pm_qos_req_type type)
{
	mutex_lock(&dev_pm_qos_mtx);
	__dev_pm_qos_drop_user_request(dev, type);
	mutex_unlock(&dev_pm_qos_mtx);
}

/**
 * dev_pm_qos_expose_latency_limit - Expose PM QoS latency limit to user space.
 * @dev: Device whose PM QoS latency limit is to be exposed to user space.
 * @value: Initial value of the latency limit.
 */
int dev_pm_qos_expose_latency_limit(struct device *dev, s32 value)
{
	struct dev_pm_qos_request *req;
	int ret;

	if (!device_is_registered(dev) || value < 0)
		return -EINVAL;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ret = dev_pm_qos_add_request(dev, req, DEV_PM_QOS_RESUME_LATENCY, value);
	if (ret < 0) {
		kfree(req);
		return ret;
	}

	mutex_lock(&dev_pm_qos_sysfs_mtx);

	mutex_lock(&dev_pm_qos_mtx);

	if (IS_ERR_OR_NULL(dev->power.qos))
		ret = -ENODEV;
	else if (dev->power.qos->resume_latency_req)
		ret = -EEXIST;

	if (ret < 0) {
		__dev_pm_qos_remove_request(req);
		kfree(req);
		mutex_unlock(&dev_pm_qos_mtx);
		goto out;
	}
	dev->power.qos->resume_latency_req = req;

	mutex_unlock(&dev_pm_qos_mtx);

	ret = pm_qos_sysfs_add_resume_latency(dev);
	if (ret)
		dev_pm_qos_drop_user_request(dev, DEV_PM_QOS_RESUME_LATENCY);

 out:
	mutex_unlock(&dev_pm_qos_sysfs_mtx);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_expose_latency_limit);

static void __dev_pm_qos_hide_latency_limit(struct device *dev)
{
	if (!IS_ERR_OR_NULL(dev->power.qos) && dev->power.qos->resume_latency_req)
		__dev_pm_qos_drop_user_request(dev, DEV_PM_QOS_RESUME_LATENCY);
}

/**
 * dev_pm_qos_hide_latency_limit - Hide PM QoS latency limit from user space.
 * @dev: Device whose PM QoS latency limit is to be hidden from user space.
 */
void dev_pm_qos_hide_latency_limit(struct device *dev)
{
	mutex_lock(&dev_pm_qos_sysfs_mtx);

	pm_qos_sysfs_remove_resume_latency(dev);

	mutex_lock(&dev_pm_qos_mtx);
	__dev_pm_qos_hide_latency_limit(dev);
	mutex_unlock(&dev_pm_qos_mtx);

	mutex_unlock(&dev_pm_qos_sysfs_mtx);
}
EXPORT_SYMBOL_GPL(dev_pm_qos_hide_latency_limit);

/**
 * dev_pm_qos_expose_flags - Expose PM QoS flags of a device to user space.
 * @dev: Device whose PM QoS flags are to be exposed to user space.
 * @val: Initial values of the flags.
 */
int dev_pm_qos_expose_flags(struct device *dev, s32 val)
{
	struct dev_pm_qos_request *req;
	int ret;

	if (!device_is_registered(dev))
		return -EINVAL;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ret = dev_pm_qos_add_request(dev, req, DEV_PM_QOS_FLAGS, val);
	if (ret < 0) {
		kfree(req);
		return ret;
	}

	pm_runtime_get_sync(dev);
	mutex_lock(&dev_pm_qos_sysfs_mtx);

	mutex_lock(&dev_pm_qos_mtx);

	if (IS_ERR_OR_NULL(dev->power.qos))
		ret = -ENODEV;
	else if (dev->power.qos->flags_req)
		ret = -EEXIST;

	if (ret < 0) {
		__dev_pm_qos_remove_request(req);
		kfree(req);
		mutex_unlock(&dev_pm_qos_mtx);
		goto out;
	}
	dev->power.qos->flags_req = req;

	mutex_unlock(&dev_pm_qos_mtx);

	ret = pm_qos_sysfs_add_flags(dev);
	if (ret)
		dev_pm_qos_drop_user_request(dev, DEV_PM_QOS_FLAGS);

 out:
	mutex_unlock(&dev_pm_qos_sysfs_mtx);
	pm_runtime_put(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(dev_pm_qos_expose_flags);

static void __dev_pm_qos_hide_flags(struct device *dev)
{
	if (!IS_ERR_OR_NULL(dev->power.qos) && dev->power.qos->flags_req)
		__dev_pm_qos_drop_user_request(dev, DEV_PM_QOS_FLAGS);
}

/**
 * dev_pm_qos_hide_flags - Hide PM QoS flags of a device from user space.
 * @dev: Device whose PM QoS flags are to be hidden from user space.
 */
void dev_pm_qos_hide_flags(struct device *dev)
{
	pm_runtime_get_sync(dev);
	mutex_lock(&dev_pm_qos_sysfs_mtx);

	pm_qos_sysfs_remove_flags(dev);

	mutex_lock(&dev_pm_qos_mtx);
	__dev_pm_qos_hide_flags(dev);
	mutex_unlock(&dev_pm_qos_mtx);

	mutex_unlock(&dev_pm_qos_sysfs_mtx);
	pm_runtime_put(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_qos_hide_flags);

/**
 * dev_pm_qos_update_flags - Update PM QoS flags request owned by user space.
 * @dev: Device to update the PM QoS flags request for.
 * @mask: Flags to set/clear.
 * @set: Whether to set or clear the flags (true means set).
 */
int dev_pm_qos_update_flags(struct device *dev, s32 mask, bool set)
{
	s32 value;
	int ret;

	pm_runtime_get_sync(dev);
	mutex_lock(&dev_pm_qos_mtx);

	if (IS_ERR_OR_NULL(dev->power.qos) || !dev->power.qos->flags_req) {
		ret = -EINVAL;
		goto out;
	}

	value = dev_pm_qos_requested_flags(dev);
	if (set)
		value |= mask;
	else
		value &= ~mask;

	ret = __dev_pm_qos_update_request(dev->power.qos->flags_req, value);

 out:
	mutex_unlock(&dev_pm_qos_mtx);
	pm_runtime_put(dev);
	return ret;
}

/**
 * dev_pm_qos_get_user_latency_tolerance - Get user space latency tolerance.
 * @dev: Device to obtain the user space latency tolerance for.
 */
s32 dev_pm_qos_get_user_latency_tolerance(struct device *dev)
{
	s32 ret;

	mutex_lock(&dev_pm_qos_mtx);
	ret = IS_ERR_OR_NULL(dev->power.qos)
		|| !dev->power.qos->latency_tolerance_req ?
			PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT :
			dev->power.qos->latency_tolerance_req->data.pnode.prio;
	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}

/**
 * dev_pm_qos_update_user_latency_tolerance - Update user space latency tolerance.
 * @dev: Device to update the user space latency tolerance for.
 * @val: New user space latency tolerance for @dev (negative values disable).
 */
int dev_pm_qos_update_user_latency_tolerance(struct device *dev, s32 val)
{
	int ret;

	mutex_lock(&dev_pm_qos_mtx);

	if (IS_ERR_OR_NULL(dev->power.qos)
	    || !dev->power.qos->latency_tolerance_req) {
		struct dev_pm_qos_request *req;

		if (val < 0) {
			ret = -EINVAL;
			goto out;
		}
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req) {
			ret = -ENOMEM;
			goto out;
		}
		ret = __dev_pm_qos_add_request(dev, req, DEV_PM_QOS_LATENCY_TOLERANCE, val);
		if (ret < 0) {
			kfree(req);
			goto out;
		}
		dev->power.qos->latency_tolerance_req = req;
	} else {
		if (val < 0) {
			__dev_pm_qos_drop_user_request(dev, DEV_PM_QOS_LATENCY_TOLERANCE);
			ret = 0;
		} else {
			ret = __dev_pm_qos_update_request(dev->power.qos->latency_tolerance_req, val);
		}
	}

 out:
	mutex_unlock(&dev_pm_qos_mtx);
	return ret;
}
#else /* !CONFIG_PM_RUNTIME */
static void __dev_pm_qos_hide_latency_limit(struct device *dev) {}
static void __dev_pm_qos_hide_flags(struct device *dev) {}
#endif /* CONFIG_PM_RUNTIME */
