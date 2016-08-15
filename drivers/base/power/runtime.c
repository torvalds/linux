/*
 * drivers/base/power/runtime.c - Helper functions for device runtime PM
 *
 * Copyright (c) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 * Copyright (C) 2010 Alan Stern <stern@rowland.harvard.edu>
 *
 * This file is released under the GPLv2.
 */

#include <linux/sched.h>
#include <linux/export.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <trace/events/rpm.h>
#include "power.h"

typedef int (*pm_callback_t)(struct device *);

static pm_callback_t __rpm_get_callback(struct device *dev, size_t cb_offset)
{
	pm_callback_t cb;
	const struct dev_pm_ops *ops;

	if (dev->pm_domain)
		ops = &dev->pm_domain->ops;
	else if (dev->type && dev->type->pm)
		ops = dev->type->pm;
	else if (dev->class && dev->class->pm)
		ops = dev->class->pm;
	else if (dev->bus && dev->bus->pm)
		ops = dev->bus->pm;
	else
		ops = NULL;

	if (ops)
		cb = *(pm_callback_t *)((void *)ops + cb_offset);
	else
		cb = NULL;

	if (!cb && dev->driver && dev->driver->pm)
		cb = *(pm_callback_t *)((void *)dev->driver->pm + cb_offset);

	return cb;
}

#define RPM_GET_CALLBACK(dev, callback) \
		__rpm_get_callback(dev, offsetof(struct dev_pm_ops, callback))

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
	unsigned long delta;

	delta = now - dev->power.accounting_timestamp;

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

static int dev_memalloc_noio(struct device *dev, void *data)
{
	return dev->power.memalloc_noio;
}

/*
 * pm_runtime_set_memalloc_noio - Set a device's memalloc_noio flag.
 * @dev: Device to handle.
 * @enable: True for setting the flag and False for clearing the flag.
 *
 * Set the flag for all devices in the path from the device to the
 * root device in the device tree if @enable is true, otherwise clear
 * the flag for devices in the path whose siblings don't set the flag.
 *
 * The function should only be called by block device, or network
 * device driver for solving the deadlock problem during runtime
 * resume/suspend:
 *
 *     If memory allocation with GFP_KERNEL is called inside runtime
 *     resume/suspend callback of any one of its ancestors(or the
 *     block device itself), the deadlock may be triggered inside the
 *     memory allocation since it might not complete until the block
 *     device becomes active and the involed page I/O finishes. The
 *     situation is pointed out first by Alan Stern. Network device
 *     are involved in iSCSI kind of situation.
 *
 * The lock of dev_hotplug_mutex is held in the function for handling
 * hotplug race because pm_runtime_set_memalloc_noio() may be called
 * in async probe().
 *
 * The function should be called between device_add() and device_del()
 * on the affected device(block/network device).
 */
void pm_runtime_set_memalloc_noio(struct device *dev, bool enable)
{
	static DEFINE_MUTEX(dev_hotplug_mutex);

	mutex_lock(&dev_hotplug_mutex);
	for (;;) {
		bool enabled;

		/* hold power lock since bitfield is not SMP-safe. */
		spin_lock_irq(&dev->power.lock);
		enabled = dev->power.memalloc_noio;
		dev->power.memalloc_noio = enable;
		spin_unlock_irq(&dev->power.lock);

		/*
		 * not need to enable ancestors any more if the device
		 * has been enabled.
		 */
		if (enabled && enable)
			break;

		dev = dev->parent;

		/*
		 * clear flag of the parent device only if all the
		 * children don't set the flag because ancestor's
		 * flag was set by any one of the descendants.
		 */
		if (!dev || (!enable &&
			     device_for_each_child(dev, NULL,
						   dev_memalloc_noio)))
			break;
	}
	mutex_unlock(&dev_hotplug_mutex);
}
EXPORT_SYMBOL_GPL(pm_runtime_set_memalloc_noio);

/**
 * rpm_check_suspend_allowed - Test whether a device may be suspended.
 * @dev: Device to test.
 */
