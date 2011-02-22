/*
 * drivers/base/power/runtime.c - Helper functions for device run-time PM
 *
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 * Copyright (C) 2010 Alan Stern <stern@rowland.harvard.edu>
 *
 * This file is released under the GPLv2.
 */

#include <linux/sched.h>
#include <linux/pm_runtime.h>
#include "power.h"

static int rpm_resume(struct device *dev, int rpmflags);
static int rpm_suspend(struct device *dev, int rpmflags);

/**
 * update_pm_runtime_accounting - Update the time accounting of power states
 * @dev: Device to update the accounting for
 *
 * In order to be able to have time accounting of the various power states
 * (as used by programs such as PowerTOP to show the effectiveness of runtime
 * PM), we need to track the time spent in each state.
 * update_pm_runtime_accounting must be called each time before the
 * runtime_status field is updated, to account the time in the old state
 * correctly.
 */
void update_pm_runtime_accounting(struct device *dev)
{
	unsigned long now = jiffies;
	int delta;

	delta = now - dev->power.accounting_timestamp;

	if (delta < 0)
		delta = 0;

	dev->power.accounting_timestamp = now;

	if (dev->power.disable_depth > 0)
		return;

	if (dev->power.runtime_status == RPM_SUSPENDED)
		dev->power.suspended_jiffies += delta;
	else
		dev->power.active_jiffies += delta;
}

static void __update_runtime_status(struct device *dev, enum rpm_status status)
{
	update_pm_runtime_accounting(dev);
	dev->power.runtime_status = status;
}

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

/*
 * pm_runtime_autosuspend_expiration - Get a device's autosuspend-delay expiration time.
 * @dev: Device to handle.
 *
 * Compute the autosuspend-delay expiration time based on the device's
 * power.last_busy time.  If the delay has already expired or is disabled
 * (negative) or the power.use_autosuspend flag isn't set, return 0.
 * Otherwise return the expiration time in jiffies (adjusted to be nonzero).
 *
 * This function may be called either with or without dev->power.lock held.
 * Either way it can be racy, since power.last_busy may be updated at any time.
 */
unsigned long pm_runtime_autosuspend_expiration(struct device *dev)
{
	int autosuspend_delay;
	long elapsed;
	unsigned long last_busy;
	unsigned long expires = 0;

	if (!dev->power.use_autosuspend)
		goto out;

	autosuspend_delay = ACCESS_ONCE(dev->power.autosuspend_delay);
	if (autosuspend_delay < 0)
		goto out;

	last_busy = ACCESS_ONCE(dev->power.last_busy);
	elapsed = jiffies - last_busy;
	if (elapsed < 0)
		goto out;	/* jiffies has wrapped around. */

	/*
	 * If the autosuspend_delay is >= 1 second, align the timer by rounding
	 * up to the nearest second.
	 */
	expires = last_busy + msecs_to_jiffies(autosuspend_delay);
	if (autosuspend_delay >= 1000)
		expires = round_jiffies(expires);
	expires += !expires;
	if (elapsed >= expires - last_busy)
		expires = 0;	/* Already expired. */

 out:
	return expires;
}
EXPORT_SYMBOL_GPL(pm_runtime_autosuspend_expiration);

/**
 * rpm_check_suspend_allowed - Test whether a device may be suspended.
 * @dev: Device to test.
 */
static int rpm_check_suspend_allowed(struct device *dev)
{
	int retval = 0;

	if (dev->power.runtime_error)
		retval = -EINVAL;
	else if (atomic_read(&dev->power.usage_count) > 0
	    || dev->power.disable_depth > 0)
		retval = -EAGAIN;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;

	/* Pending resume requests take precedence over suspends. */
	else if ((dev->power.deferred_resume
			&& dev->power.runtime_status == RPM_SUSPENDING)
	    || (dev->power.request_pending
			&& dev->power.request == RPM_REQ_RESUME))
		retval = -EAGAIN;
	else if (dev->power.runtime_status == RPM_SUSPENDED)
		retval = 1;

	return retval;
}

