/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_globals.h"
#include "i915_params.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"
#include "intel_gt_requests.h"
#include "intel_llc.h"
#include "intel_pm.h"
#include "intel_rc6.h"
#include "intel_rps.h"
#include "intel_wakeref.h"

static int __gt_unpark(struct intel_wakeref *wf)
{
	struct intel_gt *gt = container_of(wf, typeof(*gt), wakeref);
	struct drm_i915_private *i915 = gt->i915;

	GEM_TRACE("\n");

	i915_globals_unpark();

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

	intel_rps_unpark(&gt->rps);
	i915_pmu_gt_unparked(i915);

	intel_gt_unpark_requests(gt);

	return 0;
}

static int __gt_park(struct intel_wakeref *wf)
{
	struct intel_gt *gt = container_of(wf, typeof(*gt), wakeref);
	intel_wakeref_t wakeref = fetch_and_zero(&gt->awake);
	struct drm_i915_private *i915 = gt->i915;

	GEM_TRACE("\n");

	intel_gt_park_requests(gt);

	i915_vma_parked(gt);
	i915_pmu_gt_parked(i915);
	intel_rps_park(&gt->rps);

	/* Everything switched off, flush any residual interrupt just in case */
	intel_synchronize_irq(i915);

	GEM_BUG_ON(!wakeref);
	intel_display_power_put(i915, POWER_DOMAIN_GT_IRQ, wakeref);

	i915_globals_park();

	return 0;
}

static const struct intel_wakeref_ops wf_ops = {
	.get = __gt_unpark,
	.put = __gt_park,
	.flags = INTEL_WAKEREF_PUT_ASYNC,
};

void intel_gt_pm_init_early(struct intel_gt *gt)
{
	intel_wakeref_init(&gt->wakeref, gt->uncore->rpm, &wf_ops);
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

/**
 * intel_gt_sanitize: called after the GPU has lost power
 * @gt: the i915 GT container
 * @force: ignore a failed reset and sanitize engine state anyway
 *
 * Anytime we reset the GPU, either with an explicit GPU reset or through a
 * PCI power cycle, the GPU loses state and we must reset our state tracking
 * to match. Note that calling intel_gt_sanitize() if the GPU has not
 * been reset results in much confusion!
 */
void intel_gt_sanitize(struct intel_gt *gt, bool force)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;

	GEM_TRACE("force:%s\n", yesno(force));

	/* Use a raw wakeref to avoid calling intel_display_power_get early */
	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);

	/*
	 * As we have just resumed the machine and woken the device up from
	 * deep PCI sleep (presumably D3_cold), assume the HW has been reset
	 * back to defaults, recovering from whatever wedged state we left it
	 * in and so worth trying to use the device once more.
	 */
	if (intel_gt_is_wedged(gt))
		intel_gt_unset_wedged(gt);

	intel_uc_sanitize(&gt->uc);

	for_each_engine(engine, gt, id)
		if (engine->reset.prepare)
			engine->reset.prepare(engine);

	intel_uc_reset_prepare(&gt->uc);

	if (reset_engines(gt) || force) {
		for_each_engine(engine, gt, id)
			__intel_engine_reset(engine, false);
	}

	for_each_engine(engine, gt, id)
		if (engine->reset.finish)
			engine->reset.finish(engine);

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
	int err = 0;

	GEM_TRACE("\n");

	/*
	 * After resume, we may need to poke into the pinned kernel
	 * contexts to paper over any damage caused by the sudden suspend.
	 * Only the kernel contexts should remain pinned over suspend,
	 * allowing us to fixup the user contexts on their first pin.
	 */
	intel_gt_pm_get(gt);

	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);
	intel_rc6_sanitize(&gt->rc6);

	intel_rps_enable(&gt->rps);
	intel_llc_enable(&gt->llc);

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;

		intel_engine_pm_get(engine);

		ce = engine->kernel_context;
		if (ce) {
			GEM_BUG_ON(!intel_context_is_pinned(ce));
			ce->ops->reset(ce);
		}

		engine->serial++; /* kernel context lost */
		err = engine->resume(engine);

		intel_engine_pm_put(engine);
		if (err) {
			dev_err(gt->i915->drm.dev,
				"Failed to restart %s (%d)\n",
				engine->name, err);
			break;
		}
	}

	intel_rc6_enable(&gt->rc6);

	intel_uc_resume(&gt->uc);

	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);
	intel_gt_pm_put(gt);

	return err;
}

static void wait_for_idle(struct intel_gt *gt)
{
	if (intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT) == -ETIME) {
		/*
		 * Forcibly cancel outstanding work and leave
		 * the gpu quiet.
		 */
		intel_gt_set_wedged(gt);
	}

	intel_gt_pm_wait_for_idle(gt);
}

void intel_gt_suspend(struct intel_gt *gt)
{
	intel_wakeref_t wakeref;

	/* We expect to be idle already; but also want to be independent */
	wait_for_idle(gt);

	intel_uc_suspend(&gt->uc);

	with_intel_runtime_pm(gt->uncore->rpm, wakeref) {
		intel_rps_disable(&gt->rps);
		intel_rc6_disable(&gt->rc6);
		intel_llc_disable(&gt->llc);
	}

	intel_gt_sanitize(gt, false);

	GEM_TRACE("\n");
}

void intel_gt_runtime_suspend(struct intel_gt *gt)
{
	intel_uc_runtime_suspend(&gt->uc);

	GEM_TRACE("\n");
}

int intel_gt_runtime_resume(struct intel_gt *gt)
{
	GEM_TRACE("\n");

	intel_gt_init_swizzling(gt);

	return intel_uc_runtime_resume(&gt->uc);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_gt_pm.c"
#endif
