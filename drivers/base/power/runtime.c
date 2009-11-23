/*
 * drivers/base/power/runtime.c - Helper functions for device run-time PM
 *
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 *
 * This file is released under the GPLv2.
 */

#include <linux/sched.h>
#include <linux/pm_runtime.h>
#include <linux/jiffies.h>

static int __pm_runtime_resume(struct device *dev, bool from_wq);
static int __pm_request_idle(struct device *dev);
static int __pm_request_resume(struct device *dev);

/**
 * pm_runtime_deactivate_timer - Deactivate given device's suspend timer.
 * @dev: Device to handle.
 */
static void pm_runtime_deactivate_timer(struct device *dev)
{
	if (dev->power.timer_expires > 0) {
		del_timer(&dev->power.suspend_timer);
		dev->power.timer_expires = 0;
	}
}

/**
 * pm_runtime_cancel_pending - Deactivate suspend timer and cancel requests.
 * @dev: Device to handle.
 */
static void pm_runtime_cancel_pending(struct device *dev)
{
	pm_runtime_deactivate_timer(dev);
	/*
	 * In case there's a request pending, make sure its work function will
	 * return without doing anything.
	 */
	dev->power.request = RPM_REQ_NONE;
}

/**
 * __pm_runtime_idle - Notify device bus type if the device can be suspended.
 * @dev: Device to notify the bus type about.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int __pm_runtime_idle(struct device *dev)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	int retval = 0;

	if (dev->power.runtime_error)
		retval = -EINVAL;
	else if (dev->power.idle_notification)
		retval = -EINPROGRESS;
	else if (atomic_read(&dev->power.usage_count) > 0
	    || dev->power.disable_depth > 0
	    || dev->power.runtime_status != RPM_ACTIVE)
		retval = -EAGAIN;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;
	if (retval)
		goto out;

	if (dev->power.request_pending) {
		/*
		 * If an idle notification request is pending, cancel it.  Any
		 * other pending request takes precedence over us.
		 */
		if (dev->power.request == RPM_REQ_IDLE) {
			dev->power.request = RPM_REQ_NONE;
		} else if (dev->power.request != RPM_REQ_NONE) {
			retval = -EAGAIN;
			goto out;
		}
	}

	dev->power.idle_notification = true;

	if (dev->bus && dev->bus->pm && dev->bus->pm->runtime_idle) {
		spin_unlock_irq(&dev->power.lock);

		dev->bus->pm->runtime_idle(dev);

		spin_lock_irq(&dev->power.lock);
	}

	dev->power.idle_notification = false;
	wake_up_all(&dev->power.wait_queue);

 out:
	return retval;
}

/**
 * pm_runtime_idle - Notify device bus type if the device can be suspended.
 * @dev: Device to notify the bus type about.
 */
int pm_runtime_idle(struct device *dev)
{
	int retval;

	spin_lock_irq(&dev->power.lock);
	retval = __pm_runtime_idle(dev);
	spin_unlock_irq(&dev->power.lock);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_runtime_idle);

