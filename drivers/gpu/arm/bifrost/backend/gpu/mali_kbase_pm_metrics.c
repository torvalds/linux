/*
 *
 * (C) COPYRIGHT 2011-2018 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/*
 * Metrics for power management
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_jm_rb.h>
#include <backend/gpu/mali_kbase_pm_defs.h>

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

#ifdef CONFIG_MALI_BIFROST_DVFS
static enum hrtimer_restart dvfs_callback(struct hrtimer *timer)
{
	unsigned long flags;
	struct kbasep_pm_metrics_state *metrics;

	KBASE_DEBUG_ASSERT(timer != NULL);

	metrics = container_of(timer, struct kbasep_pm_metrics_state, timer);
	kbase_pm_get_dvfs_action(metrics->kbdev);

	spin_lock_irqsave(&metrics->lock, flags);

	if (metrics->timer_active)
		hrtimer_start(timer,
			HR_TIMER_DELAY_MSEC(metrics->kbdev->pm.dvfs_period),
			HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&metrics->lock, flags);

	return HRTIMER_NORESTART;
}
#endif /* CONFIG_MALI_BIFROST_DVFS */

int kbasep_pm_metrics_init(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	kbdev->pm.backend.metrics.kbdev = kbdev;

	kbdev->pm.backend.metrics.time_period_start = ktime_get();
	kbdev->pm.backend.metrics.gpu_active = false;
	kbdev->pm.backend.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[1] = 0;

	kbdev->pm.backend.metrics.values.time_busy = 0;
	kbdev->pm.backend.metrics.values.time_idle = 0;
	kbdev->pm.backend.metrics.values.busy_cl[0] = 0;
	kbdev->pm.backend.metrics.values.busy_cl[1] = 0;
	kbdev->pm.backend.metrics.values.busy_gl = 0;

	spin_lock_init(&kbdev->pm.backend.metrics.lock);

#ifdef CONFIG_MALI_BIFROST_DVFS
	kbdev->pm.backend.metrics.timer_active = true;
	hrtimer_init(&kbdev->pm.backend.metrics.timer, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	kbdev->pm.backend.metrics.timer.function = dvfs_callback;

	hrtimer_start(&kbdev->pm.backend.metrics.timer,
			HR_TIMER_DELAY_MSEC(kbdev->pm.dvfs_period),
			HRTIMER_MODE_REL);
#endif /* CONFIG_MALI_BIFROST_DVFS */

	return 0;
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_init);

void kbasep_pm_metrics_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_BIFROST_DVFS
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	kbdev->pm.backend.metrics.timer_active = false;
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);

	hrtimer_cancel(&kbdev->pm.backend.metrics.timer);
#endif /* CONFIG_MALI_BIFROST_DVFS */
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

		kbdev->pm.backend.metrics.values.time_busy += ns_time;
		if (kbdev->pm.backend.metrics.active_cl_ctx[0])
			kbdev->pm.backend.metrics.values.busy_cl[0] += ns_time;
		if (kbdev->pm.backend.metrics.active_cl_ctx[1])
			kbdev->pm.backend.metrics.values.busy_cl[1] += ns_time;
		if (kbdev->pm.backend.metrics.active_gl_ctx[0])
			kbdev->pm.backend.metrics.values.busy_gl += ns_time;
		if (kbdev->pm.backend.metrics.active_gl_ctx[1])
			kbdev->pm.backend.metrics.values.busy_gl += ns_time;
	} else {
		kbdev->pm.backend.metrics.values.time_idle += (u32) (ktime_to_ns(diff)
							>> KBASE_PM_TIME_SHIFT);
	}

	kbdev->pm.backend.metrics.time_period_start = now;
}

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS)
void kbase_pm_get_dvfs_metrics(struct kbase_device *kbdev,
			       struct kbasep_pm_metrics *last,
			       struct kbasep_pm_metrics *diff)
{
	struct kbasep_pm_metrics *cur = &kbdev->pm.backend.metrics.values;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
	kbase_pm_get_dvfs_utilisation_calc(kbdev, ktime_get());

	memset(diff, 0, sizeof(*diff));
	diff->time_busy = cur->time_busy - last->time_busy;
	diff->time_idle = cur->time_idle - last->time_idle;
	diff->busy_cl[0] = cur->busy_cl[0] - last->busy_cl[0];
	diff->busy_cl[1] = cur->busy_cl[1] - last->busy_cl[1];
	diff->busy_gl = cur->busy_gl - last->busy_gl;

	*last = *cur;

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}
KBASE_EXPORT_TEST_API(kbase_pm_get_dvfs_metrics);
#endif

#ifdef CONFIG_MALI_BIFROST_DVFS
void kbase_pm_get_dvfs_action(struct kbase_device *kbdev)
{
	int utilisation, util_gl_share;
	int util_cl_share[2];
	int busy;
	struct kbasep_pm_metrics *diff;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	diff = &kbdev->pm.backend.metrics.dvfs_diff;

	kbase_pm_get_dvfs_metrics(kbdev, &kbdev->pm.backend.metrics.dvfs_last, diff);

	utilisation = (100 * diff->time_busy) /
			max(diff->time_busy + diff->time_idle, 1u);

	busy = max(diff->busy_gl + diff->busy_cl[0] + diff->busy_cl[1], 1u);
	util_gl_share = (100 * diff->busy_gl) / busy;
	util_cl_share[0] = (100 * diff->busy_cl[0]) / busy;
	util_cl_share[1] = (100 * diff->busy_cl[1]) / busy;

	kbase_platform_dvfs_event(kbdev, utilisation, util_gl_share, util_cl_share);
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

#endif /* CONFIG_MALI_BIFROST_DVFS */

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
				if (!WARN_ON(device_nr >= 2))
					kbdev->pm.backend.metrics.
						active_cl_ctx[device_nr] = 1;
			} else {
				/* Slot 2 should not be running non-compute
				 * atoms */
				if (!WARN_ON(js >= 2))
					kbdev->pm.backend.metrics.
						active_gl_ctx[js] = 1;
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

	lockdep_assert_held(&kbdev->hwaccess_lock);

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
