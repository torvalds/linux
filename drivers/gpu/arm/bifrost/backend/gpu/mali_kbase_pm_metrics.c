// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2011-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * Metrics for power management
 */

#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#if MALI_USE_CSF
#include "backend/gpu/mali_kbase_clk_rate_trace_mgr.h"
#include <csf/ipa_control/mali_kbase_csf_ipa_control.h>
#else
#include <backend/gpu/mali_kbase_jm_rb.h>
#endif /* !MALI_USE_CSF */

#include <backend/gpu/mali_kbase_pm_defs.h>
#include <mali_linux_trace.h>

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS) || !MALI_USE_CSF
/* Shift used for kbasep_pm_metrics_data.time_busy/idle - units of (1 << 8) ns
 * This gives a maximum period between samples of 2^(32+8)/100 ns = slightly
 * under 11s. Exceeding this will cause overflow
 */
#define KBASE_PM_TIME_SHIFT			8
#endif

#if MALI_USE_CSF
/* To get the GPU_ACTIVE value in nano seconds unit */
#define GPU_ACTIVE_SCALING_FACTOR ((u64)1E9)
#endif

/*
 * Possible state transitions
 * ON        -> ON | OFF | STOPPED
 * STOPPED   -> ON | OFF
 * OFF       -> ON
 *
 *
 * ┌─e─┐┌────────────f─────────────┐
 * │   v│                          v
 * └───ON ──a──> STOPPED ──b──> OFF
 *     ^^            │             │
 *     │└──────c─────┘             │
 *     │                           │
 *     └─────────────d─────────────┘
 *
 * Transition effects:
 * a. None
 * b. Timer expires without restart
 * c. Timer is not stopped, timer period is unaffected
 * d. Timer must be restarted
 * e. Callback is executed and the timer is restarted
 * f. Timer is cancelled, or the callback is waited on if currently executing. This is called during
 *    tear-down and should not be subject to a race from an OFF->ON transition
 */
enum dvfs_metric_timer_state { TIMER_OFF, TIMER_STOPPED, TIMER_ON };

#ifdef CONFIG_MALI_BIFROST_DVFS
static enum hrtimer_restart dvfs_callback(struct hrtimer *timer)
{
	struct kbasep_pm_metrics_state *metrics;

	if (WARN_ON(!timer))
		return HRTIMER_NORESTART;

	metrics = container_of(timer, struct kbasep_pm_metrics_state, timer);

	/* Transition (b) to fully off if timer was stopped, don't restart the timer in this case */
	if (atomic_cmpxchg(&metrics->timer_state, TIMER_STOPPED, TIMER_OFF) != TIMER_ON)
		return HRTIMER_NORESTART;

	kbase_pm_get_dvfs_action(metrics->kbdev);

	/* Set the new expiration time and restart (transition e) */
	hrtimer_forward_now(timer, HR_TIMER_DELAY_MSEC(metrics->kbdev->pm.dvfs_period));
	return HRTIMER_RESTART;
}
#endif /* CONFIG_MALI_BIFROST_DVFS */

