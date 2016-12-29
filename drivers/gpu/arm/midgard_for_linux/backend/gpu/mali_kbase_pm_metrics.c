/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/*
 * Metrics for power management
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_jm_rb.h>

/* When VSync is being hit aim for utilisation between 70-90% */
#define KBASE_PM_VSYNC_MIN_UTILISATION          70
#define KBASE_PM_VSYNC_MAX_UTILISATION          90
/* Otherwise aim for 10-40% */
#define KBASE_PM_NO_VSYNC_MIN_UTILISATION       10
#define KBASE_PM_NO_VSYNC_MAX_UTILISATION       40

/* Shift used for kbasep_pm_metrics_data.time_busy/idle - units of (1 << 8) ns
 * This gives a maximum period between samples of 2^(32+8)/100 ns = slightly
 * under 11s. Exceeding this will cause overflow */
#define KBASE_PM_TIME_SHIFT			8

/* Maximum time between sampling of utilization data, without resetting the
 * counters. */
#define MALI_UTILIZATION_MAX_PERIOD 100000 /* ns = 100ms */

#ifdef CONFIG_MALI_MIDGARD_DVFS
static enum hrtimer_restart dvfs_callback(struct hrtimer *timer)
{
	unsigned long flags;
	struct kbasep_pm_metrics_data *metrics;

	KBASE_DEBUG_ASSERT(timer != NULL);

	metrics = container_of(timer, struct kbasep_pm_metrics_data, timer);
	kbase_pm_get_dvfs_action(metrics->kbdev);

	spin_lock_irqsave(&metrics->lock, flags);

	if (metrics->timer_active)
		hrtimer_start(timer,
			HR_TIMER_DELAY_MSEC(metrics->kbdev->pm.dvfs_period),
			HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&metrics->lock, flags);

	return HRTIMER_NORESTART;
}
#endif /* CONFIG_MALI_MIDGARD_DVFS */

int kbasep_pm_metrics_init(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	kbdev->pm.backend.metrics.kbdev = kbdev;

	kbdev->pm.backend.metrics.time_period_start = ktime_get();
	kbdev->pm.backend.metrics.time_busy = 0;
	kbdev->pm.backend.metrics.time_idle = 0;
	kbdev->pm.backend.metrics.prev_busy = 0;
	kbdev->pm.backend.metrics.prev_idle = 0;
	kbdev->pm.backend.metrics.gpu_active = false;
	kbdev->pm.backend.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[1] = 0;
	kbdev->pm.backend.metrics.busy_cl[0] = 0;
	kbdev->pm.backend.metrics.busy_cl[1] = 0;
	kbdev->pm.backend.metrics.busy_gl = 0;

	spin_lock_init(&kbdev->pm.backend.metrics.lock);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbdev->pm.backend.metrics.timer_active = true;
	hrtimer_init(&kbdev->pm.backend.metrics.timer, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	kbdev->pm.backend.metrics.timer.function = dvfs_callback;

	hrtimer_start(&kbdev->pm.backend.metrics.timer,
			HR_TIMER_DELAY_MSEC(kbdev->pm.dvfs_period),
			HRTIMER_MODE_REL);
#endif /* CONFIG_MALI_MIDGARD_DVFS */

	return 0;
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_init);

void kbasep_pm_metrics_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_MIDGARD_DVFS
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	kbdev->pm.backend.metrics.timer_active = false;
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);

	hrtimer_cancel(&kbdev->pm.backend.metrics.timer);
#endif /* CONFIG_MALI_MIDGARD_DVFS */
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_term);

/* caller needs to hold kbdev->pm.backend.metrics.lock before calling this
 * function
 */
