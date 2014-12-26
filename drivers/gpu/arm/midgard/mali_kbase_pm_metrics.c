/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_pm_metrics.c
 * Metrics for power management
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#if KBASE_PM_EN
/* When VSync is being hit aim for utilisation between 70-90% */
#define KBASE_PM_VSYNC_MIN_UTILISATION          70
#define KBASE_PM_VSYNC_MAX_UTILISATION          90
/* Otherwise aim for 10-40% */
#define KBASE_PM_NO_VSYNC_MIN_UTILISATION       10
#define KBASE_PM_NO_VSYNC_MAX_UTILISATION       40

/* Shift used for kbasep_pm_metrics_data.time_busy/idle - units of (1 << 8) ns
   This gives a maximum period between samples of 2^(32+8)/100 ns = slightly under 11s.
   Exceeding this will cause overflow */
#define KBASE_PM_TIME_SHIFT			8

/* Maximum time between sampling of utilization data, without resetting the
 * counters. */
#define MALI_UTILIZATION_MAX_PERIOD 100000 /* ns = 100ms */

#ifdef CONFIG_MALI_MIDGARD_DVFS
static enum hrtimer_restart dvfs_callback(struct hrtimer *timer)
{
	unsigned long flags;
	enum kbase_pm_dvfs_action action;
	struct kbasep_pm_metrics_data *metrics;

	KBASE_DEBUG_ASSERT(timer != NULL);

	metrics = container_of(timer, struct kbasep_pm_metrics_data, timer);
	action = kbase_pm_get_dvfs_action(metrics->kbdev);

	spin_lock_irqsave(&metrics->lock, flags);

	if (metrics->timer_active)
		hrtimer_start(timer,
					  HR_TIMER_DELAY_MSEC(metrics->kbdev->pm.platform_dvfs_frequency),
					  HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&metrics->lock, flags);

	return HRTIMER_NORESTART;
}
#endif /* CONFIG_MALI_MIDGARD_DVFS */

mali_error kbasep_pm_metrics_init(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	kbdev->pm.metrics.kbdev = kbdev;
	kbdev->pm.metrics.vsync_hit = 0;
	kbdev->pm.metrics.utilisation = 0;
	kbdev->pm.metrics.util_cl_share[0] = 0;
	kbdev->pm.metrics.util_cl_share[1] = 0;
	kbdev->pm.metrics.util_gl_share = 0;

	kbdev->pm.metrics.time_period_start = ktime_get();
	kbdev->pm.metrics.time_busy = 0;
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.prev_busy = 0;
	kbdev->pm.metrics.prev_idle = 0;
	kbdev->pm.metrics.gpu_active = MALI_TRUE;
	kbdev->pm.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.metrics.active_gl_ctx = 0;
	kbdev->pm.metrics.busy_cl[0] = 0;
	kbdev->pm.metrics.busy_cl[1] = 0;
	kbdev->pm.metrics.busy_gl = 0;

	spin_lock_init(&kbdev->pm.metrics.lock);

#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbdev->pm.metrics.timer_active = MALI_TRUE;
	hrtimer_init(&kbdev->pm.metrics.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kbdev->pm.metrics.timer.function = dvfs_callback;

	hrtimer_start(&kbdev->pm.metrics.timer, HR_TIMER_DELAY_MSEC(kbdev->pm.platform_dvfs_frequency), HRTIMER_MODE_REL);
#endif /* CONFIG_MALI_MIDGARD_DVFS */

	kbase_pm_register_vsync_callback(kbdev);

	return MALI_ERROR_NONE;
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_init)

void kbasep_pm_metrics_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_MIDGARD_DVFS
	unsigned long flags;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	kbdev->pm.metrics.timer_active = MALI_FALSE;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	hrtimer_cancel(&kbdev->pm.metrics.timer);
#endif /* CONFIG_MALI_MIDGARD_DVFS */

	kbase_pm_unregister_vsync_callback(kbdev);
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_term)

/*caller needs to hold kbdev->pm.metrics.lock before calling this function*/
void kbasep_pm_record_job_status(struct kbase_device *kbdev)
{
	ktime_t now;
	ktime_t diff;
	u32 ns_time;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	now = ktime_get();
	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	kbdev->pm.metrics.time_busy += ns_time;
	kbdev->pm.metrics.busy_gl += ns_time * kbdev->pm.metrics.active_gl_ctx;
	kbdev->pm.metrics.busy_cl[0] += ns_time * kbdev->pm.metrics.active_cl_ctx[0];
	kbdev->pm.metrics.busy_cl[1] += ns_time * kbdev->pm.metrics.active_cl_ctx[1];
	kbdev->pm.metrics.time_period_start = now;
}

