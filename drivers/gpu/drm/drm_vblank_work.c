// SPDX-License-Identifier: MIT

#include <uapi/linux/sched/types.h>

#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_work.h>
#include <drm/drm_crtc.h>

#include "drm_internal.h"

/**
 * DOC: vblank works
 *
 * Many DRM drivers need to program hardware in a time-sensitive manner, many
 * times with a deadline of starting and finishing within a certain region of
 * the scanout. Most of the time the safest way to accomplish this is to
 * simply do said time-sensitive programming in the driver's IRQ handler,
 * which allows drivers to avoid being preempted during these critical
 * regions. Or even better, the hardware may even handle applying such
 * time-critical programming independently of the CPU.
 *
 * While there's a decent amount of hardware that's designed so that the CPU
 * doesn't need to be concerned with extremely time-sensitive programming,
 * there's a few situations where it can't be helped. Some unforgiving
 * hardware may require that certain time-sensitive programming be handled
 * completely by the CPU, and said programming may even take too long to
 * handle in an IRQ handler. Another such situation would be where the driver
 * needs to perform a task that needs to complete within a specific scanout
 * period, but might possibly block and thus cannot be handled in an IRQ
 * context. Both of these situations can't be solved perfectly in Linux since
 * we're not a realtime kernel, and thus the scheduler may cause us to miss
 * our deadline if it decides to preempt us. But for some drivers, it's good
 * enough if we can lower our chance of being preempted to an absolute
 * minimum.
 *
 * This is where &drm_vblank_work comes in. &drm_vblank_work provides a simple
 * generic delayed work implementation which delays work execution until a
 * particular vblank has passed, and then executes the work at realtime
 * priority. This provides the best possible chance at performing
 * time-sensitive hardware programming on time, even when the system is under
 * heavy load. &drm_vblank_work also supports rescheduling, so that self
 * re-arming work items can be easily implemented.
 */

void drm_handle_vblank_works(struct drm_vblank_crtc *vblank)
{
	struct drm_vblank_work *work, *next;
	u64 count = atomic64_read(&vblank->count);
	bool wake = false;

	assert_spin_locked(&vblank->dev->event_lock);

	list_for_each_entry_safe(work, next, &vblank->pending_work, node) {
		if (!drm_vblank_passed(count, work->count))
			continue;

		list_del_init(&work->node);
		drm_vblank_put(vblank->dev, vblank->pipe);
		kthread_queue_work(vblank->worker, &work->base);
		wake = true;
	}
	if (wake)
		wake_up_all(&vblank->work_wait_queue);
}

/* Handle cancelling any pending vblank work items and drop respective vblank
 * references in response to vblank interrupts being disabled.
 */
void drm_vblank_cancel_pending_works(struct drm_vblank_crtc *vblank)
{
	struct drm_vblank_work *work, *next;

	assert_spin_locked(&vblank->dev->event_lock);

	drm_WARN_ONCE(vblank->dev, !list_empty(&vblank->pending_work),
		      "Cancelling pending vblank works!\n");

	list_for_each_entry_safe(work, next, &vblank->pending_work, node) {
		list_del_init(&work->node);
		drm_vblank_put(vblank->dev, vblank->pipe);
	}

	wake_up_all(&vblank->work_wait_queue);
}

/**
 * drm_vblank_work_schedule - schedule a vblank work
 * @work: vblank work to schedule
 * @count: target vblank count
 * @nextonmiss: defer until the next vblank if target vblank was missed
 *
 * Schedule @work for execution once the crtc vblank count reaches @count.
 *
 * If the crtc vblank count has already reached @count and @nextonmiss is
 * %false the work starts to execute immediately.
 *
 * If the crtc vblank count has already reached @count and @nextonmiss is
 * %true the work is deferred until the next vblank (as if @count has been
 * specified as crtc vblank count + 1).
 *
 * If @work is already scheduled, this function will reschedule said work
 * using the new @count. This can be used for self-rearming work items.
 *
 * Returns:
 * %1 if @work was successfully (re)scheduled, %0 if it was either already
 * scheduled or cancelled, or a negative error code on failure.
 */
