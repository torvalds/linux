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





/**
 * @file mali_kbase_pm_metrics.c
 * Metrics for power management
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

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
	enum kbase_pm_dvfs_action action;
	struct kbasep_pm_metrics_data *metrics;

	KBASE_DEBUG_ASSERT(timer != NULL);

	metrics = container_of(timer, struct kbasep_pm_metrics_data, timer);
	action = kbase_pm_get_dvfs_action(metrics->kbdev);

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
	kbdev->pm.backend.metrics.vsync_hit = 0;
	kbdev->pm.backend.metrics.utilisation = 0;
	kbdev->pm.backend.metrics.util_cl_share[0] = 0;
	kbdev->pm.backend.metrics.util_cl_share[1] = 0;
	kbdev->pm.backend.metrics.util_gl_share = 0;

	kbdev->pm.backend.metrics.time_period_start = ktime_get();
	kbdev->pm.backend.metrics.time_busy = 0;
	kbdev->pm.backend.metrics.time_idle = 0;
	kbdev->pm.backend.metrics.prev_busy = 0;
	kbdev->pm.backend.metrics.prev_idle = 0;
	kbdev->pm.backend.metrics.gpu_active = false;
	kbdev->pm.backend.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx = 0;
	kbdev->pm.backend.metrics.busy_cl[0] = 0;
	kbdev->pm.backend.metrics.busy_cl[1] = 0;
	kbdev->pm.backend.metrics.busy_gl = 0;
	kbdev->pm.backend.metrics.nr_in_slots = 0;

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

	kbase_pm_register_vsync_callback(kbdev);

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

	kbase_pm_unregister_vsync_callback(kbdev);
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_term);

/**
 * kbasep_pm_record_gpu_active - update metrics tracking GPU active time
 *
 * This function updates the time the GPU was busy executing jobs in
 * general and specifically for CL and GL jobs. Call this function when
 * a job is submitted or removed from the GPU (job issue) slots.
 *
 * The busy time recorded is the time passed since the last time the
 * busy/idle metrics were updated (e.g. by this function,
 * kbasep_pm_record_gpu_idle or others).
 *
 * Note that the time we record towards CL and GL jobs accounts for
 * the total number of CL and GL jobs active at that time. If 20ms
 * has passed and 3 GL jobs were active, we account 3*20 ms towards
 * the GL busy time. The number of CL/GL jobs active is tracked by
 * kbase_pm_metrics_run_atom() / kbase_pm_metrics_release_atom().
 *
 * The kbdev->pm.backend.metrics.lock needs to be held when calling
 * this function.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
static void kbasep_pm_record_gpu_active(struct kbase_device *kbdev)
{
	ktime_t now = ktime_get();
	ktime_t diff;
	u32 ns_time;

	lockdep_assert_held(&kbdev->pm.backend.metrics.lock);

	/* Record active time */
	diff = ktime_sub(now, kbdev->pm.backend.metrics.time_period_start);
	ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	kbdev->pm.backend.metrics.time_busy += ns_time;
	kbdev->pm.backend.metrics.busy_gl += ns_time *
				kbdev->pm.backend.metrics.active_gl_ctx;
	kbdev->pm.backend.metrics.busy_cl[0] += ns_time *
				kbdev->pm.backend.metrics.active_cl_ctx[0];
	kbdev->pm.backend.metrics.busy_cl[1] += ns_time *
				kbdev->pm.backend.metrics.active_cl_ctx[1];
	/* Reset time period */
	kbdev->pm.backend.metrics.time_period_start = now;
}

/**
 * kbasep_pm_record_gpu_idle - update metrics tracking GPU idle time
 *
 * This function updates the time the GPU was idle (not executing any
 * jobs) based on the time passed when kbasep_pm_record_gpu_active()
 * was called last to record the last job on the GPU finishing.
 *
 * Call this function when no jobs are in the job slots of the GPU and
 * a job is about to be submitted to a job slot.
 *
 * The kbdev->pm.backend.metrics.lock needs to be held when calling
 * this function.
 *
 * @param kbdev The kbase device structure for the device (must be a valid
 *              pointer)
 */
static void kbasep_pm_record_gpu_idle(struct kbase_device *kbdev)
{
	ktime_t now = ktime_get();
	ktime_t diff;
	u32 ns_time;

	lockdep_assert_held(&kbdev->pm.backend.metrics.lock);

	/* Record idle time */
	diff = ktime_sub(now, kbdev->pm.backend.metrics.time_period_start);
	ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	kbdev->pm.backend.metrics.time_idle += ns_time;
	/* Reset time period */
	kbdev->pm.backend.metrics.time_period_start = now;
}

void kbase_pm_report_vsync(struct kbase_device *kbdev, int buffer_updated)
{
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	kbdev->pm.backend.metrics.vsync_hit = buffer_updated;
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}

KBASE_EXPORT_TEST_API(kbase_pm_report_vsync);

#if defined(CONFIG_PM_DEVFREQ) || defined(CONFIG_MALI_MIDGARD_DVFS)
/* caller needs to hold kbdev->pm.backend.metrics.lock before calling this
 * function
 */