/**
 * __pm_runtime_suspend - Carry out run-time suspend of given device.
 * @dev: Device to suspend.
 * @from_wq: If set, the function has been called via pm_wq.
 *
 * Check if the device can be suspended and run the ->runtime_suspend() callback
 * provided by its bus type.  If another suspend has been started earlier, wait
 * for it to finish.  If an idle notification or suspend request is pending or
 * scheduled, cancel it.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
int __pm_runtime_suspend(struct device *dev, bool from_wq)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	struct device *parent = NULL;
	bool notify = false;
	int retval = 0;

	dev_dbg(dev, "__pm_runtime_suspend()%s!\n",
		from_wq ? " from workqueue" : "");

 repeat:
	if (dev->power.runtime_error) {
		retval = -EINVAL;
		goto out;
	}

	/* Pending resume requests take precedence over us. */
	if (dev->power.request_pending
	    && dev->power.request == RPM_REQ_RESUME) {
		retval = -EAGAIN;
		goto out;
	}

	/* Other scheduled or pending requests need to be canceled. */
	pm_runtime_cancel_pending(dev);

	if (dev->power.runtime_status == RPM_SUSPENDED)
		retval = 1;
	else if (dev->power.runtime_status == RPM_RESUMING
	    || dev->power.disable_depth > 0
	    || atomic_read(&dev->power.usage_count) > 0)
		retval = -EAGAIN;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;
	if (retval)
		goto out;

	if (dev->power.runtime_status == RPM_SUSPENDING) {
		DEFINE_WAIT(wait);

		if (from_wq) {
			retval = -EINPROGRESS;
			goto out;
		}

		/* Wait for the other suspend running in parallel with us. */
		for (;;) {
			prepare_to_wait(&dev->power.wait_queue, &wait,
					TASK_UNINTERRUPTIBLE);
			if (dev->power.runtime_status != RPM_SUSPENDING)
				break;

			spin_unlock_irq(&dev->power.lock);

			schedule();

			spin_lock_irq(&dev->power.lock);
		}
		finish_wait(&dev->power.wait_queue, &wait);
		goto repeat;
	}

	dev->power.runtime_status = RPM_SUSPENDING;

	if (dev->bus && dev->bus->pm && dev->bus->pm->runtime_suspend) {
		spin_unlock_irq(&dev->power.lock);

		retval = dev->bus->pm->runtime_suspend(dev);

		spin_lock_irq(&dev->power.lock);
		dev->power.runtime_error = retval;
	} else {
		retval = -ENOSYS;
	}

	if (retval) {
		dev->power.runtime_status = RPM_ACTIVE;
		pm_runtime_cancel_pending(dev);
		dev->power.deferred_resume = false;

		if (retval == -EAGAIN || retval == -EBUSY) {
			notify = true;
			dev->power.runtime_error = 0;
		}
	} else {
		dev->power.runtime_status = RPM_SUSPENDED;

		if (dev->parent) {
			parent = dev->parent;
			atomic_add_unless(&parent->power.child_count, -1, 0);
		}
	}
	wake_up_all(&dev->power.wait_queue);

	if (dev->power.deferred_resume) {
		dev->power.deferred_resume = false;
		__pm_runtime_resume(dev, false);
		retval = -EAGAIN;
		goto out;
	}

	if (notify)
		__pm_runtime_idle(dev);

	if (parent && !parent->power.ignore_children) {
		spin_unlock_irq(&dev->power.lock);

		pm_request_idle(parent);

		spin_lock_irq(&dev->power.lock);
	}

 out:
	dev_dbg(dev, "__pm_runtime_suspend() returns %d!\n", retval);

	return retval;
}

/**
 * pm_runtime_suspend - Carry out run-time suspend of given device.
 * @dev: Device to suspend.
 */
int pm_runtime_suspend(struct device *dev)
{
	int retval;

	spin_lock_irq(&dev->power.lock);
	retval = __pm_runtime_suspend(dev, false);
	spin_unlock_irq(&dev->power.lock);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_runtime_suspend);

