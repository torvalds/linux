// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/string_helpers.h>
#include <linux/suspend.h>

#include "i915_drv.h"
#include "i915_params.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_gt.h"
#include "intel_gt_clock_utils.h"
#include "intel_gt_pm.h"
#include "intel_gt_requests.h"
#include "intel_llc.h"
#include "intel_pm.h"
#include "intel_rc6.h"
#include "intel_rps.h"
#include "intel_wakeref.h"
#include "pxp/intel_pxp_pm.h"

#define I915_GT_SUSPEND_IDLE_TIMEOUT (HZ / 2)

static void user_forcewake(struct intel_gt *gt, bool suspend)
{
	int count = atomic_read(&gt->user_wakeref);

	/* Inside suspend/resume so single threaded, no races to worry about. */
	if (likely(!count))
		return;

	intel_gt_pm_get(gt);
	if (suspend) {
		GEM_BUG_ON(count > atomic_read(&gt->wakeref.count));
		atomic_sub(count, &gt->wakeref.count);
	} else {
		atomic_add(count, &gt->wakeref.count);
	}
	intel_gt_pm_put(gt);
}

static void runtime_begin(struct intel_gt *gt)
{
	local_irq_disable();
	write_seqcount_begin(&gt->stats.lock);
	gt->stats.start = ktime_get();
	gt->stats.active = true;
	write_seqcount_end(&gt->stats.lock);
	local_irq_enable();
}

static void runtime_end(struct intel_gt *gt)
{
	local_irq_disable();
	write_seqcount_begin(&gt->stats.lock);
	gt->stats.active = false;
	gt->stats.total =
		ktime_add(gt->stats.total,
			  ktime_sub(ktime_get(), gt->stats.start));
	write_seqcount_end(&gt->stats.lock);
	local_irq_enable();
}

static int __gt_unpark(struct intel_wakeref *wf)
{
	struct intel_gt *gt = container_of(wf, typeof(*gt), wakeref);
	struct drm_i915_private *i915 = gt->i915;

	GT_TRACE(gt, "\n");

	/*
	 * It seems that the DMC likes to transition between the DC states a lot
	 * when there are no connected displays (no active power domains) during
	 * command submission.
	 *
	 * This activity has negative impact on the performance of the chip with
	 * huge latencies observed in the interrupt handler and elsewhere.
	 *
	 * Work around it by grabbing a GT IRQ power domain whilst there is any
	 * GT activity, preventing any DC state transitions.
	 */
	gt->awake = intel_display_power_get(i915, POWER_DOMAIN_GT_IRQ);
	GEM_BUG_ON(!gt->awake);

	intel_rc6_unpark(&gt->rc6);
	intel_rps_unpark(&gt->rps);
	i915_pmu_gt_unparked(i915);
	intel_guc_busyness_unpark(gt);

	intel_gt_unpark_requests(gt);
	runtime_begin(gt);

	return 0;
}

static int __gt_park(struct intel_wakeref *wf)
{
	struct intel_gt *gt = container_of(wf, typeof(*gt), wakeref);
	intel_wakeref_t wakeref = fetch_and_zero(&gt->awake);
	struct drm_i915_private *i915 = gt->i915;

	GT_TRACE(gt, "\n");

	runtime_end(gt);
	intel_gt_park_requests(gt);

	intel_guc_busyness_park(gt);
	i915_vma_parked(gt);
	i915_pmu_gt_parked(i915);
	intel_rps_park(&gt->rps);
	intel_rc6_park(&gt->rc6);

	/* Everything switched off, flush any residual interrupt just in case */
	intel_synchronize_irq(i915);

	/* Defer dropping the display power well for 100ms, it's slow! */
	GEM_BUG_ON(!wakeref);
	intel_display_power_put_async(i915, POWER_DOMAIN_GT_IRQ, wakeref);

	return 0;
}

static const struct intel_wakeref_ops wf_ops = {
	.get = __gt_unpark,
	.put = __gt_park,
};

void intel_gt_pm_init_early(struct intel_gt *gt)
{
	intel_wakeref_init(&gt->wakeref, gt->uncore->rpm, &wf_ops);
	seqcount_mutex_init(&gt->stats.lock, &gt->wakeref.mutex);
}

void intel_gt_pm_init(struct intel_gt *gt)
{
	/*
	 * Enabling power-management should be "self-healing". If we cannot
	 * enable a feature, simply leave it disabled with a notice to the
	 * user.
	 */
	intel_rc6_init(&gt->rc6);
	intel_rps_init(&gt->rps);
}

static bool reset_engines(struct intel_gt *gt)
{
	if (INTEL_INFO(gt->i915)->gpu_reset_clobbers_display)
		return false;

	return __intel_gt_reset(gt, ALL_ENGINES) == 0;
}

static void gt_sanitize(struct intel_gt *gt, bool force)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;

	GT_TRACE(gt, "force:%s", str_yes_no(force));

	/* Use a raw wakeref to avoid calling intel_display_power_get early */
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);

	intel_gt_check_clock_frequency(gt);

	/*
	 * As we have just resumed the machine and woken the device up from
	 * deep PCI sleep (presumably D3_cold), assume the HW has been reset
	 * back to defaults, recovering from whatever wedged state we left it
	 * in and so worth trying to use the device once more.
	 */
	if (intel_gt_is_wedged(gt))
		intel_gt_unset_wedged(gt);

	for_each_engine(engine, gt, id)
		if (engine->reset.prepare)
			engine->reset.prepare(engine);

	intel_uc_reset_prepare(&gt->uc);

	for_each_engine(engine, gt, id)
		if (engine->sanitize)
			engine->sanitize(engine);

	if (reset_engines(gt) || force) {
		for_each_engine(engine, gt, id)
			__intel_engine_reset(engine, false);
	}

	intel_uc_reset(&gt->uc, false);

	for_each_engine(engine, gt, id)
		if (engine->reset.finish)
			engine->reset.finish(engine);

	intel_rps_sanitize(&gt->rps);

	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
}