int kbasep_pm_metrics_init(struct kbase_device *kbdev)
{
#if MALI_USE_CSF
	struct kbase_ipa_control_perf_counter perf_counter;
	int err;

	/* One counter group */
	const size_t NUM_PERF_COUNTERS = 1;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	kbdev->pm.backend.metrics.kbdev = kbdev;
	kbdev->pm.backend.metrics.time_period_start = ktime_get_raw();
	kbdev->pm.backend.metrics.values.time_busy = 0;
	kbdev->pm.backend.metrics.values.time_idle = 0;
	kbdev->pm.backend.metrics.values.time_in_protm = 0;

	perf_counter.scaling_factor = GPU_ACTIVE_SCALING_FACTOR;

	/* Normalize values by GPU frequency */
	perf_counter.gpu_norm = true;

	/* We need the GPU_ACTIVE counter, which is in the CSHW group */
	perf_counter.type = KBASE_IPA_CORE_TYPE_CSHW;

	/* We need the GPU_ACTIVE counter */
	perf_counter.idx = GPU_ACTIVE_CNT_IDX;

	err = kbase_ipa_control_register(
		kbdev, &perf_counter, NUM_PERF_COUNTERS,
		&kbdev->pm.backend.metrics.ipa_control_client);
	if (err) {
		dev_err(kbdev->dev,
			"Failed to register IPA with kbase_ipa_control: err=%d",
			err);
		return -1;
	}
#else
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	kbdev->pm.backend.metrics.kbdev = kbdev;
	kbdev->pm.backend.metrics.time_period_start = ktime_get_raw();

	kbdev->pm.backend.metrics.gpu_active = false;
	kbdev->pm.backend.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[1] = 0;
	kbdev->pm.backend.metrics.active_gl_ctx[2] = 0;

	kbdev->pm.backend.metrics.values.time_busy = 0;
	kbdev->pm.backend.metrics.values.time_idle = 0;
	kbdev->pm.backend.metrics.values.busy_cl[0] = 0;
	kbdev->pm.backend.metrics.values.busy_cl[1] = 0;
	kbdev->pm.backend.metrics.values.busy_gl = 0;

#endif
	spin_lock_init(&kbdev->pm.backend.metrics.lock);

#ifdef CONFIG_MALI_BIFROST_DVFS
	hrtimer_init(&kbdev->pm.backend.metrics.timer, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	kbdev->pm.backend.metrics.timer.function = dvfs_callback;
	kbdev->pm.backend.metrics.initialized = true;
	atomic_set(&kbdev->pm.backend.metrics.timer_state, TIMER_OFF);
	kbase_pm_metrics_start(kbdev);
#endif /* CONFIG_MALI_BIFROST_DVFS */

#if MALI_USE_CSF
	/* The sanity check on the GPU_ACTIVE performance counter
	 * is skipped for Juno platforms that have timing problems.
	 */
	kbdev->pm.backend.metrics.skip_gpu_active_sanity_check =
		of_machine_is_compatible("arm,juno");
#endif

	return 0;
}
KBASE_EXPORT_TEST_API(kbasep_pm_metrics_init);

void kbasep_pm_metrics_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_BIFROST_DVFS
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	/* Cancel the timer, and block if the callback is currently executing (transition f) */
	kbdev->pm.backend.metrics.initialized = false;
	atomic_set(&kbdev->pm.backend.metrics.timer_state, TIMER_OFF);
	hrtimer_cancel(&kbdev->pm.backend.metrics.timer);
#endif /* CONFIG_MALI_BIFROST_DVFS */

#if MALI_USE_CSF
	kbase_ipa_control_unregister(
		kbdev, kbdev->pm.backend.metrics.ipa_control_client);
#endif
}

KBASE_EXPORT_TEST_API(kbasep_pm_metrics_term);

/* caller needs to hold kbdev->pm.backend.metrics.lock before calling this
 * function
 */