/**
 * __pm_runtime_resume - Carry out run-time resume of given device.
 * @dev: Device to resume.
 * @from_wq: If set, the function has been called via pm_wq.
 *
 * Check if the device can be woken up and run the ->runtime_resume() callback
 * provided by its bus type.  If another resume has been started earlier, wait
 * for it to finish.  If there's a suspend running in parallel with this
 * function, wait for it to finish and resume the device.  Cancel any scheduled
 * or pending requests.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
int __pm_runtime_resume(struct device *dev, bool from_wq)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	struct device *parent = NULL;
	int retval = 0;

	dev_dbg(dev, "__pm_runtime_resume()%s!\n",
		from_wq ? " from workqueue" : "");

 repeat:
	if (dev->power.runtime_error) {
		retval = -EINVAL;
		goto out;
	}

	pm_runtime_cancel_pending(dev);

	if (dev->power.runtime_status == RPM_ACTIVE)
		retval = 1;
	else if (dev->power.disable_depth > 0)
		retval = -EAGAIN;
	if (retval)
		goto out;

	if (dev->power.runtime_status == RPM_RESUMING
	    || dev->power.runtime_status == RPM_SUSPENDING) {
		DEFINE_WAIT(wait);

		if (from_wq) {
			if (dev->power.runtime_status == RPM_SUSPENDING)
				dev->power.deferred_resume = true;
			retval = -EINPROGRESS;
			goto out;
		}

		/* Wait for the operation carried out in parallel with us. */
		for (;;) {
			prepare_to_wait(&dev->power.wait_queue, &wait,
					TASK_UNINTERRUPTIBLE);
			if (dev->power.runtime_status != RPM_RESUMING
			    && dev->power.runtime_status != RPM_SUSPENDING)
				break;

			spin_unlock_irq(&dev->power.lock);

			schedule();

			spin_lock_irq(&dev->power.lock);
		}
		finish_wait(&dev->power.wait_queue, &wait);
		goto repeat;
	}

	if (!parent && dev->parent) {
		/*
		 * Increment the parent's resume counter and resume it if
		 * necessary.
		 */
		parent = dev->parent;
		spin_unlock_irq(&dev->power.lock);

		pm_runtime_get_noresume(parent);

		spin_lock_irq(&parent->power.lock);
		/*
		 * We can resume if the parent's run-time PM is disabled or it
		 * is set to ignore children.
		 */
		if (!parent->power.disable_depth
		    && !parent->power.ignore_children) {
			__pm_runtime_resume(parent, false);
			if (parent->power.runtime_status != RPM_ACTIVE)
				retval = -EBUSY;
		}
		spin_unlock_irq(&parent->power.lock);

		spin_lock_irq(&dev->power.lock);
		if (retval)
			goto out;
		goto repeat;
	}

	dev->power.runtime_status = RPM_RESUMING;

	if (dev->bus && dev->bus->pm && dev->bus->pm->runtime_resume) {
		spin_unlock_irq(&dev->power.lock);

		retval = dev->bus->pm->runtime_resume(dev);

		spin_lock_irq(&dev->power.lock);
		dev->power.runtime_error = retval;
	} else {
		retval = -ENOSYS;
	}

	if (retval) {
		dev->power.runtime_status = RPM_SUSPENDED;
		pm_runtime_cancel_pending(dev);
	} else {
		dev->power.runtime_status = RPM_ACTIVE;
		if (parent)
			atomic_inc(&parent->power.child_count);
	}
	wake_up_all(&dev->power.wait_queue);

	if (!retval)
		__pm_request_idle(dev);

 out:
	if (parent) {
		spin_unlock_irq(&dev->power.lock);

		pm_runtime_put(parent);

		spin_lock_irq(&dev->power.lock);
	}

	dev_dbg(dev, "__pm_runtime_resume() returns %d!\n", retval);

	return retval;
}

/**
 * pm_runtime_resume - Carry out run-time resume of given device.
 * @dev: Device to suspend.
 */
int pm_runtime_resume(struct device *dev)
{
	int retval;

	spin_lock_irq(&dev->power.lock);
	retval = __pm_runtime_resume(dev, false);
	spin_unlock_irq(&dev->power.lock);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_runtime_resume);

/**
 * pm_runtime_work - Universal run-time PM work function.
 * @work: Work structure used for scheduling the execution of this function.
 *
 * Use @work to get the device object the work is to be done for, determine what
 * is to be done and execute the appropriate run-time PM function.
 */
static void pm_runtime_work(struct work_struct *work)
{
	struct device *dev = container_of(work, struct device, power.work);
	enum rpm_request req;

	spin_lock_irq(&dev->power.lock);

	if (!dev->power.request_pending)
		goto out;

	req = dev->power.request;
	dev->power.request = RPM_REQ_NONE;
	dev->power.request_pending = false;

	switch (req) {
	case RPM_REQ_NONE:
		break;
	case RPM_REQ_IDLE:
		__pm_runtime_idle(dev);
		break;
	case RPM_REQ_SUSPEND:
		__pm_runtime_suspend(dev, true);
		break;
	case RPM_REQ_RESUME:
		__pm_runtime_resume(dev, true);
		break;
	}

 out:
	spin_unlock_irq(&dev->power.lock);
}