/**
 * rpm_idle - Notify device bus type if the device can be suspended.
 * @dev: Device to notify the bus type about.
 * @rpmflags: Flag bits.
 *
 * Check if the device's run-time PM status allows it to be suspended.  If
 * another idle notification has been started earlier, return immediately.  If
 * the RPM_ASYNC flag is set then queue an idle-notification request; otherwise
 * run the ->runtime_idle() callback directly.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int rpm_idle(struct device *dev, int rpmflags)
{
	int (*callback)(struct device *);
	int retval;

	retval = rpm_check_suspend_allowed(dev);
	if (retval < 0)
		;	/* Conditions are wrong. */

	/* Idle notifications are allowed only in the RPM_ACTIVE state. */
	else if (dev->power.runtime_status != RPM_ACTIVE)
		retval = -EAGAIN;

	/*
	 * Any pending request other than an idle notification takes
	 * precedence over us, except that the timer may be running.
	 */
	else if (dev->power.request_pending &&
	    dev->power.request > RPM_REQ_IDLE)
		retval = -EAGAIN;

	/* Act as though RPM_NOWAIT is always set. */
	else if (dev->power.idle_notification)
		retval = -EINPROGRESS;
	if (retval)
		goto out;

	/* Pending requests need to be canceled. */
	dev->power.request = RPM_REQ_NONE;

	if (dev->power.no_callbacks) {
		/* Assume ->runtime_idle() callback would have suspended. */
		retval = rpm_suspend(dev, rpmflags);
		goto out;
	}

	/* Carry out an asynchronous or a synchronous idle notification. */
	if (rpmflags & RPM_ASYNC) {
		dev->power.request = RPM_REQ_IDLE;
		if (!dev->power.request_pending) {
			dev->power.request_pending = true;
			queue_work(pm_wq, &dev->power.work);
		}
		goto out;
	}

	dev->power.idle_notification = true;

	if (dev->bus && dev->bus->pm && dev->bus->pm->runtime_idle)
		callback = dev->bus->pm->runtime_idle;
	else if (dev->type && dev->type->pm && dev->type->pm->runtime_idle)
		callback = dev->type->pm->runtime_idle;
	else if (dev->class && dev->class->pm)
		callback = dev->class->pm->runtime_idle;
	else
		callback = NULL;

	if (callback) {
		spin_unlock_irq(&dev->power.lock);

		callback(dev);

		spin_lock_irq(&dev->power.lock);
	}

	dev->power.idle_notification = false;
	wake_up_all(&dev->power.wait_queue);

 out:
	return retval;
}

/**
 * rpm_callback - Run a given runtime PM callback for a given device.
 * @cb: Runtime PM callback to run.
 * @dev: Device to run the callback for.
 */
static int rpm_callback(int (*cb)(struct device *), struct device *dev)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	int retval;

	if (!cb)
		return -ENOSYS;

	if (dev->power.irq_safe) {
		retval = cb(dev);
	} else {
		spin_unlock_irq(&dev->power.lock);

		retval = cb(dev);

		spin_lock_irq(&dev->power.lock);
	}
	dev->power.runtime_error = retval;
	return retval;
}

