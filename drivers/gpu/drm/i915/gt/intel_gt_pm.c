/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_gt_pm.h"
#include "intel_pm.h"
#include "intel_wakeref.h"

static void pm_notify(struct drm_i915_private *i915, int state)
{
	blocking_notifier_call_chain(&i915->gt.pm_notifications, state, i915);
}

static int intel_gt_unpark(struct intel_wakeref *wf)
{
	struct drm_i915_private *i915 =
		container_of(wf, typeof(*i915), gt.wakeref);

	GEM_TRACE("\n");

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
	i915->gt.awake = intel_display_power_get(i915, POWER_DOMAIN_GT_IRQ);
	GEM_BUG_ON(!i915->gt.awake);

	intel_enable_gt_powersave(i915);

	i915_update_gfx_val(i915);
	if (INTEL_GEN(i915) >= 6)
		gen6_rps_busy(i915);

	i915_pmu_gt_unparked(i915);

	i915_queue_hangcheck(i915);

	pm_notify(i915, INTEL_GT_UNPARK);

	return 0;
}

void intel_gt_pm_get(struct intel_gt *gt)
{
	struct intel_runtime_pm *rpm = &gt->i915->runtime_pm;

	intel_wakeref_get(rpm, &gt->wakeref, intel_gt_unpark);
}

static int intel_gt_park(struct intel_wakeref *wf)
{
	struct drm_i915_private *i915 =
		container_of(wf, typeof(*i915), gt.wakeref);
	intel_wakeref_t wakeref = fetch_and_zero(&i915->gt.awake);

	GEM_TRACE("\n");

	pm_notify(i915, INTEL_GT_PARK);

	i915_pmu_gt_parked(i915);
	if (INTEL_GEN(i915) >= 6)
		gen6_rps_idle(i915);

	GEM_BUG_ON(!wakeref);
	intel_display_power_put(i915, POWER_DOMAIN_GT_IRQ, wakeref);

	return 0;
}

void intel_gt_pm_put(struct intel_gt *gt)
{
	struct intel_runtime_pm *rpm = &gt->i915->runtime_pm;

	intel_wakeref_put(rpm, &gt->wakeref, intel_gt_park);
}

void intel_gt_pm_init_early(struct intel_gt *gt)
{
	intel_wakeref_init(&gt->wakeref);
	BLOCKING_INIT_NOTIFIER_HEAD(&gt->pm_notifications);
}

static bool reset_engines(struct drm_i915_private *i915)
{
	if (INTEL_INFO(i915)->gpu_reset_clobbers_display)
		return false;

	return intel_gpu_reset(i915, ALL_ENGINES) == 0;
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

	GEM_TRACE("\n");

	if (!reset_engines(gt->i915) && !force)
		return;

	for_each_engine(engine, gt->i915, id)
		intel_engine_reset(engine, false);
}

void intel_gt_resume(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * After resume, we may need to poke into the pinned kernel
	 * contexts to paper over any damage caused by the sudden suspend.
	 * Only the kernel contexts should remain pinned over suspend,
	 * allowing us to fixup the user contexts on their first pin.
	 */
	for_each_engine(engine, gt->i915, id) {
		struct intel_context *ce;

		ce = engine->kernel_context;
		if (ce)
			ce->ops->reset(ce);

		ce = engine->preempt_context;
		if (ce)
			ce->ops->reset(ce);
	}
}
