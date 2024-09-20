// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_perf_oa_regs.h"
#include "intel_engine_pm.h"
#include "intel_gt.h"
#include "intel_gt_mcr.h"
#include "intel_gt_pm.h"
#include "intel_gt_print.h"
#include "intel_gt_regs.h"
#include "intel_tlb.h"
#include "uc/intel_guc.h"

/*
 * HW architecture suggest typical invalidation time at 40us,
 * with pessimistic cases up to 100us and a recommendation to
 * cap at 1ms. We go a bit higher just in case.
 */
#define TLB_INVAL_TIMEOUT_US 100
#define TLB_INVAL_TIMEOUT_MS 4

/*
 * On Xe_HP the TLB invalidation registers are located at the same MMIO offsets
 * but are now considered MCR registers.  Since they exist within a GAM range,
 * the primary instance of the register rolls up the status from each unit.
 */
static int wait_for_invalidate(struct intel_engine_cs *engine)
{
	if (engine->tlb_inv.mcr)
		return intel_gt_mcr_wait_for_reg(engine->gt,
						 engine->tlb_inv.reg.mcr_reg,
						 engine->tlb_inv.done,
						 0,
						 TLB_INVAL_TIMEOUT_US,
						 TLB_INVAL_TIMEOUT_MS);
	else
		return __intel_wait_for_register_fw(engine->gt->uncore,
						    engine->tlb_inv.reg.reg,
						    engine->tlb_inv.done,
						    0,
						    TLB_INVAL_TIMEOUT_US,
						    TLB_INVAL_TIMEOUT_MS,
						    NULL);
}

static void mmio_invalidate_full(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	struct intel_engine_cs *engine;
	intel_engine_mask_t awake, tmp;
	enum intel_engine_id id;
	unsigned long flags;

	if (GRAPHICS_VER(i915) < 8)
		return;

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

	intel_gt_mcr_lock(gt, &flags);
	spin_lock(&uncore->lock); /* serialise invalidate with GT reset */

	awake = 0;
	for_each_engine(engine, gt, id) {
		if (!intel_engine_pm_is_awake(engine))
			continue;

		if (engine->tlb_inv.mcr)
			intel_gt_mcr_multicast_write_fw(gt,
							engine->tlb_inv.reg.mcr_reg,
							engine->tlb_inv.request);
		else
			intel_uncore_write_fw(uncore,
					      engine->tlb_inv.reg.reg,
					      engine->tlb_inv.request);

		awake |= engine->mask;
	}

	GT_TRACE(gt, "invalidated engines %08x\n", awake);

	/* Wa_2207587034:tgl,dg1,rkl,adl-s,adl-p */
	if (awake &&
	    (IS_TIGERLAKE(i915) ||
	     IS_DG1(i915) ||
	     IS_ROCKETLAKE(i915) ||
	     IS_ALDERLAKE_S(i915) ||
	     IS_ALDERLAKE_P(i915)))
		intel_uncore_write_fw(uncore, GEN12_OA_TLB_INV_CR, 1);

	spin_unlock(&uncore->lock);
	intel_gt_mcr_unlock(gt, flags);

	for_each_engine_masked(engine, gt, awake, tmp) {
		if (wait_for_invalidate(engine))
			gt_err_ratelimited(gt,
					   "%s TLB invalidation did not complete in %ums!\n",
					   engine->name, TLB_INVAL_TIMEOUT_MS);
	}

	/*
	 * Use delayed put since a) we mostly expect a flurry of TLB
	 * invalidations so it is good to avoid paying the forcewake cost and
	 * b) it works around a bug in Icelake which cannot cope with too rapid
	 * transitions.
	 */
	intel_uncore_forcewake_put_delayed(uncore, FORCEWAKE_ALL);
}

static bool tlb_seqno_passed(const struct intel_gt *gt, u32 seqno)
{
	u32 cur = intel_gt_tlb_seqno(gt);

	/* Only skip if a *full* TLB invalidate barrier has passed */
	return (s32)(cur - ALIGN(seqno, 2)) > 0;
}

void intel_gt_invalidate_tlb_full(struct intel_gt *gt, u32 seqno)
{
	intel_wakeref_t wakeref;

	if (I915_SELFTEST_ONLY(gt->awake == -ENODEV))
		return;

	if (intel_gt_is_wedged(gt))
		return;

	if (tlb_seqno_passed(gt, seqno))
		return;

	with_intel_gt_pm_if_awake(gt, wakeref) {
		struct intel_guc *guc = gt_to_guc(gt);

		mutex_lock(&gt->tlb.invalidate_lock);
		if (tlb_seqno_passed(gt, seqno))
			goto unlock;

		if (HAS_GUC_TLB_INVALIDATION(gt->i915)) {
			/*
			 * Only perform GuC TLB invalidation if GuC is ready.
			 * The only time GuC could not be ready is on GT reset,
			 * which would clobber all the TLBs anyways, making
			 * any TLB invalidation path here unnecessary.
			 */
			if (intel_guc_is_ready(guc))
				intel_guc_invalidate_tlb_engines(guc);
		} else {
			mmio_invalidate_full(gt);
		}

		write_seqcount_invalidate(&gt->tlb.seqno);
unlock:
		mutex_unlock(&gt->tlb.invalidate_lock);
	}
}

void intel_gt_init_tlb(struct intel_gt *gt)
{
	mutex_init(&gt->tlb.invalidate_lock);
	seqcount_mutex_init(&gt->tlb.seqno, &gt->tlb.invalidate_lock);
}

void intel_gt_fini_tlb(struct intel_gt *gt)
{
	mutex_destroy(&gt->tlb.invalidate_lock);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_tlb.c"
#endif