/**
 * rpm_suspend - Carry out run-time suspend of given device.
 * @dev: Device to suspend.
 * @rpmflags: Flag bits.
 *
 * Check if the device's run-time PM status allows it to be suspended.  If
 * another suspend has been started earlier, either return immediately or wait
 * for it to finish, depending on the RPM_NOWAIT and RPM_ASYNC flags.  Cancel a
 * pending idle notification.  If the RPM_ASYNC flag is set then queue a
 * suspend request; otherwise run the ->runtime_suspend() callback directly.
 * If a deferred resume was requested while the callback was running then carry
 * it out; otherwise send an idle notification for the device (if the suspend
 * failed) or for its parent (if the suspend succeeded).
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int rpm_suspend(struct device *dev, int rpmflags)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	int (*callback)(struct device *);
	struct device *parent = NULL;
	int retval;

	dev_dbg(dev, "%s flags 0x%x\n", __func__, rpmflags);

 repeat:
	retval = rpm_check_suspend_allowed(dev);

	if (retval < 0)
		;	/* Conditions are wrong. */

	/* Synchronous suspends are not allowed in the RPM_RESUMING state. */
	else if (dev->power.runtime_status == RPM_RESUMING &&
	    !(rpmflags & RPM_ASYNC))
		retval = -EAGAIN;
	if (retval)
		goto out;

	/* If the autosuspend_delay time hasn't expired yet, reschedule. */
	if ((rpmflags & RPM_AUTO)
	    && dev->power.runtime_status != RPM_SUSPENDING) {
		unsigned long expires = pm_runtime_autosuspend_expiration(dev);

		if (expires != 0) {
			/* Pending requests need to be canceled. */
			dev->power.request = RPM_REQ_NONE;

			/*
			 * Optimization: If the timer is already running and is
			 * set to expire at or before the autosuspend delay,
			 * avoid the overhead of resetting it.  Just let it
			 * expire; pm_suspend_timer_fn() will take care of the
			 * rest.
			 */
			if (!(dev->power.timer_expires && time_before_eq(
			    dev->power.timer_expires, expires))) {
				dev->power.timer_expires = expires;
				mod_timer(&dev->power.suspend_timer, expires);
			}
			dev->power.timer_autosuspends = 1;
			goto out;
		}
	}

	/* Other scheduled or pending requests need to be canceled. */
	pm_runtime_cancel_pending(dev);

	if (dev->power.runtime_status == RPM_SUSPENDING) {
		DEFINE_WAIT(wait);

		if (rpmflags & (RPM_ASYNC | RPM_NOWAIT)) {
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

	dev->power.deferred_resume = false;
	if (dev->power.no_callbacks)
		goto no_callback;	/* Assume success. */

	/* Carry out an asynchronous or a synchronous suspend. */
	if (rpmflags & RPM_ASYNC) {
		dev->power.request = (rpmflags & RPM_AUTO) ?
		    RPM_REQ_AUTOSUSPEND : RPM_REQ_SUSPEND;
		if (!dev->power.request_pending) {
			dev->power.request_pending = true;
			queue_work(pm_wq, &dev->power.work);
		}
		goto out;
	}

	__update_runtime_status(dev, RPM_SUSPENDING);

	if (dev->bus && dev->bus->pm && dev->bus->pm->runtime_suspend)
		callback = dev->bus->pm->runtime_suspend;
	else if (dev->type && dev->type->pm && dev->type->pm->runtime_suspend)
		callback = dev->type->pm->runtime_suspend;
	else if (dev->class && dev->class->pm)
		callback = dev->class->pm->runtime_suspend;
	else
		callback = NULL;

	retval = rpm_callback(callback, dev);
	if (retval) {
		__update_runtime_status(dev, RPM_ACTIVE);
		dev->power.deferred_resume = 0;
		if (retval == -EAGAIN || retval == -EBUSY)
			dev->power.runtime_error = 0;
		else
			pm_runtime_cancel_pending(dev);
	} else {
 no_callback:
		__update_runtime_status(dev, RPM_SUSPENDED);
		pm_runtime_deactivate_timer(dev);

		if (dev->parent) {
			parent = dev->parent;
			atomic_add_unless(&parent->power.child_count, -1, 0);
		}
	}
	wake_up_all(&dev->power.wait_queue);

	if (dev->power.deferred_resume) {
		rpm_resume(dev, 0);
		retval = -EAGAIN;
		goto out;
	}

	/* Maybe the parent is now able to suspend. */
	if (parent && !parent->power.ignore_children && !dev->power.irq_safe) {
		spin_unlock(&dev->power.lock);

		spin_lock(&parent->power.lock);
		rpm_idle(parent, RPM_ASYNC);
		spin_unlock(&parent->power.lock);

		spin_lock(&dev->power.lock);
	}

 out:
	dev_dbg(dev, "%s returns %d\n", __func__, retval);

	return retval;
}

/**
 * rpm_resume - Carry out run-time resume of given device.
 * @dev: Device to resume.
 * @rpmflags: Flag bits.
 *
 * Check if the device's run-time PM status allows it to be resumed.  Cancel
 * any scheduled or pending requests.  If another resume has been started
 * earlier, either return imediately or wait for it to finish, depending on the
 * RPM_NOWAIT and RPM_ASYNC flags.  Similarly, if there's a suspend running in
 * parallel with this function, either tell the other process to resume after
 * suspending (deferred_resume) or wait for it to finish.  If the RPM_ASYNC
 * flag is set then queue a resume request; otherwise run the
 * ->runtime_resume() callback directly.  Queue an idle notification for the
 * device if the resume succeeded.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int rpm_resume(struct device *dev, int rpmflags)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	int (*callback)(struct device *);
	struct device *parent = NULL;
	int retval = 0;

	dev_dbg(dev, "%s flags 0x%x\n", __func__, rpmflags);

 repeat:
	if (dev->power.runtime_error)
		retval = -EINVAL;
	else if (dev->power.disable_depth > 0)
		retval = -EAGAIN;
	if (retval)
		goto out;

	/*
	 * Other scheduled or pending requests need to be canceled.  Small
	 * optimization: If an autosuspend timer is running, leave it running
	 * rather than cancelling it now only to restart it again in the near
	 * future.
	 */
	dev->power.request = RPM_REQ_NONE;
	if (!dev->power.timer_autosuspends)
		pm_runtime_deactivate_timer(dev);

	if (dev->power.runtime_status == RPM_ACTIVE) {
		retval = 1;
		goto out;
	}

	if (dev->power.runtime_status == RPM_RESUMING
	    || dev->power.runtime_status == RPM_SUSPENDING) {
		DEFINE_WAIT(wait);

		if (rpmflags & (RPM_ASYNC | RPM_NOWAIT)) {
			if (dev->power.runtime_status == RPM_SUSPENDING)
				dev->power.deferred_resume = true;
			else
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

	/*
	 * See if we can skip waking up the parent.  This is safe only if
	 * power.no_callbacks is set, because otherwise we don't know whether
	 * the resume will actually succeed.
	 */
	if (dev->power.no_callbacks && !parent && dev->parent) {
		spin_lock_nested(&dev->parent->power.lock, SINGLE_DEPTH_NESTING);
		if (dev->parent->power.disable_depth > 0
		    || dev->parent->power.ignore_children
		    || dev->parent->power.runtime_status == RPM_ACTIVE) {
			atomic_inc(&dev->parent->power.child_count);
			spin_unlock(&dev->parent->power.lock);
			goto no_callback;	/* Assume success. */
		}
		spin_unlock(&dev->parent->power.lock);
	}

	/* Carry out an asynchronous or a synchronous resume. */
	if (rpmflags & RPM_ASYNC) {
		dev->power.request = RPM_REQ_RESUME;
		if (!dev->power.request_pending) {
			dev->power.request_pending = true;
			queue_work(pm_wq, &dev->power.work);
		}
		retval = 0;
		goto out;
	}

	if (!parent && dev->parent) {
		/*
		 * Increment the parent's usage counter and resume it if
		 * necessary.  Not needed if dev is irq-safe; then the
		 * parent is permanently resumed.
		 */
		parent = dev->parent;
		if (dev->power.irq_safe)
			goto skip_parent;
		spin_unlock(&dev->power.lock);

		pm_runtime_get_noresume(parent);

		spin_lock(&parent->power.lock);
		/*
		 * We can resume if the parent's run-time PM is disabled or it
		 * is set to ignore children.
		 */
		if (!parent->power.disable_depth
		    && !parent->power.ignore_children) {
			rpm_resume(parent, 0);
			if (parent->power.runtime_status != RPM_ACTIVE)
				retval = -EBUSY;
		}
		spin_unlock(&parent->power.lock);

		spin_lock(&dev->power.lock);
		if (retval)
			goto out;
		goto repeat;
	}
 skip_parent:

	if (dev->power.no_callbacks)
		goto no_callback;	/* Assume success. */

	__update_runtime_status(dev, RPM_RESUMING);

	if (dev->bus && dev->bus->pm && dev->bus->pm->runtime_resume)
		callback = dev->bus->pm->runtime_resume;
	else if (dev->type && dev->type->pm && dev->type->pm->runtime_resume)
		callback = dev->type->pm->runtime_resume;
	else if (dev->class && dev->class->pm)
		callback = dev->class->pm->runtime_resume;
	else
		callback = NULL;

	retval = rpm_callback(callback, dev);
	if (retval) {
		__update_runtime_status(dev, RPM_SUSPENDED);
		pm_runtime_cancel_pending(dev);
	} else {
 no_callback:
		__update_runtime_status(dev, RPM_ACTIVE);
		if (parent)
			atomic_inc(&parent->power.child_count);
	}
	wake_up_all(&dev->power.wait_queue);

	if (!retval)
		rpm_idle(dev, RPM_ASYNC);

 out:
	if (parent && !dev->power.irq_safe) {
		spin_unlock_irq(&dev->power.lock);

		pm_runtime_put(parent);

		spin_lock_irq(&dev->power.lock);
	}

	dev_dbg(dev, "%s returns %d\n", __func__, retval);

	return retval;
}

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
		rpm_idle(dev, RPM_NOWAIT);
		break;
	case RPM_REQ_SUSPEND:
		rpm_suspend(dev, RPM_NOWAIT);
		break;
	case RPM_REQ_AUTOSUSPEND:
		rpm_suspend(dev, RPM_NOWAIT | RPM_AUTO);
		break;
	case RPM_REQ_RESUME:
		rpm_resume(dev, RPM_NOWAIT);
		break;
	}

 out:
	spin_unlock_irq(&dev->power.lock);
}