static void kbase_pm_get_dvfs_utilisation_calc(struct kbase_device *kbdev,
								ktime_t now)
{
	ktime_t diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	diff = ktime_sub(now, kbdev->pm.backend.metrics.time_period_start);

	if (kbdev->pm.backend.metrics.gpu_active) {
		u32 ns_time = (u32) (ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);

		kbdev->pm.backend.metrics.time_busy += ns_time;
		kbdev->pm.backend.metrics.busy_cl[0] += ns_time *
				kbdev->pm.backend.metrics.active_cl_ctx[0];
		kbdev->pm.backend.metrics.busy_cl[1] += ns_time *
				kbdev->pm.backend.metrics.active_cl_ctx[1];
		kbdev->pm.backend.metrics.busy_gl += ns_time *
				kbdev->pm.backend.metrics.active_gl_ctx;
	} else {
		kbdev->pm.backend.metrics.time_idle += (u32) (ktime_to_ns(diff)
							>> KBASE_PM_TIME_SHIFT);
	}
}

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
							int util_cl_share[2])
{
	int utilisation;
	int busy;
	ktime_t now = ktime_get();

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

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);

	utilisation = kbase_pm_get_dvfs_utilisation_old(kbdev, &util_gl_share,
								util_cl_share);

	if (utilisation < 0 || util_gl_share < 0 || util_cl_share[0] < 0 ||
							util_cl_share[1] < 0) {
		action = KBASE_PM_DVFS_NOP;
		utilisation = 0;
		util_gl_share = 0;
		util_cl_share[0] = 0;
		util_cl_share[1] = 0;
		goto out;
	}

	if (kbdev->pm.backend.metrics.vsync_hit) {
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

	kbdev->pm.backend.metrics.utilisation = utilisation;
	kbdev->pm.backend.metrics.util_cl_share[0] = util_cl_share[0];
	kbdev->pm.backend.metrics.util_cl_share[1] = util_cl_share[1];
	kbdev->pm.backend.metrics.util_gl_share = util_gl_share;
out:
#ifdef CONFIG_MALI_MIDGARD_DVFS
	kbase_platform_dvfs_event(kbdev, utilisation, util_gl_share,
								util_cl_share);
#endif				/*CONFIG_MALI_MIDGARD_DVFS */

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);

	return action;
}
KBASE_EXPORT_TEST_API(kbase_pm_get_dvfs_action);

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

/* called when job is submitted to a GPU slot */
void kbase_pm_metrics_run_atom(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);

	/* We may have been idle before */
	if (kbdev->pm.backend.metrics.nr_in_slots == 0) {
		WARN_ON(kbdev->pm.backend.metrics.active_cl_ctx[0] != 0);
		WARN_ON(kbdev->pm.backend.metrics.active_cl_ctx[1] != 0);
		WARN_ON(kbdev->pm.backend.metrics.active_gl_ctx != 0);

		/* Record idle time */
		kbasep_pm_record_gpu_idle(kbdev);

		/* We are now active */
		WARN_ON(kbdev->pm.backend.metrics.gpu_active);
		kbdev->pm.backend.metrics.gpu_active = true;

	} else {
		/* Record active time */
		kbasep_pm_record_gpu_active(kbdev);
	}

	/* Track number of jobs in GPU slots */
	WARN_ON(kbdev->pm.backend.metrics.nr_in_slots == U8_MAX);
	kbdev->pm.backend.metrics.nr_in_slots++;

	/* Track if it was a CL or GL one that was submitted to a GPU slot */
	if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
		int device_nr = (katom->core_req &
				BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)
				? katom->device_nr : 0;
		KBASE_DEBUG_ASSERT(device_nr < 2);
		kbdev->pm.backend.metrics.active_cl_ctx[device_nr]++;
	} else {
		kbdev->pm.backend.metrics.active_gl_ctx++;
	}

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}

/* called when job is removed from a GPU slot */
void kbase_pm_metrics_release_atom(struct kbase_device *kbdev,
						struct kbase_jd_atom *katom)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);

	/* Track how long CL and/or GL jobs have been busy for */
	kbasep_pm_record_gpu_active(kbdev);

	/* Track number of jobs in GPU slots */
	WARN_ON(kbdev->pm.backend.metrics.nr_in_slots == 0);
	kbdev->pm.backend.metrics.nr_in_slots--;

	/* We may become idle */
	if (kbdev->pm.backend.metrics.nr_in_slots == 0) {
		KBASE_DEBUG_ASSERT(kbdev->pm.backend.metrics.gpu_active);
		kbdev->pm.backend.metrics.gpu_active = false;
	}

	/* Track of the GPU jobs that are active which ones are CL and
	 * which GL */
	if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
		int device_nr = (katom->core_req &
				BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)
				? katom->device_nr : 0;
		KBASE_DEBUG_ASSERT(device_nr < 2);

		WARN_ON(kbdev->pm.backend.metrics.active_cl_ctx[device_nr]
				== 0);
		kbdev->pm.backend.metrics.active_cl_ctx[device_nr]--;
	} else {
		WARN_ON(kbdev->pm.backend.metrics.active_gl_ctx == 0);
		kbdev->pm.backend.metrics.active_gl_ctx--;
	}

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}
