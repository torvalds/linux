/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_IRQ_H__
#define __I915_IRQ_H__

#include <linux/ktime.h>
#include <linux/types.h>

#include "i915_reg_defs.h"

enum pipe;
struct drm_crtc;
struct drm_device;
struct drm_display_mode;
struct drm_i915_private;
struct intel_crtc;
struct intel_encoder;
struct intel_uncore;

void intel_irq_init(struct drm_i915_private *dev_priv);
void intel_irq_fini(struct drm_i915_private *dev_priv);
int intel_irq_install(struct drm_i915_private *dev_priv);
void intel_irq_uninstall(struct drm_i915_private *dev_priv);

void gen5_enable_gt_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen5_disable_gt_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen11_reset_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_reset_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_enable_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_disable_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_rps_reset_ei(struct drm_i915_private *dev_priv);
u32 gen6_sanitize_rps_pm_mask(const struct drm_i915_private *i915, u32 mask);

void intel_runtime_pm_disable_interrupts(struct drm_i915_private *dev_priv);
void intel_runtime_pm_enable_interrupts(struct drm_i915_private *dev_priv);
bool intel_irqs_enabled(struct drm_i915_private *dev_priv);
void intel_synchronize_irq(struct drm_i915_private *i915);
void intel_synchronize_hardirq(struct drm_i915_private *i915);

void gen3_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg);

void gen3_irq_reset(struct intel_uncore *uncore, i915_reg_t imr,
		    i915_reg_t iir, i915_reg_t ier);

void gen3_irq_init(struct intel_uncore *uncore,
		   i915_reg_t imr, u32 imr_val,
		   i915_reg_t ier, u32 ier_val,
		   i915_reg_t iir);

#define GEN8_IRQ_RESET_NDX(uncore, type, which) \
({ \
	unsigned int which_ = which; \
	gen3_irq_reset((uncore), GEN8_##type##_IMR(which_), \
		       GEN8_##type##_IIR(which_), GEN8_##type##_IER(which_)); \
})

#define GEN3_IRQ_RESET(uncore, type) \
	gen3_irq_reset((uncore), type##IMR, type##IIR, type##IER)

#define GEN8_IRQ_INIT_NDX(uncore, type, which, imr_val, ier_val) \
({ \
	unsigned int which_ = which; \
	gen3_irq_init((uncore), \
		      GEN8_##type##_IMR(which_), imr_val, \
		      GEN8_##type##_IER(which_), ier_val, \
		      GEN8_##type##_IIR(which_)); \
})

#define GEN3_IRQ_INIT(uncore, type, imr_val, ier_val) \
	gen3_irq_init((uncore), \
		      type##IMR, imr_val, \
		      type##IER, ier_val, \
		      type##IIR)

#endif /* __I915_IRQ_H__ */
