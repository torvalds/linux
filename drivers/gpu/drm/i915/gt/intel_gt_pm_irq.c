// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_gt.h"
#include "intel_gt_irq.h"
#include "intel_gt_pm_irq.h"

static void write_pm_imr(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	u32 mask = gt->pm_imr;
	i915_reg_t reg;

	if (GRAPHICS_VER(i915) >= 11) {
		reg = GEN11_GPM_WGBOXPERF_INTR_MASK;
		mask <<= 16; /* pm is in upper half */
	} else if (GRAPHICS_VER(i915) >= 8) {
		reg = GEN8_GT_IMR(2);
	} else {
		reg = GEN6_PMIMR;
	}

	intel_uncore_write(uncore, reg, mask);
}

static void gen6_gt_pm_update_irq(struct intel_gt *gt,
				  u32 interrupt_mask,
				  u32 enabled_irq_mask)
{
	u32 new_val;

	WARN_ON(enabled_irq_mask & ~interrupt_mask);

	lockdep_assert_held(&gt->irq_lock);

	new_val = gt->pm_imr;
	new_val &= ~interrupt_mask;
	new_val |= ~enabled_irq_mask & interrupt_mask;

	if (new_val != gt->pm_imr) {
		gt->pm_imr = new_val;
		write_pm_imr(gt);
	}
}

void gen6_gt_pm_unmask_irq(struct intel_gt *gt, u32 mask)
{
	gen6_gt_pm_update_irq(gt, mask, mask);
}

void gen6_gt_pm_mask_irq(struct intel_gt *gt, u32 mask)
{
	gen6_gt_pm_update_irq(gt, mask, 0);
}

void gen6_gt_pm_reset_iir(struct intel_gt *gt, u32 reset_mask)
{
	struct intel_uncore *uncore = gt->uncore;
	i915_reg_t reg = GRAPHICS_VER(gt->i915) >= 8 ? GEN8_GT_IIR(2) : GEN6_PMIIR;

	lockdep_assert_held(&gt->irq_lock);

	intel_uncore_write(uncore, reg, reset_mask);
	intel_uncore_write(uncore, reg, reset_mask);
	intel_uncore_posting_read(uncore, reg);
}

static void write_pm_ier(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	u32 mask = gt->pm_ier;
	i915_reg_t reg;

	if (GRAPHICS_VER(i915) >= 11) {
		reg = GEN11_GPM_WGBOXPERF_INTR_ENABLE;
		mask <<= 16; /* pm is in upper half */
	} else if (GRAPHICS_VER(i915) >= 8) {
		reg = GEN8_GT_IER(2);
	} else {
		reg = GEN6_PMIER;
	}

	intel_uncore_write(uncore, reg, mask);
}

void gen6_gt_pm_enable_irq(struct intel_gt *gt, u32 enable_mask)
{
	lockdep_assert_held(&gt->irq_lock);

	gt->pm_ier |= enable_mask;
	write_pm_ier(gt);
	gen6_gt_pm_unmask_irq(gt, enable_mask);
}

void gen6_gt_pm_disable_irq(struct intel_gt *gt, u32 disable_mask)
{
	lockdep_assert_held(&gt->irq_lock);

	gt->pm_ier &= ~disable_mask;
	gen6_gt_pm_mask_irq(gt, disable_mask);
	write_pm_ier(gt);
}