static int rpm_check_suspend_allowed(struct device *dev)
{
	int retval = 0;

	if (dev->power.runtime_error)
		retval = -EINVAL;
	else if (dev->power.disable_depth > 0)
		retval = -EACCES;
	else if (atomic_read(&dev->power.usage_count) > 0)
		retval = -EAGAIN;
	else if (!pm_children_suspended(dev))
		retval = -EBUSY;

	/* Pending resume requests take precedence over suspends. */
	else if ((dev->power.deferred_resume
			&& dev->power.runtime_status == RPM_SUSPENDING)
	    || (dev->power.request_pending
			&& dev->power.request == RPM_REQ_RESUME))
		retval = -EAGAIN;
	else if (__dev_pm_qos_read_value(dev) < 0)
		retval = -EPERM;
	else if (dev->power.runtime_status == RPM_SUSPENDED)
		retval = 1;

	return retval;
}

/**
 * __rpm_callback - Run a given runtime PM callback for a given device.
 * @cb: Runtime PM callback to run.
 * @dev: Device to run the callback for.
 */
static int __rpm_callback(int (*cb)(struct device *), struct device *dev)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	int retval;

	if (dev->power.irq_safe)
		spin_unlock(&dev->power.lock);
	else
		spin_unlock_irq(&dev->power.lock);

	retval = cb(dev);

	if (dev->power.irq_safe)
		spin_lock(&dev->power.lock);
	else
		spin_lock_irq(&dev->power.lock);

	return retval;
}

/**
 * rpm_idle - Notify device bus type if the device can be suspended.
 * @dev: Device to notify the bus type about.
 * @rpmflags: Flag bits.
 *
 * Check if the device's runtime PM status allows it to be suspended.  If
 * another idle notification has been started earlier, return immediately.  If
 * the RPM_ASYNC flag is set then queue an idle-notification request; otherwise
 * run the ->runtime_idle() callback directly. If the ->runtime_idle callback
 * doesn't exist or if it returns 0, call rpm_suspend with the RPM_AUTO flag.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int rpm_idle(struct device *dev, int rpmflags)
{
	int (*callback)(struct device *);
	int retval;

	trace_rpm_idle(dev, rpmflags);
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

	if (dev->power.no_callbacks)
		goto out;

	/* Carry out an asynchronous or a synchronous idle notification. */
	if (rpmflags & RPM_ASYNC) {
		dev->power.request = RPM_REQ_IDLE;
		if (!dev->power.request_pending) {
			dev->power.request_pending = true;
			queue_work(pm_wq, &dev->power.work);
		}
		trace_rpm_return_int(dev, _THIS_IP_, 0);
		return 0;
	}

	dev->power.idle_notification = true;

	callback = RPM_GET_CALLBACK(dev, runtime_idle);

	if (callback)
		retval = __rpm_callback(callback, dev);

	dev->power.idle_notification = false;
	wake_up_all(&dev->power.wait_queue);

 out:
	trace_rpm_return_int(dev, _THIS_IP_, retval);
	return retval ? retval : rpm_suspend(dev, rpmflags | RPM_AUTO);
}

/**
 * rpm_callback - Run a given runtime PM callback for a given device.
 * @cb: Runtime PM callback to run.
 * @dev: Device to run the callback for.
 */
static int rpm_callback(int (*cb)(struct device *), struct device *dev)
{
	int retval;

	if (!cb)
		return -ENOSYS;

	if (dev->power.memalloc_noio) {
		unsigned int noio_flag;

		/*
		 * Deadlock might be caused if memory allocation with
		 * GFP_KERNEL happens inside runtime_suspend and
		 * runtime_resume callbacks of one block device's
		 * ancestor or the block device itself. Network
		 * device might be thought as part of iSCSI block
		 * device, so network device and its ancestor should
		 * be marked as memalloc_noio too.
		 */
		noio_flag = memalloc_noio_save();
		retval = __rpm_callback(cb, dev);
		memalloc_noio_restore(noio_flag);
	} else {
		retval = __rpm_callback(cb, dev);
	}

	dev->power.runtime_error = retval;
	return retval != -EACCES ? retval : -EIO;
}

/**
 * rpm_suspend - Carry out runtime suspend of given device.
 * @dev: Device to suspend.
 * @rpmflags: Flag bits.
 *
 * Check if the device's runtime PM status allows it to be suspended.
 * Cancel a pending idle notification, autosuspend or suspend. If
 * another suspend has been started earlier, either return immediately
 * or wait for it to finish, depending on the RPM_NOWAIT and RPM_ASYNC
 * flags. If the RPM_ASYNC flag is set then queue a suspend request;
 * otherwise run the ->runtime_suspend() callback directly. When
 * ->runtime_suspend succeeded, if a deferred resume was requested while
 * the callback was running then carry it out, otherwise send an idle
 * notification for its parent (if the suspend succeeded and both
 * ignore_children of parent->power and irq_safe of dev->power are not set).
 * If ->runtime_suspend failed with -EAGAIN or -EBUSY, and if the RPM_AUTO
 * flag is set and the next autosuspend-delay expiration time is in the
 * future, schedule another autosuspend attempt.
 *
 * This function must be called under dev->power.lock with interrupts disabled.
 */