int drm_vblank_work_schedule(struct drm_vblank_work *work,
			     u64 count, bool nextonmiss)
{
	struct drm_vblank_crtc *vblank = work->vblank;
	struct drm_device *dev = vblank->dev;
	u64 cur_vbl;
	unsigned long irqflags;
	bool passed, inmodeset, rescheduling = false, wake = false;
	int ret = 0;

	spin_lock_irqsave(&dev->event_lock, irqflags);
	if (work->cancelling)
		goto out;

	spin_lock(&dev->vbl_lock);
	inmodeset = vblank->inmodeset;
	spin_unlock(&dev->vbl_lock);
	if (inmodeset)
		goto out;

	if (list_empty(&work->node)) {
		ret = drm_vblank_get(dev, vblank->pipe);
		if (ret < 0)
			goto out;
	} else if (work->count == count) {
		/* Already scheduled w/ same vbl count */
		goto out;
	} else {
		rescheduling = true;
	}

	work->count = count;
	cur_vbl = drm_vblank_count(dev, vblank->pipe);
	passed = drm_vblank_passed(cur_vbl, count);
	if (passed)
		drm_dbg_core(dev,
			     "crtc %d vblank %llu already passed (current %llu)\n",
			     vblank->pipe, count, cur_vbl);

	if (!nextonmiss && passed) {
		drm_vblank_put(dev, vblank->pipe);
		ret = kthread_queue_work(vblank->worker, &work->base);

		if (rescheduling) {
			list_del_init(&work->node);
			wake = true;
		}
	} else {
		if (!rescheduling)
			list_add_tail(&work->node, &vblank->pending_work);
		ret = true;
	}

out:
	spin_unlock_irqrestore(&dev->event_lock, irqflags);
	if (wake)
		wake_up_all(&vblank->work_wait_queue);
	return ret;
}
EXPORT_SYMBOL(drm_vblank_work_schedule);

/**
 * drm_vblank_work_cancel_sync - cancel a vblank work and wait for it to
 * finish executing
 * @work: vblank work to cancel
 *
 * Cancel an already scheduled vblank work and wait for its
 * execution to finish.
 *
 * On return, @work is guaranteed to no longer be scheduled or running, even
 * if it's self-arming.
 *
 * Returns:
 * %True if the work was cancelled before it started to execute, %false
 * otherwise.
 */
bool drm_vblank_work_cancel_sync(struct drm_vblank_work *work)
{
	struct drm_vblank_crtc *vblank = work->vblank;
	struct drm_device *dev = vblank->dev;
	bool ret = false;

	spin_lock_irq(&dev->event_lock);
	if (!list_empty(&work->node)) {
		list_del_init(&work->node);
		drm_vblank_put(vblank->dev, vblank->pipe);
		ret = true;
	}

	work->cancelling++;
	spin_unlock_irq(&dev->event_lock);

	wake_up_all(&vblank->work_wait_queue);

	if (kthread_cancel_work_sync(&work->base))
		ret = true;

	spin_lock_irq(&dev->event_lock);
	work->cancelling--;
	spin_unlock_irq(&dev->event_lock);

	return ret;
}
EXPORT_SYMBOL(drm_vblank_work_cancel_sync);

/**
 * drm_vblank_work_flush - wait for a scheduled vblank work to finish
 * executing
 * @work: vblank work to flush
 *
 * Wait until @work has finished executing once.
 */
void drm_vblank_work_flush(struct drm_vblank_work *work)
{
	struct drm_vblank_crtc *vblank = work->vblank;
	struct drm_device *dev = vblank->dev;

	spin_lock_irq(&dev->event_lock);
	wait_event_lock_irq(vblank->work_wait_queue, list_empty(&work->node),
			    dev->event_lock);
	spin_unlock_irq(&dev->event_lock);

	kthread_flush_work(&work->base);
}
EXPORT_SYMBOL(drm_vblank_work_flush);

/**
 * drm_vblank_work_flush_all - flush all currently pending vblank work on crtc.
 * @crtc: crtc for which vblank work to flush
 *
 * Wait until all currently queued vblank work on @crtc
 * has finished executing once.
 */
void drm_vblank_work_flush_all(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_vblank_crtc *vblank = &dev->vblank[drm_crtc_index(crtc)];

	spin_lock_irq(&dev->event_lock);
	wait_event_lock_irq(vblank->work_wait_queue,
			    list_empty(&vblank->pending_work),
			    dev->event_lock);
	spin_unlock_irq(&dev->event_lock);

	kthread_flush_worker(vblank->worker);
}
EXPORT_SYMBOL(drm_vblank_work_flush_all);

/**
 * drm_vblank_work_init - initialize a vblank work item
 * @work: vblank work item
 * @crtc: CRTC whose vblank will trigger the work execution
 * @func: work function to be executed
 *
 * Initialize a vblank work item for a specific crtc.
 */
void drm_vblank_work_init(struct drm_vblank_work *work, struct drm_crtc *crtc,
			  void (*func)(struct kthread_work *work))
{
	kthread_init_work(&work->base, func);
	INIT_LIST_HEAD(&work->node);
	work->vblank = drm_crtc_vblank_crtc(crtc);
}
EXPORT_SYMBOL(drm_vblank_work_init);

int drm_vblank_worker_init(struct drm_vblank_crtc *vblank)
{
	struct kthread_worker *worker;

	INIT_LIST_HEAD(&vblank->pending_work);
	init_waitqueue_head(&vblank->work_wait_queue);
	worker = kthread_run_worker(0, "card%d-crtc%d",
				       vblank->dev->primary->index,
				       vblank->pipe);
	if (IS_ERR(worker))
		return PTR_ERR(worker);

	vblank->worker = worker;

	sched_set_fifo(worker->task);
	return 0;
}