/**
 * __pm_request_idle - Submit an idle notification request for given device.
 * @dev: Device to handle.
 *
 * Check if the device's run-time PM status is correct for suspending the device
 * and queue up a request to run __pm_runtime_idle() for it.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int __pm_request_idle(struct device *dev)
{
	int retval = 0;

	if (dev->power.runtime_error)
		retval = -EINVAL;
	else if (atomic_read(&dev->power.usage_count) > 0
	    || dev->power.disable_depth > 0
	    || dev->power.runtime_status == RPM_SUSPENDED
	    || dev->power.runtime_status == RPM_SUSPENDING)
		retval = -EAGAIN;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;
	if (retval)
		return retval;

	if (dev->power.request_pending) {
		/* Any requests other then RPM_REQ_IDLE take precedence. */
		if (dev->power.request == RPM_REQ_NONE)
			dev->power.request = RPM_REQ_IDLE;
		else if (dev->power.request != RPM_REQ_IDLE)
			retval = -EAGAIN;
		return retval;
	}

	dev->power.request = RPM_REQ_IDLE;
	dev->power.request_pending = true;
	queue_work(pm_wq, &dev->power.work);

	return retval;
}

/**
 * pm_request_idle - Submit an idle notification request for given device.
 * @dev: Device to handle.
 */
int pm_request_idle(struct device *dev)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = __pm_request_idle(dev);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_request_idle);

/**
 * __pm_request_suspend - Submit a suspend request for given device.
 * @dev: Device to suspend.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int __pm_request_suspend(struct device *dev)
{
	int retval = 0;

	if (dev->power.runtime_error)
		return -EINVAL;

	if (dev->power.runtime_status == RPM_SUSPENDED)
		retval = 1;
	else if (atomic_read(&dev->power.usage_count) > 0
	    || dev->power.disable_depth > 0)
		retval = -EAGAIN;
	else if (dev->power.runtime_status == RPM_SUSPENDING)
		retval = -EINPROGRESS;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;
	if (retval < 0)
		return retval;

	pm_runtime_deactivate_timer(dev);

	if (dev->power.request_pending) {
		/*
		 * Pending resume requests take precedence over us, but we can
		 * overtake any other pending request.
		 */
		if (dev->power.request == RPM_REQ_RESUME)
			retval = -EAGAIN;
		else if (dev->power.request != RPM_REQ_SUSPEND)
			dev->power.request = retval ?
						RPM_REQ_NONE : RPM_REQ_SUSPEND;
		return retval;
	} else if (retval) {
		return retval;
	}

	dev->power.request = RPM_REQ_SUSPEND;
	dev->power.request_pending = true;
	queue_work(pm_wq, &dev->power.work);

	return 0;
}

/**
 * pm_suspend_timer_fn - Timer function for pm_schedule_suspend().
 * @data: Device pointer passed by pm_schedule_suspend().
 *
 * Check if the time is right and execute __pm_request_suspend() in that case.
 */
static void pm_suspend_timer_fn(unsigned long data)
{
	struct device *dev = (struct device *)data;
	unsigned long flags;
	unsigned long expires;

	spin_lock_irqsave(&dev->power.lock, flags);

	expires = dev->power.timer_expires;
	/* If 'expire' is after 'jiffies' we've been called too early. */
	if (expires > 0 && !time_after(expires, jiffies)) {
		dev->power.timer_expires = 0;
		__pm_request_suspend(dev);
	}

	spin_unlock_irqrestore(&dev->power.lock, flags);
}

/**
 * pm_schedule_suspend - Set up a timer to submit a suspend request in future.
 * @dev: Device to suspend.
 * @delay: Time to wait before submitting a suspend request, in milliseconds.
 */