static void kbase_pm_get_dvfs_utilisation_calc(struct kbase_device *kbdev,
								ktime_t now)
{
	ktime_t diff;

	lockdep_assert_held(&kbdev->pm.backend.metrics.lock);

	diff = ktime_sub(now, kbdev->pm.backend.metrics.time_period_start);
	if (ktime_to_ns(diff) < 0)
		return;

	if (kbdev->pm.backend.metrics.gpu_active) {
		u32 ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);

		kbdev->pm.backend.metrics.time_busy += ns_time;
		if (kbdev->pm.backend.metrics.active_cl_ctx[0])
			kbdev->pm.backend.metrics.busy_cl[0] += ns_time;
		if (kbdev->pm.backend.metrics.active_cl_ctx[1])
			kbdev->pm.backend.metrics.busy_cl[1] += ns_time;
		if (kbdev->pm.backend.metrics.active_gl_ctx[0])
			kbdev->pm.backend.metrics.busy_gl += ns_time;
		if (kbdev->pm.backend.metrics.active_gl_ctx[1])
			kbdev->pm.backend.metrics.busy_gl += ns_time;
	} else {
		kbdev->pm.backend.metrics.time_idle += (u32) (ktime_to_ns(diff)
							>> KBASE_PM_TIME_SHIFT);
	}

	kbdev->pm.backend.metrics.time_period_start = now;
}

#if defined(CONFIG_PM_DEVFREQ) || defined(CONFIG_MALI_MIDGARD_DVFS)
/* Caller needs to hold kbdev->pm.backend.metrics.lock before calling this
 * function.
 */
static void kbase_pm_reset_dvfs_utilisation_unlocked(struct kbase_device *kbdev,
								ktime_t now)
{
	/* Store previous value */
	kbdev->pm.backend.metrics.prev_idle =
					kbdev->pm.backend.metrics.time_idle;
	kbdev->pm.backend.metrics.prev_busy =
					kbdev->pm.backend.metrics.time_busy;

	/* Reset current values */
	kbdev->pm.backend.metrics.time_period_start = now;
	kbdev->pm.backend.metrics.time_idle = 0;
	kbdev->pm.backend.metrics.time_busy = 0;
	kbdev->pm.backend.metrics.busy_cl[0] = 0;
	kbdev->pm.backend.metrics.busy_cl[1] = 0;
	kbdev->pm.backend.metrics.busy_gl = 0;
}

void kbase_pm_reset_dvfs_utilisation(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	kbase_pm_reset_dvfs_utilisation_unlocked(kbdev, ktime_get());
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}

void kbase_pm_get_dvfs_utilisation(struct kbase_device *kbdev,
		unsigned long *total_out, unsigned long *busy_out)
{
	ktime_t now = ktime_get();
	unsigned long flags, busy, total;

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	kbase_pm_get_dvfs_utilisation_calc(kbdev, now);

	busy = kbdev->pm.backend.metrics.time_busy;
	total = busy + kbdev->pm.backend.metrics.time_idle;

	/* Reset stats if older than MALI_UTILIZATION_MAX_PERIOD (default
	 * 100ms) */
	if (total >= MALI_UTILIZATION_MAX_PERIOD) {
		kbase_pm_reset_dvfs_utilisation_unlocked(kbdev, now);
	} else if (total < (MALI_UTILIZATION_MAX_PERIOD / 2)) {
		total += kbdev->pm.backend.metrics.prev_idle +
				kbdev->pm.backend.metrics.prev_busy;
		busy += kbdev->pm.backend.metrics.prev_busy;
	}

	*total_out = total;
	*busy_out = busy;
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}
#endif

#ifdef CONFIG_MALI_MIDGARD_DVFS

/* caller needs to hold kbdev->pm.backend.metrics.lock before calling this
 * function
 */