#if MALI_USE_CSF
#if defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS)
static void kbase_pm_get_dvfs_utilisation_calc(struct kbase_device *kbdev)
{
	int err;
	u64 gpu_active_counter;
	u64 protected_time;
	ktime_t now;

	lockdep_assert_held(&kbdev->pm.backend.metrics.lock);

	/* Query IPA_CONTROL for the latest GPU-active and protected-time
	 * info.
	 */
	err = kbase_ipa_control_query(
		kbdev, kbdev->pm.backend.metrics.ipa_control_client,
		&gpu_active_counter, 1, &protected_time);

	/* Read the timestamp after reading the GPU_ACTIVE counter value.
	 * This ensures the time gap between the 2 reads is consistent for
	 * a meaningful comparison between the increment of GPU_ACTIVE and
	 * elapsed time. The lock taken inside kbase_ipa_control_query()
	 * function can cause lot of variation.
	 */
	now = ktime_get_raw();

	if (err) {
		dev_err(kbdev->dev,
			"Failed to query the increment of GPU_ACTIVE counter: err=%d",
			err);
	} else {
		u64 diff_ns;
		s64 diff_ns_signed;
		u32 ns_time;
		ktime_t diff = ktime_sub(
			now, kbdev->pm.backend.metrics.time_period_start);

		diff_ns_signed = ktime_to_ns(diff);

		if (diff_ns_signed < 0)
			return;

		diff_ns = (u64)diff_ns_signed;

#if !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
		/* The GPU_ACTIVE counter shouldn't clock-up more time than has
		 * actually elapsed - but still some margin needs to be given
		 * when doing the comparison. There could be some drift between
		 * the CPU and GPU clock.
		 *
		 * Can do the check only in a real driver build, as an arbitrary
		 * value for GPU_ACTIVE can be fed into dummy model in no_mali
		 * configuration which may not correspond to the real elapsed
		 * time.
		 */
		if (!kbdev->pm.backend.metrics.skip_gpu_active_sanity_check) {
			/* The margin is scaled to allow for the worst-case
			 * scenario where the samples are maximally separated,
			 * plus a small offset for sampling errors.
			 */
			u64 const MARGIN_NS =
				IPA_CONTROL_TIMER_DEFAULT_VALUE_MS * NSEC_PER_MSEC * 3 / 2;

			if (gpu_active_counter > (diff_ns + MARGIN_NS)) {
				dev_info(
					kbdev->dev,
					"GPU activity takes longer than time interval: %llu ns > %llu ns",
					(unsigned long long)gpu_active_counter,
					(unsigned long long)diff_ns);
			}
		}
#endif
		/* Calculate time difference in units of 256ns */
		ns_time = (u32)(diff_ns >> KBASE_PM_TIME_SHIFT);

		/* Add protected_time to gpu_active_counter so that time in
		 * protected mode is included in the apparent GPU active time,
		 * then convert it from units of 1ns to units of 256ns, to
		 * match what JM GPUs use. The assumption is made here that the
		 * GPU is 100% busy while in protected mode, so we should add
		 * this since the GPU can't (and thus won't) update these
		 * counters while it's actually in protected mode.
		 *
		 * Perform the add after dividing each value down, to reduce
		 * the chances of overflows.
		 */
		protected_time >>= KBASE_PM_TIME_SHIFT;
		gpu_active_counter >>= KBASE_PM_TIME_SHIFT;
		gpu_active_counter += protected_time;

		/* Ensure the following equations don't go wrong if ns_time is
		 * slightly larger than gpu_active_counter somehow
		 */
		gpu_active_counter = MIN(gpu_active_counter, ns_time);

		kbdev->pm.backend.metrics.values.time_busy +=
			gpu_active_counter;

		kbdev->pm.backend.metrics.values.time_idle +=
			ns_time - gpu_active_counter;

		/* Also make time in protected mode available explicitly,
		 * so users of this data have this info, too.
		 */
		kbdev->pm.backend.metrics.values.time_in_protm +=
			protected_time;
	}

	kbdev->pm.backend.metrics.time_period_start = now;
}
#endif /* defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS) */
#else
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
		if (kbdev->pm.backend.metrics.active_gl_ctx[2])
			kbdev->pm.backend.metrics.values.busy_gl += ns_time;
	} else {
		kbdev->pm.backend.metrics.values.time_idle +=
			(u32)(ktime_to_ns(diff) >> KBASE_PM_TIME_SHIFT);
	}

	kbdev->pm.backend.metrics.time_period_start = now;
}
#endif  /* MALI_USE_CSF */

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) || defined(CONFIG_MALI_BIFROST_DVFS)
void kbase_pm_get_dvfs_metrics(struct kbase_device *kbdev,
			       struct kbasep_pm_metrics *last,
			       struct kbasep_pm_metrics *diff)
{
	struct kbasep_pm_metrics *cur = &kbdev->pm.backend.metrics.values;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.backend.metrics.lock, flags);
#if MALI_USE_CSF
	kbase_pm_get_dvfs_utilisation_calc(kbdev);
#else
	kbase_pm_get_dvfs_utilisation_calc(kbdev, ktime_get_raw());
#endif

	memset(diff, 0, sizeof(*diff));
	diff->time_busy = cur->time_busy - last->time_busy;
	diff->time_idle = cur->time_idle - last->time_idle;

#if MALI_USE_CSF
	diff->time_in_protm = cur->time_in_protm - last->time_in_protm;
#else
	diff->busy_cl[0] = cur->busy_cl[0] - last->busy_cl[0];
	diff->busy_cl[1] = cur->busy_cl[1] - last->busy_cl[1];
	diff->busy_gl = cur->busy_gl - last->busy_gl;
#endif

	*last = *cur;

	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}
KBASE_EXPORT_TEST_API(kbase_pm_get_dvfs_metrics);
#endif