int pm_schedule_suspend(struct device *dev, unsigned int delay)
{
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&dev->power.lock, flags);

	if (dev->power.runtime_error) {
		retval = -EINVAL;
		goto out;
	}

	if (!delay) {
		retval = __pm_request_suspend(dev);
		goto out;
	}

	pm_runtime_deactivate_timer(dev);

	if (dev->power.request_pending) {
		/*
		 * Pending resume requests take precedence over us, but any
		 * other pending requests have to be canceled.
		 */
		if (dev->power.request == RPM_REQ_RESUME) {
			retval = -EAGAIN;
			goto out;
		}
		dev->power.request = RPM_REQ_NONE;
	}

	if (dev->power.runtime_status == RPM_SUSPENDED)
		retval = 1;
	else if (dev->power.runtime_status == RPM_SUSPENDING)
		retval = -EINPROGRESS;
	else if (atomic_read(&dev->power.usage_count) > 0
	    || dev->power.disable_depth > 0)
		retval = -EAGAIN;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;
	if (retval)
		goto out;

	dev->power.timer_expires = jiffies + msecs_to_jiffies(delay);
	mod_timer(&dev->power.suspend_timer, dev->power.timer_expires);

 out:
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_schedule_suspend);

/**
 * pm_request_resume - Submit a resume request for given device.
 * @dev: Device to resume.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int __pm_request_resume(struct device *dev)
{
	int retval = 0;

	if (dev->power.runtime_error)
		return -EINVAL;

	if (dev->power.runtime_status == RPM_ACTIVE)
		retval = 1;
	else if (dev->power.runtime_status == RPM_RESUMING)
		retval = -EINPROGRESS;
	else if (dev->power.disable_depth > 0)
		retval = -EAGAIN;
	if (retval < 0)
		return retval;

	pm_runtime_deactivate_timer(dev);

	if (dev->power.request_pending) {
		/* If non-resume request is pending, we can overtake it. */
		dev->power.request = retval ? RPM_REQ_NONE : RPM_REQ_RESUME;
		return retval;
	} else if (retval) {
		return retval;
	}

	dev->power.request = RPM_REQ_RESUME;
	dev->power.request_pending = true;
	queue_work(pm_wq, &dev->power.work);

	return retval;
}

/**
 * pm_request_resume - Submit a resume request for given device.
 * @dev: Device to resume.
 */
int pm_request_resume(struct device *dev)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = __pm_request_resume(dev);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_request_resume);

/**
 * __pm_runtime_get - Reference count a device and wake it up, if necessary.
 * @dev: Device to handle.
 * @sync: If set and the device is suspended, resume it synchronously.
 *
 * Increment the usage count of the device and if it was zero previously,
 * resume it or submit a resume request for it, depending on the value of @sync.
 */
int __pm_runtime_get(struct device *dev, bool sync)
{
	int retval = 1;

	if (atomic_add_return(1, &dev->power.usage_count) == 1)
		retval = sync ? pm_runtime_resume(dev) : pm_request_resume(dev);

	return retval;
}
EXPORT_SYMBOL_GPL(__pm_runtime_get);

/**
 * __pm_runtime_put - Decrement the device's usage counter and notify its bus.
 * @dev: Device to handle.
 * @sync: If the device's bus type is to be notified, do that synchronously.
 *
 * Decrement the usage count of the device and if it reaches zero, carry out a
 * synchronous idle notification or submit an idle notification request for it,
 * depending on the value of @sync.
 */
int __pm_runtime_put(struct device *dev, bool sync)
{
	int retval = 0;

	if (atomic_dec_and_test(&dev->power.usage_count))
		retval = sync ? pm_runtime_idle(dev) : pm_request_idle(dev);

	return retval;
}
EXPORT_SYMBOL_GPL(__pm_runtime_put);