int kbase_pm_get_dvfs_utilisation_old(struct kbase_device *kbdev,
					int *util_gl_share,
					int util_cl_share[2],
					ktime_t now)
{
	int utilisation;
	int busy;

	kbase_pm_get_dvfs_utilisation_calc(kbdev, now);

	if (kbdev->pm.backend.metrics.time_idle +
				kbdev->pm.backend.metrics.time_busy == 0) {
		/* No data - so we return NOP */
		utilisation = -1;
		if (util_gl_share)
			*util_gl_share = -1;
		if (util_cl_share) {
			util_cl_share[0] = -1;
			util_cl_share[1] = -1;
		}
		goto out;
	}

	utilisation = (100 * kbdev->pm.backend.metrics.time_busy) /
			(kbdev->pm.backend.metrics.time_idle +
			 kbdev->pm.backend.metrics.time_busy);

	busy = kbdev->pm.backend.metrics.busy_gl +
		kbdev->pm.backend.metrics.busy_cl[0] +
		kbdev->pm.backend.metrics.busy_cl[1];

	if (busy != 0) {
		if (util_gl_share)
			*util_gl_share =
				(100 * kbdev->pm.backend.metrics.busy_gl) /
									busy;
		if (util_cl_share) {
			util_cl_share[0] =
				(100 * kbdev->pm.backend.metrics.busy_cl[0]) /
									busy;
			util_cl_share[1] =
				(100 * kbdev->pm.backend.metrics.busy_cl[1]) /
									busy;
		}
	} else {
		if (util_gl_share)
			*util_gl_share = -1;
		if (util_cl_share) {
			util_cl_share[0] = -1;
			util_cl_share[1] = -1;
		}
	}

out:
	return utilisation;
}

void kbase_pm_get_dvfs_action(struct kbase_device *kbdev)
{
	unsigned long flags;
	int utilisation, util_gl_share;
	int util_cl_share[2];
	ktime_t now;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);

	now = ktime_get();

	utilisation = kbase_pm_get_dvfs_utilisation_old(kbdev, &util_gl_share,
			util_cl_share, now);

	if (utilisation < 0 || util_gl_share < 0 || util_cl_share[0] < 0 ||
							util_cl_share[1] < 0) {
		utilisation = 0;
		util_gl_share = 0;
		util_cl_share[0] = 0;
		util_cl_share[1] = 0;
		goto out;
	}

out:
#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_event(kbdev, utilisation, util_gl_share,
								util_cl_share);
#endif				/*CONFIG_MALI_MIDGARD_DVFS */

	kbase_pm_reset_dvfs_utilisation_unlocked(kbdev, now);

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}

bool kbase_pm_metrics_is_active(struct kbase_device *kbdev)
{
	bool isactive;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	isactive = kbdev->pm.backend.metrics.timer_active;
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);

	return isactive;
}
KBASE_EXPORT_TEST_API(kbase_pm_metrics_is_active);

#endif /* CONFIG_MALI_MIDGARD_DVFS */

/**
 * kbase_pm_metrics_active_calc - Update PM active counts based on currently
 *                                running atoms
 * @kbdev: Device pointer
 *
 * The caller must hold kbdev->pm.backend.metrics.lock
 */
static void kbase_pm_metrics_active_calc(struct kbase_device *kbdev)
{
	int js;

	lockdep_assert_held(&kbdev->pm.backend.metrics.lock);

	kbdev->pm.backend.metrics.active_gl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[1] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.backend.metrics.gpu_active = false;

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, 0);

		/* Head atom may have just completed, so if it isn't running
		 * then try the next atom */
		if (katom && katom->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED)
			katom = kbase_gpu_inspect(kbdev, js, 1);

		if (katom && katom->gpu_rb_state ==
				KBASE_ATOM_GPU_RB_SUBMITTED) {
			if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
				int device_nr = (katom->core_req &
					BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)
						? katom->device_nr : 0;
				WARN_ON(device_nr >= 2);
				kbdev->pm.backend.metrics.active_cl_ctx[
						device_nr] = 1;
			} else {
				/* Slot 2 should not be running non-compute
				 * atoms */
				WARN_ON(js >= 2);
				kbdev->pm.backend.metrics.active_gl_ctx[js] = 1;
			}
			kbdev->pm.backend.metrics.gpu_active = true;
		}
	}
}

/* called when job is submitted to or removed from a GPU slot */
void kbase_pm_metrics_update(struct kbase_device *kbdev, ktime_t *timestamp)
{
	unsigned long flags;
	ktime_t now;

	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);

	if (!timestamp) {
		now = ktime_get();
		timestamp = &now;
	}

	/* Track how long CL and/or GL jobs have been busy for */
	kbase_pm_get_dvfs_utilisation_calc(kbdev, *timestamp);

	kbase_pm_metrics_active_calc(kbdev);

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}