void intel_gt_pm_fini(struct intel_gt *gt)
{
	intel_rc6_fini(&gt->rc6);
}

int intel_gt_resume(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err;

	err = intel_gt_has_unrecoverable_error(gt);
	if (err)
		return err;

	GT_TRACE(gt, "\n");

	/*
	 * After resume, we may need to poke into the pinned kernel
	 * contexts to paper over any damage caused by the sudden suspend.
	 * Only the kernel contexts should remain pinned over suspend,
	 * allowing us to fixup the user contexts on their first pin.
	 */
	gt_sanitize(gt, true);

	intel_gt_pm_get(gt);

	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);
	intel_rc6_sanitize(&gt->rc6);
	if (intel_gt_is_wedged(gt)) {
		err = -EIO;
		goto out_fw;
	}

	/* Only when the HW is re-initialised, can we replay the requests */
	err = intel_gt_init_hw(gt);
	if (err) {
		i915_probe_error(gt->i915,
				 "Failed to initialize GPU, declaring it wedged!\n");
		goto err_wedged;
	}

	intel_uc_reset_finish(&gt->uc);

	intel_rps_enable(&gt->rps);
	intel_llc_enable(&gt->llc);

	for_each_engine(engine, gt, id) {
		intel_engine_pm_get(engine);

		engine->serial++; /* kernel context lost */
		err = intel_engine_resume(engine);

		intel_engine_pm_put(engine);
		if (err) {
			drm_err(&gt->i915->drm,
				"Failed to restart %s (%d)\n",
				engine->name, err);
			goto err_wedged;
		}
	}

	intel_rc6_enable(&gt->rc6);

	intel_uc_resume(&gt->uc);

	intel_pxp_resume(&gt->pxp);

	user_forcewake(gt, false);

out_fw:
	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);
	intel_gt_pm_put(gt);
	return err;

err_wedged:
	intel_gt_set_wedged(gt);
	goto out_fw;
}

static void wait_for_suspend(struct intel_gt *gt)
{
	if (!intel_gt_pm_is_awake(gt))
		return;

	if (intel_gt_wait_for_idle(gt, I915_GT_SUSPEND_IDLE_TIMEOUT) == -ETIME) {
		/*
		 * Forcibly cancel outstanding work and leave
		 * the gpu quiet.
		 */
		intel_gt_set_wedged(gt);
		intel_gt_retire_requests(gt);
	}

	intel_gt_pm_wait_for_idle(gt);
}

void intel_gt_suspend_prepare(struct intel_gt *gt)
{
	user_forcewake(gt, true);
	wait_for_suspend(gt);

	intel_pxp_suspend_prepare(&gt->pxp);
}

static suspend_state_t pm_suspend_target(void)
{
#if IS_ENABLED(CONFIG_SUSPEND) && IS_ENABLED(CONFIG_PM_SLEEP)
	return pm_suspend_target_state;
#else
	return PM_SUSPEND_TO_IDLE;
#endif
}

void intel_gt_suspend_late(struct intel_gt *gt)
{
	intel_wakeref_t wakeref;

	/* We expect to be idle already; but also want to be independent */
	wait_for_suspend(gt);

	if (is_mock_gt(gt))
		return;

	GEM_BUG_ON(gt->awake);

	intel_uc_suspend(&gt->uc);
	intel_pxp_suspend(&gt->pxp);

	/*
	 * On disabling the device, we want to turn off HW access to memory
	 * that we no longer own.
	 *
	 * However, not all suspend-states disable the device. S0 (s2idle)
	 * is effectively runtime-suspend, the device is left powered on
	 * but needs to be put into a low power state. We need to keep
	 * powermanagement enabled, but we also retain system state and so
	 * it remains safe to keep on using our allocated memory.
	 */
	if (pm_suspend_target() == PM_SUSPEND_TO_IDLE)
		return;

	with_intel_runtime_pm(gt->uncore->rpm, wakeref) {
		intel_rps_disable(&gt->rps);
		intel_rc6_disable(&gt->rc6);
		intel_llc_disable(&gt->llc);
	}

	gt_sanitize(gt, false);

	GT_TRACE(gt, "\n");
}

void intel_gt_runtime_suspend(struct intel_gt *gt)
{
	intel_pxp_runtime_suspend(&gt->pxp);
	intel_uc_runtime_suspend(&gt->uc);

	GT_TRACE(gt, "\n");
}

int intel_gt_runtime_resume(struct intel_gt *gt)
{
	int ret;

	GT_TRACE(gt, "\n");
	intel_gt_init_swizzling(gt);
	intel_ggtt_restore_fences(gt->ggtt);

	ret = intel_uc_runtime_resume(&gt->uc);
	if (ret)
		return ret;

	intel_pxp_runtime_resume(&gt->pxp);

	return 0;
}

static ktime_t __intel_gt_get_awake_time(const struct intel_gt *gt)
{
	ktime_t total = gt->stats.total;

	if (gt->stats.active)
		total = ktime_add(total,
				  ktime_sub(ktime_get(), gt->stats.start));

	return total;
}

ktime_t intel_gt_get_awake_time(const struct intel_gt *gt)
{
	unsigned int seq;
	ktime_t total;

	do {
		seq = read_seqcount_begin(&gt->stats.lock);
		total = __intel_gt_get_awake_time(gt);
	} while (read_seqcount_retry(&gt->stats.lock, seq));

	return total;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_gt_pm.c"
#endif