/**
 * __pm_runtime_set_status - Set run-time PM status of a device.
 * @dev: Device to handle.
 * @status: New run-time PM status of the device.
 *
 * If run-time PM of the device is disabled or its power.runtime_error field is
 * different from zero, the status may be changed either to RPM_ACTIVE, or to
 * RPM_SUSPENDED, as long as that reflects the actual state of the device.
 * However, if the device has a parent and the parent is not active, and the
 * parent's power.ignore_children flag is unset, the device's status cannot be
 * set to RPM_ACTIVE, so -EBUSY is returned in that case.
 *
 * If successful, __pm_runtime_set_status() clears the power.runtime_error field
 * and the device parent's counter of unsuspended children is modified to
 * reflect the new status.  If the new status is RPM_SUSPENDED, an idle
 * notification request for the parent is submitted.
 */
int __pm_runtime_set_status(struct device *dev, unsigned int status)
{
	struct device *parent = dev->parent;
	unsigned long flags;
	bool notify_parent = false;
	int error = 0;

	if (status != RPM_ACTIVE && status != RPM_SUSPENDED)
		return -EINVAL;

	spin_lock_irqsave(&dev->power.lock, flags);

	if (!dev->power.runtime_error && !dev->power.disable_depth) {
		error = -EAGAIN;
		goto out;
	}

	if (dev->power.runtime_status == status)
		goto out_set;

	if (status == RPM_SUSPENDED) {
		/* It always is possible to set the status to 'suspended'. */
		if (parent) {
			atomic_add_unless(&parent->power.child_count, -1, 0);
			notify_parent = !parent->power.ignore_children;
		}
		goto out_set;
	}

	if (parent) {
		spin_lock_irq(&parent->power.lock);

		/*
		 * It is invalid to put an active child under a parent that is
		 * not active, has run-time PM enabled and the
		 * 'power.ignore_children' flag unset.
		 */
		if (!parent->power.disable_depth
		    && !parent->power.ignore_children
		    && parent->power.runtime_status != RPM_ACTIVE) {
			error = -EBUSY;
		} else {
			if (dev->power.runtime_status == RPM_SUSPENDED)
				atomic_inc(&parent->power.child_count);
		}

		spin_unlock_irq(&parent->power.lock);

		if (error)
			goto out;
	}

 out_set:
	dev->power.runtime_status = status;
	dev->power.runtime_error = 0;
 out:
	spin_unlock_irqrestore(&dev->power.lock, flags);

	if (notify_parent)
		pm_request_idle(parent);

	return error;
}
EXPORT_SYMBOL_GPL(__pm_runtime_set_status);

/**
 * __pm_runtime_barrier - Cancel pending requests and wait for completions.
 * @dev: Device to handle.
 *
 * Flush all pending requests for the device from pm_wq and wait for all
 * run-time PM operations involving the device in progress to complete.
 *
 * Should be called under dev->power.lock with interrupts disabled.
 */
static void __pm_runtime_barrier(struct device *dev)
{
	pm_runtime_deactivate_timer(dev);

	if (dev->power.request_pending) {
		dev->power.request = RPM_REQ_NONE;
		spin_unlock_irq(&dev->power.lock);

		cancel_work_sync(&dev->power.work);

		spin_lock_irq(&dev->power.lock);
		dev->power.request_pending = false;
	}

	if (dev->power.runtime_status == RPM_SUSPENDING
	    || dev->power.runtime_status == RPM_RESUMING
	    || dev->power.idle_notification) {
		DEFINE_WAIT(wait);

		/* Suspend, wake-up or idle notification in progress. */
		for (;;) {
			prepare_to_wait(&dev->power.wait_queue, &wait,
					TASK_UNINTERRUPTIBLE);
			if (dev->power.runtime_status != RPM_SUSPENDING
			    && dev->power.runtime_status != RPM_RESUMING
			    && !dev->power.idle_notification)
				break;
			spin_unlock_irq(&dev->power.lock);

			schedule();

			spin_lock_irq(&dev->power.lock);
		}
		finish_wait(&dev->power.wait_queue, &wait);
	}
}

/**
 * pm_runtime_barrier - Flush pending requests and wait for completions.
 * @dev: Device to handle.
 *
 * Prevent the device from being suspended by incrementing its usage counter and
 * if there's a pending resume request for the device, wake the device up.
 * Next, make sure that all pending requests for the device have been flushed
 * from pm_wq and wait for all run-time PM operations involving the device in
 * progress to complete.
 *
 * Return value:
 * 1, if there was a resume request pending and the device had to be woken up,
 * 0, otherwise
 */
