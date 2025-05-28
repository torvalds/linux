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

void intel_irq_suspend(struct drm_i915_private *i915);
void intel_irq_resume(struct drm_i915_private *i915);
bool intel_irqs_enabled(struct drm_i915_private *dev_priv);
void intel_synchronize_irq(struct drm_i915_private *i915);
void intel_synchronize_hardirq(struct drm_i915_private *i915);

void gen2_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg);

void gen2_irq_reset(struct intel_uncore *uncore, struct i915_irq_regs regs);

void gen2_irq_init(struct intel_uncore *uncore, struct i915_irq_regs regs,
		   u32 imr_val, u32 ier_val);

void gen2_error_reset(struct intel_uncore *uncore, struct i915_error_regs regs);
void gen2_error_init(struct intel_uncore *uncore, struct i915_error_regs regs,
		     u32 emr_val);

#endif /* __I915_IRQ_H__ */
