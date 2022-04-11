/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/wait_bit.h>

#include "intel_runtime_pm.h"
#include "intel_wakeref.h"

static void rpm_get(struct intel_wakeref *wf)
{
	wf->wakeref = intel_runtime_pm_get(wf->rpm);
}

static void rpm_put(struct intel_wakeref *wf)
{
	intel_wakeref_t wakeref = fetch_and_zero(&wf->wakeref);

	intel_runtime_pm_put(wf->rpm, wakeref);
	INTEL_WAKEREF_BUG_ON(!wakeref);
}

int __intel_wakeref_get_first(struct intel_wakeref *wf)
{
	/*
	 * Treat get/put as different subclasses, as we may need to run
	 * the put callback from under the shrinker and do not want to
	 * cross-contanimate that callback with any extra work performed
	 * upon acquiring the wakeref.
	 */
	mutex_lock_nested(&wf->mutex, SINGLE_DEPTH_NESTING);
	if (!atomic_read(&wf->count)) {
		int err;

		rpm_get(wf);

		err = wf->ops->get(wf);
		if (unlikely(err)) {
			rpm_put(wf);
			mutex_unlock(&wf->mutex);
			return err;
		}

		smp_mb__before_atomic(); /* release wf->count */
	}
	atomic_inc(&wf->count);
	mutex_unlock(&wf->mutex);

	INTEL_WAKEREF_BUG_ON(atomic_read(&wf->count) <= 0);
	return 0;
}

static void ____intel_wakeref_put_last(struct intel_wakeref *wf)
{
	if (!atomic_dec_and_test(&wf->count))
		goto unlock;

	/* ops->put() must reschedule its own release on error/deferral */
	if (likely(!wf->ops->put(wf))) {
		rpm_put(wf);
		wake_up_var(&wf->wakeref);
	}

unlock:
	mutex_unlock(&wf->mutex);
}

void __intel_wakeref_put_last(struct intel_wakeref *wf)
{
	INTEL_WAKEREF_BUG_ON(work_pending(&wf->work));

	/* Assume we are not in process context and so cannot sleep. */
	if (wf->ops->flags & INTEL_WAKEREF_PUT_ASYNC ||
	    !mutex_trylock(&wf->mutex)) {
		schedule_work(&wf->work);
		return;
	}

	____intel_wakeref_put_last(wf);
}

static void __intel_wakeref_put_work(struct work_struct *wrk)
{
	struct intel_wakeref *wf = container_of(wrk, typeof(*wf), work);

	if (atomic_add_unless(&wf->count, -1, 1))
		return;

	mutex_lock(&wf->mutex);
	____intel_wakeref_put_last(wf);
}

void __intel_wakeref_init(struct intel_wakeref *wf,
			  struct intel_runtime_pm *rpm,
			  const struct intel_wakeref_ops *ops,
			  struct lock_class_key *key)
{
	wf->rpm = rpm;
	wf->ops = ops;

	__mutex_init(&wf->mutex, "wakeref", key);
	atomic_set(&wf->count, 0);
	wf->wakeref = 0;

	INIT_WORK(&wf->work, __intel_wakeref_put_work);
}

int intel_wakeref_wait_for_idle(struct intel_wakeref *wf)
{
	return wait_var_event_killable(&wf->wakeref,
				       !intel_wakeref_is_active(wf));
}

static void wakeref_auto_timeout(struct timer_list *t)
{
	struct intel_wakeref_auto *wf = from_timer(wf, t, timer);
	intel_wakeref_t wakeref;
	unsigned long flags;

	if (!refcount_dec_and_lock_irqsave(&wf->count, &wf->lock, &flags))
		return;

	wakeref = fetch_and_zero(&wf->wakeref);
	spin_unlock_irqrestore(&wf->lock, flags);

	intel_runtime_pm_put(wf->rpm, wakeref);
}

void intel_wakeref_auto_init(struct intel_wakeref_auto *wf,
			     struct intel_runtime_pm *rpm)
{
	spin_lock_init(&wf->lock);
	timer_setup(&wf->timer, wakeref_auto_timeout, 0);
	refcount_set(&wf->count, 0);
	wf->wakeref = 0;
	wf->rpm = rpm;
}

void intel_wakeref_auto(struct intel_wakeref_auto *wf, unsigned long timeout)
{
	unsigned long flags;

	if (!timeout) {
		if (del_timer_sync(&wf->timer))
			wakeref_auto_timeout(&wf->timer);
		return;
	}

	/* Our mission is that we only extend an already active wakeref */
	assert_rpm_wakelock_held(wf->rpm);

	if (!refcount_inc_not_zero(&wf->count)) {
		spin_lock_irqsave(&wf->lock, flags);
		if (!refcount_inc_not_zero(&wf->count)) {
			INTEL_WAKEREF_BUG_ON(wf->wakeref);
			wf->wakeref = intel_runtime_pm_get_if_in_use(wf->rpm);
			refcount_set(&wf->count, 1);
		}
		spin_unlock_irqrestore(&wf->lock, flags);
	}

	/*
	 * If we extend a pending timer, we will only get a single timer
	 * callback and so need to cancel the local inc by running the
	 * elided callback to keep the wf->count balanced.
	 */
	if (mod_timer(&wf->timer, jiffies + timeout))
		wakeref_auto_timeout(&wf->timer);
}

void intel_wakeref_auto_fini(struct intel_wakeref_auto *wf)
{
	intel_wakeref_auto(wf, 0);
	INTEL_WAKEREF_BUG_ON(wf->wakeref);
}