static int rpm_suspend(struct device *dev, int rpmflags)
	__releases(&dev->power.lock) __acquires(&dev->power.lock)
{
	int (*callback)(struct device *);
	struct device *parent = NULL;
	int retval;

	trace_rpm_suspend(dev, rpmflags);

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

		if (dev->power.irq_safe) {
			spin_unlock(&dev->power.lock);

			cpu_relax();

			spin_lock(&dev->power.lock);
			goto repeat;
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

	callback = RPM_GET_CALLBACK(dev, runtime_suspend);

	dev_pm_enable_wake_irq(dev);
	retval = rpm_callback(callback, dev);
	if (retval)
		goto fail;

 no_callback:
	__update_runtime_status(dev, RPM_SUSPENDED);
	pm_runtime_deactivate_timer(dev);

	if (dev->parent) {
		parent = dev->parent;
		atomic_add_unless(&parent->power.child_count, -1, 0);
	}
	wake_up_all(&dev->power.wait_queue);

	if (dev->power.deferred_resume) {
		dev->power.deferred_resume = false;
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
	trace_rpm_return_int(dev, _THIS_IP_, retval);

	return retval;

 fail:
	dev_pm_disable_wake_irq(dev);
	__update_runtime_status(dev, RPM_ACTIVE);
	dev->power.deferred_resume = false;
	wake_up_all(&dev->power.wait_queue);

	if (retval == -EAGAIN || retval == -EBUSY) {
		dev->power.runtime_error = 0;

		/*
		 * If the callback routine failed an autosuspend, and
		 * if the last_busy time has been updated so that there
		 * is a new autosuspend expiration time, automatically
		 * reschedule another autosuspend.
		 */
		if ((rpmflags & RPM_AUTO) &&
		    pm_runtime_autosuspend_expiration(dev) != 0)
			goto repeat;
	} else {
		pm_runtime_cancel_pending(dev);
	}
	goto out;
}

/**
 * rpm_resume - Carry out runtime resume of given device.
 * @dev: Device to resume.
 * @rpmflags: Flag bits.
 *
 * Check if the device's runtime PM status allows it to be resumed.  Cancel
 * any scheduled or pending requests.  If another resume has been started
 * earlier, either return immediately or wait for it to finish, depending on the
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

	trace_rpm_resume(dev, rpmflags);

 repeat:
	if (dev->power.runtime_error)
		retval = -EINVAL;
	else if (dev->power.disable_depth == 1 && dev->power.is_suspended
	    && dev->power.runtime_status == RPM_ACTIVE)
		retval = 1;
	else if (dev->power.disable_depth > 0)
		retval = -EACCES;
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

		if (dev->power.irq_safe) {
			spin_unlock(&dev->power.lock);

			cpu_relax();

			spin_lock(&dev->power.lock);
			goto repeat;
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
			retval = 1;
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
		 * We can resume if the parent's runtime PM is disabled or it
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

	callback = RPM_GET_CALLBACK(dev, runtime_resume);

	dev_pm_disable_wake_irq(dev);
	retval = rpm_callback(callback, dev);
	if (retval) {
		__update_runtime_status(dev, RPM_SUSPENDED);
		pm_runtime_cancel_pending(dev);
		dev_pm_enable_wake_irq(dev);
	} else {
 no_callback:
		__update_runtime_status(dev, RPM_ACTIVE);
		pm_runtime_mark_last_busy(dev);
		if (parent)
			atomic_inc(&parent->power.child_count);
	}
	wake_up_all(&dev->power.wait_queue);

	if (retval >= 0)
		rpm_idle(dev, RPM_ASYNC);

 out:
	if (parent && !dev->power.irq_safe) {
		spin_unlock_irq(&dev->power.lock);

		pm_runtime_put(parent);

		spin_lock_irq(&dev->power.lock);
	}

	trace_rpm_return_int(dev, _THIS_IP_, retval);

	return retval;
}

/**
 * pm_runtime_work - Universal runtime PM work function.
 * @work: Work structure used for scheduling the execution of this function.
 *
 * Use @work to get the device object the work is to be done for, determine what
 * is to be done and execute the appropriate runtime PM function.
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
 * __pm_runtime_idle - Entry point for runtime idle operations.
 * @dev: Device to send idle notification for.
 * @rpmflags: Flag bits.
 *
 * If the RPM_GET_PUT flag is set, decrement the device's usage count and
 * return immediately if it is larger than zero.  Then carry out an idle
 * notification, either synchronous or asynchronous.
 *
 * This routine may be called in atomic context if the RPM_ASYNC flag is set,
 * or if pm_runtime_irq_safe() has been called.
 */
int __pm_runtime_idle(struct device *dev, int rpmflags)
{
	unsigned long flags;
	int retval;

	might_sleep_if(!(rpmflags & RPM_ASYNC) && !dev->power.irq_safe);

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
 * __pm_runtime_suspend - Entry point for runtime put/suspend operations.
 * @dev: Device to suspend.
 * @rpmflags: Flag bits.
 *
 * If the RPM_GET_PUT flag is set, decrement the device's usage count and
 * return immediately if it is larger than zero.  Then carry out a suspend,
 * either synchronous or asynchronous.
 *
 * This routine may be called in atomic context if the RPM_ASYNC flag is set,
 * or if pm_runtime_irq_safe() has been called.
 */
int __pm_runtime_suspend(struct device *dev, int rpmflags)
{
	unsigned long flags;
	int retval;

	might_sleep_if(!(rpmflags & RPM_ASYNC) && !dev->power.irq_safe);

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
 * __pm_runtime_resume - Entry point for runtime resume operations.
 * @dev: Device to resume.
 * @rpmflags: Flag bits.
 *
 * If the RPM_GET_PUT flag is set, increment the device's usage count.  Then
 * carry out a resume, either synchronous or asynchronous.
 *
 * This routine may be called in atomic context if the RPM_ASYNC flag is set,
 * or if pm_runtime_irq_safe() has been called.
 */
int __pm_runtime_resume(struct device *dev, int rpmflags)
{
	unsigned long flags;
	int retval;

	might_sleep_if(!(rpmflags & RPM_ASYNC) && !dev->power.irq_safe);

	if (rpmflags & RPM_GET_PUT)
		atomic_inc(&dev->power.usage_count);

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = rpm_resume(dev, rpmflags);
	spin_unlock_irqrestore(&dev->power.lock, flags);

	return retval;
}
EXPORT_SYMBOL_GPL(__pm_runtime_resume);

/**
 * pm_runtime_get_if_in_use - Conditionally bump up the device's usage counter.
 * @dev: Device to handle.
 *
 * Return -EINVAL if runtime PM is disabled for the device.
 *
 * If that's not the case and if the device's runtime PM status is RPM_ACTIVE
 * and the runtime PM usage counter is nonzero, increment the counter and
 * return 1.  Otherwise return 0 without changing the counter.
 */
int pm_runtime_get_if_in_use(struct device *dev)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&dev->power.lock, flags);
	retval = dev->power.disable_depth > 0 ? -EINVAL :
		dev->power.runtime_status == RPM_ACTIVE
			&& atomic_inc_not_zero(&dev->power.usage_count);
	spin_unlock_irqrestore(&dev->power.lock, flags);
	return retval;
}
EXPORT_SYMBOL_GPL(pm_runtime_get_if_in_use);

/**
 * __pm_runtime_set_status - Set runtime PM status of a device.
 * @dev: Device to handle.
 * @status: New runtime PM status of the device.
 *
 * If runtime PM of the device is disabled or its power.runtime_error field is
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
		 * not active, has runtime PM enabled and the
		 * 'power.ignore_children' flag unset.
		 */
		if (!parent->power.disable_depth
		    && !parent->power.ignore_children
		    && parent->power.runtime_status != RPM_ACTIVE) {
			dev_err(dev, "runtime PM trying to activate child device %s but parent (%s) is not active\n",
				dev_name(dev),
				dev_name(parent));
			error = -EBUSY;
		} else if (dev->power.runtime_status == RPM_SUSPENDED) {
			atomic_inc(&parent->power.child_count);
		}

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
 * runtime PM operations involving the device in progress to complete.
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
 * from pm_wq and wait for all runtime PM operations involving the device in
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
 * __pm_runtime_disable - Disable runtime PM of a device.
 * @dev: Device to handle.
 * @check_resume: If set, check if there's a resume request for the device.
 *
 * Increment power.disable_depth for the device and if it was zero previously,
 * cancel all pending runtime PM requests for the device and wait for all
 * operations in progress to complete.  The device can be either active or
 * suspended after its runtime PM has been disabled.
 *
 * If @check_resume is set and there's a resume request pending when
 * __pm_runtime_disable() is called and power.disable_depth is zero, the
 * function will wake up the device before disabling its runtime PM.
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
	 * means there probably is some I/O to process and disabling runtime PM
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
 * pm_runtime_enable - Enable runtime PM of a device.
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
 * pm_runtime_forbid - Block runtime PM of a device.
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
 * pm_runtime_allow - Unblock runtime PM of a device.
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
		rpm_idle(dev, RPM_AUTO | RPM_ASYNC);

 out:
	spin_unlock_irq(&dev->power.lock);
}
EXPORT_SYMBOL_GPL(pm_runtime_allow);

/**
 * pm_runtime_no_callbacks - Ignore runtime PM callbacks for a device.
 * @dev: Device to handle.
 *
 * Set the power.no_callbacks flag, which tells the PM core that this
 * device is power-managed through its parent and has no runtime PM
 * callbacks of its own.  The runtime sysfs attributes will be removed.
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
 * and the power.use_autosuspend flag is set, prevent runtime suspends.  If it
 * changes the other way, allow runtime suspends.
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
 * Set the device's power.use_autosuspend flag, and allow or prevent runtime
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
 * pm_runtime_init - Initialize runtime PM fields in given device object.
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
 * pm_runtime_reinit - Re-initialize runtime PM fields in given device object.
 * @dev: Device object to re-initialize.
 */
void pm_runtime_reinit(struct device *dev)
{
	if (!pm_runtime_enabled(dev)) {
		if (dev->power.runtime_status == RPM_ACTIVE)
			pm_runtime_set_suspended(dev);
		if (dev->power.irq_safe) {
			spin_lock_irq(&dev->power.lock);
			dev->power.irq_safe = 0;
			spin_unlock_irq(&dev->power.lock);
			if (dev->parent)
				pm_runtime_put(dev->parent);
		}
	}
}

/**
 * pm_runtime_remove - Prepare for removing a device from device hierarchy.
 * @dev: Device object being removed from device hierarchy.
 */
void pm_runtime_remove(struct device *dev)
{
	__pm_runtime_disable(dev, false);
	pm_runtime_reinit(dev);
}

/**
 * pm_runtime_force_suspend - Force a device into suspend state if needed.
 * @dev: Device to suspend.
 *
 * Disable runtime PM so we safely can check the device's runtime PM status and
 * if it is active, invoke it's .runtime_suspend callback to bring it into
 * suspend state. Keep runtime PM disabled to preserve the state unless we
 * encounter errors.
 *
 * Typically this function may be invoked from a system suspend callback to make
 * sure the device is put into low power state.
 */
int pm_runtime_force_suspend(struct device *dev)
{
	int (*callback)(struct device *);
	int ret = 0;

	pm_runtime_disable(dev);
	if (pm_runtime_status_suspended(dev))
		return 0;

	callback = RPM_GET_CALLBACK(dev, runtime_suspend);

	if (!callback) {
		ret = -ENOSYS;
		goto err;
	}

	ret = callback(dev);
	if (ret)
		goto err;

	pm_runtime_set_suspended(dev);
	return 0;
err:
	pm_runtime_enable(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(pm_runtime_force_suspend);

/**
 * pm_runtime_force_resume - Force a device into resume state.
 * @dev: Device to resume.
 *
 * Prior invoking this function we expect the user to have brought the device
 * into low power state by a call to pm_runtime_force_suspend(). Here we reverse
 * those actions and brings the device into full power. We update the runtime PM
 * status and re-enables runtime PM.
 *
 * Typically this function may be invoked from a system resume callback to make
 * sure the device is put into full power state.
 */
int pm_runtime_force_resume(struct device *dev)
{
	int (*callback)(struct device *);
	int ret = 0;

	callback = RPM_GET_CALLBACK(dev, runtime_resume);

	if (!callback) {
		ret = -ENOSYS;
		goto out;
	}

	if (!pm_runtime_status_suspended(dev))
		goto out;

	ret = pm_runtime_set_active(dev);
	if (ret)
		goto out;

	ret = callback(dev);
	if (ret) {
		pm_runtime_set_suspended(dev);
		goto out;
	}

	pm_runtime_mark_last_busy(dev);
out:
	pm_runtime_enable(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(pm_runtime_force_resume);
