// SPDX-License-Identifier: GPL-2.0

#include <linux/blk-pm.h>
#include <linux/blkdev.h>
#include <linux/pm_runtime.h>
#include "blk-mq.h"

/**
 * blk_pm_runtime_init - Block layer runtime PM initialization routine
 * @q: the queue of the device
 * @dev: the device the queue belongs to
 *
 * Description:
 *    Initialize runtime-PM-related fields for @q and start auto suspend for
 *    @dev. Drivers that want to take advantage of request-based runtime PM
 *    should call this function after @dev has been initialized, and its
 *    request queue @q has been allocated, and runtime PM for it can not happen
 *    yet(either due to disabled/forbidden or its usage_count > 0). In most
 *    cases, driver should call this function before any I/O has taken place.
 *
 *    This function takes care of setting up using auto suspend for the device,
 *    the autosuspend delay is set to -1 to make runtime suspend impossible
 *    until an updated value is either set by user or by driver. Drivers do
 *    not need to touch other autosuspend settings.
 *
 *    The block layer runtime PM is request based, so only works for drivers
 *    that use request as their IO unit instead of those directly use bio's.
 */
void blk_pm_runtime_init(struct request_queue *q, struct device *dev)
{
	q->dev = dev;
	q->rpm_status = RPM_ACTIVE;
	pm_runtime_set_autosuspend_delay(q->dev, -1);
	pm_runtime_use_autosuspend(q->dev);
}
EXPORT_SYMBOL(blk_pm_runtime_init);

/**
 * blk_pre_runtime_suspend - Pre runtime suspend check
 * @q: the queue of the device
 *
 * Description:
 *    This function will check if runtime suspend is allowed for the device
 *    by examining if there are any requests pending in the queue. If there
 *    are requests pending, the device can not be runtime suspended; otherwise,
 *    the queue's status will be updated to SUSPENDING and the driver can
 *    proceed to suspend the device.
 *
 *    For the not allowed case, we mark last busy for the device so that
 *    runtime PM core will try to autosuspend it some time later.
 *
 *    This function should be called near the start of the device's
 *    runtime_suspend callback.
 *
 * Return:
 *    0		- OK to runtime suspend the device
 *    -EBUSY	- Device should not be runtime suspended
 */
int blk_pre_runtime_suspend(struct request_queue *q)
{
	int ret = 0;

	if (!q->dev)
		return ret;

	WARN_ON_ONCE(q->rpm_status != RPM_ACTIVE);

	spin_lock_irq(&q->queue_lock);
	q->rpm_status = RPM_SUSPENDING;
	spin_unlock_irq(&q->queue_lock);

	/*
	 * Increase the pm_only counter before checking whether any
	 * non-PM blk_queue_enter() calls are in progress to avoid that any
	 * new non-PM blk_queue_enter() calls succeed before the pm_only
	 * counter is decreased again.
	 */
	blk_set_pm_only(q);
	ret = -EBUSY;
	/* Switch q_usage_counter from per-cpu to atomic mode. */
	blk_freeze_queue_start(q);
	/*
	 * Wait until atomic mode has been reached. Since that
	 * involves calling call_rcu(), it is guaranteed that later
	 * blk_queue_enter() calls see the pm-only state. See also
	 * http://lwn.net/Articles/573497/.
	 */
	percpu_ref_switch_to_atomic_sync(&q->q_usage_counter);
	if (percpu_ref_is_zero(&q->q_usage_counter))
		ret = 0;
	/* Switch q_usage_counter back to per-cpu mode. */
	blk_mq_unfreeze_queue(q);

	if (ret < 0) {
		spin_lock_irq(&q->queue_lock);
		q->rpm_status = RPM_ACTIVE;
		pm_runtime_mark_last_busy(q->dev);
		spin_unlock_irq(&q->queue_lock);

		blk_clear_pm_only(q);
	}

	return ret;
}
EXPORT_SYMBOL(blk_pre_runtime_suspend);

/**
 * blk_post_runtime_suspend - Post runtime suspend processing
 * @q: the queue of the device
 * @err: return value of the device's runtime_suspend function
 *
 * Description:
 *    Update the queue's runtime status according to the return value of the
 *    device's runtime suspend function and mark last busy for the device so
 *    that PM core will try to auto suspend the device at a later time.
 *
 *    This function should be called near the end of the device's
 *    runtime_suspend callback.
 */
void blk_post_runtime_suspend(struct request_queue *q, int err)
{
	if (!q->dev)
		return;

	spin_lock_irq(&q->queue_lock);
	if (!err) {
		q->rpm_status = RPM_SUSPENDED;
	} else {
		q->rpm_status = RPM_ACTIVE;
		pm_runtime_mark_last_busy(q->dev);
	}
	spin_unlock_irq(&q->queue_lock);

	if (err)
		blk_clear_pm_only(q);
}
EXPORT_SYMBOL(blk_post_runtime_suspend);

/**
 * blk_pre_runtime_resume - Pre runtime resume processing
 * @q: the queue of the device
 *
 * Description:
 *    Update the queue's runtime status to RESUMING in preparation for the
 *    runtime resume of the device.
 *
 *    This function should be called near the start of the device's
 *    runtime_resume callback.
 */
void blk_pre_runtime_resume(struct request_queue *q)
{
	if (!q->dev)
		return;

	spin_lock_irq(&q->queue_lock);
	q->rpm_status = RPM_RESUMING;
	spin_unlock_irq(&q->queue_lock);
}
EXPORT_SYMBOL(blk_pre_runtime_resume);

/**
 * blk_post_runtime_resume - Post runtime resume processing
 * @q: the queue of the device
 *
 * Description:
 *    Restart the queue of a runtime suspended device. It does this regardless
 *    of whether the device's runtime-resume succeeded; even if it failed the
 *    driver or error handler will need to communicate with the device.
 *
 *    This function should be called near the end of the device's
 *    runtime_resume callback to correct queue runtime PM status and re-enable
 *    peeking requests from the queue.
 */
void blk_post_runtime_resume(struct request_queue *q)
{
	int old_status;

	if (!q->dev)
		return;

	spin_lock_irq(&q->queue_lock);
	old_status = q->rpm_status;
	q->rpm_status = RPM_ACTIVE;
	pm_runtime_mark_last_busy(q->dev);
	pm_request_autosuspend(q->dev);
	spin_unlock_irq(&q->queue_lock);

	if (old_status != RPM_ACTIVE)
		blk_clear_pm_only(q);
}
EXPORT_SYMBOL(blk_post_runtime_resume);