int pm_runtime_barrier(struct device *dev)
{
	int retval = 0;

	pm_runtime_get_noresume(dev);
	spin_lock_irq(&dev->power.lock);

	if (dev->power.request_pending
	    && dev->power.request == RPM_REQ_RESUME) {
		__pm_runtime_resume(dev, false);
		retval = 1;
	}

	__pm_runtime_barrier(dev);

	spin_unlock_irq(&dev->power.lock);
	pm_runtime_put_noidle(dev);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_runtime_barrier);

/**
 * __pm_runtime_disable - Disable run-time PM of a device.
 * @dev: Device to handle.
 * @check_resume: If set, check if there's a resume request for the device.
 *
 * Increment power.disable_depth for the device and if was zero previously,
 * cancel all pending run-time PM requests for the device and wait for all
 * operations in progress to complete.  The device can be either active or
 * suspended after its run-time PM has been disabled.
 *
 * If @check_resume is set and there's a resume request pending when
 * __pm_runtime_disable() is called and power.disable_depth is zero, the
 * function will wake up the device before disabling its run-time PM.
 */
void __pm_runtime_disable(struct device *dev, bool check_resume)
{
	spin_lock_irq(&dev->power.lock);

	if (dev->power.disable_depth > 0) {
		dev->power.disable_depth++;
		goto out;
	}

	/*
	 * Wake up the device if there's a resume request pending, because that
	 * means there probably is some I/O to process and disabling run-time PM
	 * shouldn't prevent the device from processing the I/O.
	 */
	if (check_resume && dev->power.request_pending
	    && dev->power.request == RPM_REQ_RESUME) {
		/*
		 * Prevent suspends and idle notifications from being carried
		 * out after we have woken up the device.
		 */
		pm_runtime_get_noresume(dev);

		__pm_runtime_resume(dev, false);

		pm_runtime_put_noidle(dev);
	}

	if (!dev->power.disable_depth++)
		__pm_runtime_barrier(dev);

 out:
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(__pm_runtime_disable);

/**
 * pm_runtime_enable - Enable run-time PM of a device.
 * @dev: Device to handle.
 */
void pm_runtime_enable(struct device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->power.lock, flags);

	if (dev->power.disable_depth > 0)
		dev->power.disable_depth--;
	else
		dev_warn(dev, "Unbalanced %s!\n", __func__);

	spin_unlock_irqrestore(&dev->power.lock, flags);
}
EXPORT_SYMBOL_GPL(pm_runtime_enable);

/**
 * pm_runtime_init - Initialize run-time PM fields in given device object.
 * @dev: Device object to initialize.
 */
void pm_runtime_init(struct device *dev)
{
	spin_lock_init(&dev->power.lock);

	dev->power.runtime_status = RPM_SUSPENDED;
	dev->power.idle_notification = false;

	dev->power.disable_depth = 1;
	atomic_set(&dev->power.usage_count, 0);

	dev->power.runtime_error = 0;

	atomic_set(&dev->power.child_count, 0);
	pm_suspend_ignore_children(dev, false);

	dev->power.request_pending = false;
	dev->power.request = RPM_REQ_NONE;
	dev->power.deferred_resume = false;
	INIT_WORK(&dev->power.work, pm_runtime_work);

	dev->power.timer_expires = 0;
	setup_timer(&dev->power.suspend_timer, pm_suspend_timer_fn,
			(unsigned long)dev);

	init_waitqueue_head(&dev->power.wait_queue);
}

/**
 * pm_runtime_remove - Prepare for removing a device from device hierarchy.
 * @dev: Device object being removed from device hierarchy.
 */
void pm_runtime_remove(struct device *dev)
{
	__pm_runtime_disable(dev, false);

	/* Change the status back to 'suspended' to match the initial status. */
	if (dev->power.runtime_status == RPM_ACTIVE)
		pm_runtime_set_suspended(dev);
}