KBASE_EXPORT_TEST_API(kbasep_pm_record_job_status)

void kbasep_pm_record_gpu_idle(struct kbase_device *kbdev)
{
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

	KBASE_DEBUG_ASSERT(kbdev->pm.metrics.gpu_active == MALI_TRUE);

	kbdev->pm.metrics.gpu_active = MALI_FALSE;

	kbasep_pm_record_job_status(kbdev);

	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbasep_pm_record_gpu_idle)

void kbasep_pm_record_gpu_active(struct kbase_device *kbdev)
{
	unsigned long flags;
	ktime_t now;
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

	KBASE_DEBUG_ASSERT(kbdev->pm.metrics.gpu_active == MALI_FALSE);

	kbdev->pm.metrics.gpu_active = MALI_TRUE;

	now = ktime_get();
	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	kbdev->pm.metrics.time_idle += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	kbdev->pm.metrics.time_period_start = now;

	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbasep_pm_record_gpu_active)

void kbase_pm_report_vsync(struct kbase_device *kbdev, int buffer_updated)
{
	unsigned long flags;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	kbdev->pm.metrics.vsync_hit = buffer_updated;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_report_vsync)

/*caller needs to hold kbdev->pm.metrics.lock before calling this function*/
static void kbase_pm_get_dvfs_utilisation_calc(struct kbase_device *kbdev, ktime_t now)
{
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	diff = ktime_sub(now, kbdev->pm.metrics.time_period_start);

	if (kbdev->pm.metrics.gpu_active) {
		u32 ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
		kbdev->pm.metrics.time_busy += ns_time;
		kbdev->pm.metrics.busy_cl[0] += ns_time * kbdev->pm.metrics.active_cl_ctx[0];
		kbdev->pm.metrics.busy_cl[1] += ns_time * kbdev->pm.metrics.active_cl_ctx[1];
		kbdev->pm.metrics.busy_gl += ns_time * kbdev->pm.metrics.active_gl_ctx;
	} else {
		kbdev->pm.metrics.time_idle += (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	}
}

/* Caller needs to hold kbdev->pm.metrics.lock before calling this function. */
static void kbase_pm_reset_dvfs_utilisation_unlocked(struct kbase_device *kbdev, ktime_t now)
{
	/* Store previous value */
	kbdev->pm.metrics.prev_idle = kbdev->pm.metrics.time_idle;
	kbdev->pm.metrics.prev_busy = kbdev->pm.metrics.time_busy;

	/* Reset current values */
	kbdev->pm.metrics.time_period_start = now;
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;
	kbdev->pm.metrics.busy_cl[0] = 0;
	kbdev->pm.metrics.busy_cl[1] = 0;
	kbdev->pm.metrics.busy_gl = 0;
}

void kbase_pm_reset_dvfs_utilisation(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	kbase_pm_reset_dvfs_utilisation_unlocked(kbdev, ktime_get());
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

void kbase_pm_get_dvfs_utilisation(struct kbase_device *kbdev,
		unsigned long *total_out, unsigned long *busy_out)
{
	ktime_t now = ktime_get();
	unsigned long flags, busy, total;

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	kbase_pm_get_dvfs_utilisation_calc(kbdev, now);

	busy = kbdev->pm.metrics.time_busy;
	total = busy + kbdev->pm.metrics.time_idle;

	/* Reset stats if older than MALI_UTILIZATION_MAX_PERIOD (default
	 * 100ms) */
	if (total >= MALI_UTILIZATION_MAX_PERIOD) {
		kbase_pm_reset_dvfs_utilisation_unlocked(kbdev, now);
	} else if (total < (MALI_UTILIZATION_MAX_PERIOD / 2)) {
		total += kbdev->pm.metrics.prev_idle +
				kbdev->pm.metrics.prev_busy;
		busy += kbdev->pm.metrics.prev_busy;
	}

	*total_out = total;
	*busy_out = busy;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);
}

#ifdef CONFIG_MALI_MIDGARD_DVFS

/*caller needs to hold kbdev->pm.metrics.lock before calling this function*/
int kbase_pm_get_dvfs_utilisation_old(struct kbase_device *kbdev, int *util_gl_share, int util_cl_share[2])
{
	int utilisation;
	int busy;
	ktime_t now = ktime_get();

	kbase_pm_get_dvfs_utilisation_calc(kbdev, now);

	if (kbdev->pm.metrics.time_idle + kbdev->pm.metrics.time_busy == 0) {
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

	utilisation = (100 * kbdev->pm.metrics.time_busy) /
			(kbdev->pm.metrics.time_idle +
			 kbdev->pm.metrics.time_busy);

	busy = kbdev->pm.metrics.busy_gl +
		kbdev->pm.metrics.busy_cl[0] +
		kbdev->pm.metrics.busy_cl[1];

	if (busy != 0) {
		if (util_gl_share)
			*util_gl_share =
				(100 * kbdev->pm.metrics.busy_gl) / busy;
		if (util_cl_share) {
			util_cl_share[0] =
				(100 * kbdev->pm.metrics.busy_cl[0]) / busy;
			util_cl_share[1] =
				(100 * kbdev->pm.metrics.busy_cl[1]) / busy;
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
	kbase_pm_reset_dvfs_utilisation_unlocked(kbdev, now);

	return utilisation;
}

enum kbase_pm_dvfs_action kbase_pm_get_dvfs_action(struct kbase_device *kbdev)
{
	unsigned long flags;
	int utilisation, util_gl_share;
	int util_cl_share[2];
	enum kbase_pm_dvfs_action action;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);

	utilisation = kbase_pm_get_dvfs_utilisation_old(kbdev, &util_gl_share, util_cl_share);

	if (utilisation < 0 || util_gl_share < 0 || util_cl_share[0] < 0 || util_cl_share[1] < 0) {
		action = KBASE_PM_DVFS_NOP;
		utilisation = 0;
		util_gl_share = 0;
		util_cl_share[0] = 0;
		util_cl_share[1] = 0;
		goto out;
	}

	if (kbdev->pm.metrics.vsync_hit) {
		/* VSync is being met */
		if (utilisation < KBASE_PM_VSYNC_MIN_UTILISATION)
			action = KBASE_PM_DVFS_CLOCK_DOWN;
		else if (utilisation > KBASE_PM_VSYNC_MAX_UTILISATION)
			action = KBASE_PM_DVFS_CLOCK_UP;
		else
			action = KBASE_PM_DVFS_NOP;
	} else {
		/* VSync is being missed */
		if (utilisation < KBASE_PM_NO_VSYNC_MIN_UTILISATION)
			action = KBASE_PM_DVFS_CLOCK_DOWN;
		else if (utilisation > KBASE_PM_NO_VSYNC_MAX_UTILISATION)
			action = KBASE_PM_DVFS_CLOCK_UP;
		else
			action = KBASE_PM_DVFS_NOP;
	}

	kbdev->pm.metrics.utilisation = utilisation;
	kbdev->pm.metrics.util_cl_share[0] = util_cl_share[0];
	kbdev->pm.metrics.util_cl_share[1] = util_cl_share[1];
	kbdev->pm.metrics.util_gl_share = util_gl_share;
out:
#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_event(kbdev, utilisation, util_gl_share, util_cl_share);
#endif				/*CONFIG_MALI_MIDGARD_DVFS */
	kbdev->pm.metrics.time_idle = 0;
	kbdev->pm.metrics.time_busy = 0;
	kbdev->pm.metrics.busy_cl[0] = 0;
	kbdev->pm.metrics.busy_cl[1] = 0;
	kbdev->pm.metrics.busy_gl = 0;
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	return action;
}
KBASE_EXPORT_TEST_API(kbase_pm_get_dvfs_action)

mali_bool kbase_pm_metrics_is_active(struct kbase_device *kbdev)
{
	mali_bool isactive;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.metrics.lock, flags);
	isactive = (kbdev->pm.metrics.timer_active == MALI_TRUE);
	spin_unlock_irqrestore(&kbdev->pm.metrics.lock, flags);

	return isactive;
}
KBASE_EXPORT_TEST_API(kbase_pm_metrics_is_active)

#endif /* CONFIG_MALI_MIDGARD_DVFS */

#endif  /* KBASE_PM_EN */