#ifdef CONFIG_MALI_BIFROST_DVFS
void kbase_pm_get_dvfs_action(struct kbase_device *kbdev)
{
	int utilisation;
	struct kbasep_pm_metrics *diff;
#if !MALI_USE_CSF
	int busy;
	int util_gl_share;
	int util_cl_share[2];
#endif

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	diff = &kbdev->pm.backend.metrics.dvfs_diff;

	kbase_pm_get_dvfs_metrics(kbdev, &kbdev->pm.backend.metrics.dvfs_last,
				  diff);

	utilisation = (100 * diff->time_busy) /
			max(diff->time_busy + diff->time_idle, 1u);

#if !MALI_USE_CSF
	busy = max(diff->busy_gl + diff->busy_cl[0] + diff->busy_cl[1], 1u);

	util_gl_share = (100 * diff->busy_gl) / busy;
	util_cl_share[0] = (100 * diff->busy_cl[0]) / busy;
	util_cl_share[1] = (100 * diff->busy_cl[1]) / busy;

	kbase_platform_dvfs_event(kbdev, utilisation, util_gl_share,
				  util_cl_share);
#else
	/* Note that, at present, we don't pass protected-mode time to the
	 * platform here. It's unlikely to be useful, however, as the platform
	 * probably just cares whether the GPU is busy or not; time in
	 * protected mode is already added to busy-time at this point, though,
	 * so we should be good.
	 */
	kbase_platform_dvfs_event(kbdev, utilisation);
#endif
}

bool kbase_pm_metrics_is_active(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	return atomic_read(&kbdev->pm.backend.metrics.timer_state) == TIMER_ON;
}
KBASE_EXPORT_TEST_API(kbase_pm_metrics_is_active);

void kbase_pm_metrics_start(struct kbase_device *kbdev)
{
	struct kbasep_pm_metrics_state *metrics = &kbdev->pm.backend.metrics;

	if (unlikely(!metrics->initialized))
		return;

	/* Transition to ON, from a stopped state (transition c) */
	if (atomic_xchg(&metrics->timer_state, TIMER_ON) == TIMER_OFF)
		/* Start the timer only if it's been fully stopped (transition d)*/
		hrtimer_start(&metrics->timer, HR_TIMER_DELAY_MSEC(kbdev->pm.dvfs_period),
			      HRTIMER_MODE_REL);
}

void kbase_pm_metrics_stop(struct kbase_device *kbdev)
{
	if (unlikely(!kbdev->pm.backend.metrics.initialized))
		return;

	/* Timer is Stopped if its currently on (transition a) */
	atomic_cmpxchg(&kbdev->pm.backend.metrics.timer_state, TIMER_ON, TIMER_STOPPED);
}


#endif /* CONFIG_MALI_BIFROST_DVFS */

#if !MALI_USE_CSF
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
	kbdev->pm.backend.metrics.active_gl_ctx[2] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[0] = 0;
	kbdev->pm.backend.metrics.active_cl_ctx[1] = 0;
	kbdev->pm.backend.metrics.gpu_active = false;

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		struct kbase_jd_atom *katom = kbase_gpu_inspect(kbdev, js, 0);

		/* Head atom may have just completed, so if it isn't running
		 * then try the next atom
		 */
		if (katom && katom->gpu_rb_state != KBASE_ATOM_GPU_RB_SUBMITTED)
			katom = kbase_gpu_inspect(kbdev, js, 1);

		if (katom && katom->gpu_rb_state ==
				KBASE_ATOM_GPU_RB_SUBMITTED) {
			if (katom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
				int device_nr = (katom->core_req &
					BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)
						? katom->device_nr : 0;
				if (!WARN_ON(device_nr >= 2))
					kbdev->pm.backend.metrics.active_cl_ctx[device_nr] = 1;
			} else {
				kbdev->pm.backend.metrics.active_gl_ctx[js] = 1;
				trace_sysgraph(SGR_ACTIVE, 0, js);
			}
			kbdev->pm.backend.metrics.gpu_active = true;
		} else {
			trace_sysgraph(SGR_INACTIVE, 0, js);
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
		now = ktime_get_raw();
		timestamp = &now;
	}

	/* Track how much of time has been spent busy or idle. For JM GPUs,
	 * this also evaluates how long CL and/or GL jobs have been busy for.
	 */
	kbase_pm_get_dvfs_utilisation_calc(kbdev, *timestamp);

	kbase_pm_metrics_active_calc(kbdev);
	spin_unlock_irqrestore(&kbdev->pm.backend.metrics.lock, flags);
}
#endif /* !MALI_USE_CSF */