/**
 * pm_suspend_timer_fn - Timer function for pm_schedule_suspend().
 * @data: Device pointer passed by pm_schedule_suspend().
 *
 * Check if the time is right and queue a suspend request.
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
		rpm_suspend(dev, dev->power.timer_autosuspends ?
		    (RPM_ASYNC | RPM_AUTO) : RPM_ASYNC);
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
	int retval;

	spin_lock_irqsave(&dev->power.lock, flags);

	if (!delay) {
		retval = rpm_suspend(dev, RPM_ASYNC);
		goto out;
	}

	retval = rpm_check_suspend_allowed(dev);
	if (retval)
		goto out;

	/* Other scheduled or pending requests need to be canceled. */
	pm_runtime_cancel_pending(dev);

	dev->power.timer_expires = jiffies + msecs_to_jiffies(delay);
	dev->power.timer_expires += !dev->power.timer_expires;
	dev->power.timer_autosuspends = 0;
	mod_timer(&dev->power.suspend_timer, dev->power.timer_expires);

 out:
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(pm_schedule_suspend);

/**
 * __pm_runtime_idle - Entry point for run-time idle operations.
 * @dev: Device to send idle notification for.
 * @rpmflags: Flag bits.
 *
 * If the RPM_GET_PUT flag is set, decrement the device's usage count and
 * return immediately if it is larger than zero.  Then carry out an idle
 * notification, either synchronous or asynchronous.
 *
 * This routine may be called in atomic context if the RPM_ASYNC flag is set.
 */
int __pm_runtime_idle(struct device *dev, int rpmflags)
{
	unsigned long flags;
	int retval;

	if (rpmflags & RPM_GET_PUT) {
		if (!atomic_dec_and_test(&dev->power.usage_count))
			return 0;
	}

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = rpm_idle(dev, rpmflags);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(__pm_runtime_idle);

/**
 * __pm_runtime_suspend - Entry point for run-time put/suspend operations.
 * @dev: Device to suspend.
 * @rpmflags: Flag bits.
 *
 * If the RPM_GET_PUT flag is set, decrement the device's usage count and
 * return immediately if it is larger than zero.  Then carry out a suspend,
 * either synchronous or asynchronous.
 *
 * This routine may be called in atomic context if the RPM_ASYNC flag is set.
 */
int __pm_runtime_suspend(struct device *dev, int rpmflags)
{
	unsigned long flags;
	int retval;

	if (rpmflags & RPM_GET_PUT) {
		if (!atomic_dec_and_test(&dev->power.usage_count))
			return 0;
	}

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = rpm_suspend(dev, rpmflags);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(__pm_runtime_suspend);

/**
 * __pm_runtime_resume - Entry point for run-time resume operations.
 * @dev: Device to resume.
 * @rpmflags: Flag bits.
 *
 * If the RPM_GET_PUT flag is set, increment the device's usage count.  Then
 * carry out a resume, either synchronous or asynchronous.
 *
 * This routine may be called in atomic context if the RPM_ASYNC flag is set.
 */
int __pm_runtime_resume(struct device *dev, int rpmflags)
{
	unsigned long flags;
	int retval;

	if (rpmflags & RPM_GET_PUT)
		atomic_inc(&dev->power.usage_count);

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = rpm_resume(dev, rpmflags);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(__pm_runtime_resume);

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
		spin_lock_nested(&parent->power.lock, SINGLE_DEPTH_NESTING);

		/*
		 * It is invalid to put an active child under a parent that is
		 * not active, has run-time PM enabled and the
		 * 'power.ignore_children' flag unset.
		 */
		if (!parent->power.disable_depth
		    && !parent->power.ignore_children
		    && parent->power.runtime_status != RPM_ACTIVE)
			error = -EBUSY;
		else if (dev->power.runtime_status == RPM_SUSPENDED)
			atomic_inc(&parent->power.child_count);

		spin_unlock(&parent->power.lock);

		if (error)
			goto out;
	}

 out_set:
	__update_runtime_status(dev, status);
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
		rpm_resume(dev, 0);
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

		rpm_resume(dev, 0);

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
 * pm_runtime_forbid - Block run-time PM of a device.
 * @dev: Device to handle.
 *
 * Increase the device's usage count and clear its power.runtime_auto flag,
 * so that it cannot be suspended at run time until pm_runtime_allow() is called
 * for it.
 */
void pm_runtime_forbid(struct device *dev)
{
	spin_lock_irq(&dev->power.lock);
	if (!dev->power.runtime_auto)
		goto out;

	dev->power.runtime_auto = false;
	atomic_inc(&dev->power.usage_count);
	rpm_resume(dev, 0);

 out:
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(pm_runtime_forbid);

/**
 * pm_runtime_allow - Unblock run-time PM of a device.
 * @dev: Device to handle.
 *
 * Decrease the device's usage count and set its power.runtime_auto flag.
 */
void pm_runtime_allow(struct device *dev)
{
	spin_lock_irq(&dev->power.lock);
	if (dev->power.runtime_auto)
		goto out;

	dev->power.runtime_auto = true;
	if (atomic_dec_and_test(&dev->power.usage_count))
		rpm_idle(dev, RPM_AUTO);

 out:
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(pm_runtime_allow);

/**
 * pm_runtime_no_callbacks - Ignore run-time PM callbacks for a device.
 * @dev: Device to handle.
 *
 * Set the power.no_callbacks flag, which tells the PM core that this
 * device is power-managed through its parent and has no run-time PM
 * callbacks of its own.  The run-time sysfs attributes will be removed.
 */
void pm_runtime_no_callbacks(struct device *dev)
{
	spin_lock_irq(&dev->power.lock);
	dev->power.no_callbacks = 1;
	spin_unlock_irq(&dev->power.lock);
	if (device_is_registered(dev))
		rpm_sysfs_remove(dev);
}
EXPORT_SYMBOL_GPL(pm_runtime_no_callbacks);

/**
 * pm_runtime_irq_safe - Leave interrupts disabled during callbacks.
 * @dev: Device to handle
 *
 * Set the power.irq_safe flag, which tells the PM core that the
 * ->runtime_suspend() and ->runtime_resume() callbacks for this device should
 * always be invoked with the spinlock held and interrupts disabled.  It also
 * causes the parent's usage counter to be permanently incremented, preventing
 * the parent from runtime suspending -- otherwise an irq-safe child might have
 * to wait for a non-irq-safe parent.
 */
void pm_runtime_irq_safe(struct device *dev)
{
	if (dev->parent)
		pm_runtime_get_sync(dev->parent);
	spin_lock_irq(&dev->power.lock);
	dev->power.irq_safe = 1;
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(pm_runtime_irq_safe);

/**
 * update_autosuspend - Handle a change to a device's autosuspend settings.
 * @dev: Device to handle.
 * @old_delay: The former autosuspend_delay value.
 * @old_use: The former use_autosuspend value.
 *
 * Prevent runtime suspend if the new delay is negative and use_autosuspend is
 * set; otherwise allow it.  Send an idle notification if suspends are allowed.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static void update_autosuspend(struct device *dev, int old_delay, int old_use)
{
	int delay = dev->power.autosuspend_delay;

	/* Should runtime suspend be prevented now? */
	if (dev->power.use_autosuspend && delay < 0) {

		/* If it used to be allowed then prevent it. */
		if (!old_use || old_delay >= 0) {
			atomic_inc(&dev->power.usage_count);
			rpm_resume(dev, 0);
		}
	}

	/* Runtime suspend should be allowed now. */
	else {

		/* If it used to be prevented then allow it. */
		if (old_use && old_delay < 0)
			atomic_dec(&dev->power.usage_count);

		/* Maybe we can autosuspend now. */
		rpm_idle(dev, RPM_AUTO);
	}
}

/**
 * pm_runtime_set_autosuspend_delay - Set a device's autosuspend_delay value.
 * @dev: Device to handle.
 * @delay: Value of the new delay in milliseconds.
 *
 * Set the device's power.autosuspend_delay value.  If it changes to negative
 * and the power.use_autosuspend flag is set, prevent run-time suspends.  If it
 * changes the other way, allow run-time suspends.
 */
void pm_runtime_set_autosuspend_delay(struct device *dev, int delay)
{
	int old_delay, old_use;

	spin_lock_irq(&dev->power.lock);
	old_delay = dev->power.autosuspend_delay;
	old_use = dev->power.use_autosuspend;
	dev->power.autosuspend_delay = delay;
	update_autosuspend(dev, old_delay, old_use);
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(pm_runtime_set_autosuspend_delay);

/**
 * __pm_runtime_use_autosuspend - Set a device's use_autosuspend flag.
 * @dev: Device to handle.
 * @use: New value for use_autosuspend.
 *
 * Set the device's power.use_autosuspend flag, and allow or prevent run-time
 * suspends as needed.
 */
void __pm_runtime_use_autosuspend(struct device *dev, bool use)
{
	int old_delay, old_use;

	spin_lock_irq(&dev->power.lock);
	old_delay = dev->power.autosuspend_delay;
	old_use = dev->power.use_autosuspend;
	dev->power.use_autosuspend = use;
	update_autosuspend(dev, old_delay, old_use);
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(__pm_runtime_use_autosuspend);

/**
 * pm_runtime_init - Initialize run-time PM fields in given device object.
 * @dev: Device object to initialize.
 */
void pm_runtime_init(struct device *dev)
{
	dev->power.runtime_status = RPM_SUSPENDED;
	dev->power.idle_notification = false;

	dev->power.disable_depth = 1;
	atomic_set(&dev->power.usage_count, 0);

	dev->power.runtime_error = 0;

	atomic_set(&dev->power.child_count, 0);
	pm_suspend_ignore_children(dev, false);
	dev->power.runtime_auto = true;

	dev->power.request_pending = false;
	dev->power.request = RPM_REQ_NONE;
	dev->power.deferred_resume = false;
	dev->power.accounting_timestamp = jiffies;
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
	if (dev->power.irq_safe && dev->parent)
		pm_runtime_put_sync(dev->parent);
}
