/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_IRQ_H__
#define __I915_IRQ_H__

#include <linux/types.h>

#include "i915_drv.h"

struct drm_i915_private;
struct intel_crtc;

extern void intel_irq_init(struct drm_i915_private *dev_priv);
extern void intel_irq_fini(struct drm_i915_private *dev_priv);
int intel_irq_install(struct drm_i915_private *dev_priv);
void intel_irq_uninstall(struct drm_i915_private *dev_priv);

u32 i915_pipestat_enable_mask(struct drm_i915_private *dev_priv,
			      enum pipe pipe);
void
i915_enable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		     u32 status_mask);

void
i915_disable_pipestat(struct drm_i915_private *dev_priv, enum pipe pipe,
		      u32 status_mask);

void valleyview_enable_display_irqs(struct drm_i915_private *dev_priv);
void valleyview_disable_display_irqs(struct drm_i915_private *dev_priv);

void i915_hotplug_interrupt_update(struct drm_i915_private *dev_priv,
				   u32 mask,
				   u32 bits);
void ilk_update_display_irq(struct drm_i915_private *dev_priv,
			    u32 interrupt_mask,
			    u32 enabled_irq_mask);
static inline void
ilk_enable_display_irq(struct drm_i915_private *dev_priv, u32 bits)
{
	ilk_update_display_irq(dev_priv, bits, bits);
}
static inline void
ilk_disable_display_irq(struct drm_i915_private *dev_priv, u32 bits)
{
	ilk_update_display_irq(dev_priv, bits, 0);
}
void bdw_update_pipe_irq(struct drm_i915_private *dev_priv,
			 enum pipe pipe,
			 u32 interrupt_mask,
			 u32 enabled_irq_mask);
static inline void bdw_enable_pipe_irq(struct drm_i915_private *dev_priv,
				       enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(dev_priv, pipe, bits, bits);
}
static inline void bdw_disable_pipe_irq(struct drm_i915_private *dev_priv,
					enum pipe pipe, u32 bits)
{
	bdw_update_pipe_irq(dev_priv, pipe, bits, 0);
}
void ibx_display_interrupt_update(struct drm_i915_private *dev_priv,
				  u32 interrupt_mask,
				  u32 enabled_irq_mask);
static inline void
ibx_enable_display_interrupt(struct drm_i915_private *dev_priv, u32 bits)
{
	ibx_display_interrupt_update(dev_priv, bits, bits);
}
static inline void
ibx_disable_display_interrupt(struct drm_i915_private *dev_priv, u32 bits)
{
	ibx_display_interrupt_update(dev_priv, bits, 0);
}

void gen5_enable_gt_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen5_disable_gt_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen6_mask_pm_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen6_unmask_pm_irq(struct drm_i915_private *dev_priv, u32 mask);
void gen11_reset_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_reset_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_enable_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_disable_rps_interrupts(struct drm_i915_private *dev_priv);
void gen6_rps_reset_ei(struct drm_i915_private *dev_priv);

static inline u32 gen6_sanitize_rps_pm_mask(const struct drm_i915_private *i915,
					    u32 mask)
{
	return mask & ~i915->gt_pm.rps.pm_intrmsk_mbz;
}

void intel_runtime_pm_disable_interrupts(struct drm_i915_private *dev_priv);
void intel_runtime_pm_enable_interrupts(struct drm_i915_private *dev_priv);
static inline bool intel_irqs_enabled(struct drm_i915_private *dev_priv)
{
	/*
	 * We only use drm_irq_uninstall() at unload and VT switch, so
	 * this is the only thing we need to check.
	 */
	return dev_priv->runtime_pm.irqs_enabled;
}

int intel_get_crtc_scanline(struct intel_crtc *crtc);
void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask);
void gen8_irq_power_well_pre_disable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask);
void gen9_reset_guc_interrupts(struct drm_i915_private *dev_priv);
void gen9_enable_guc_interrupts(struct drm_i915_private *dev_priv);
void gen9_disable_guc_interrupts(struct drm_i915_private *dev_priv);
void gen11_reset_guc_interrupts(struct drm_i915_private *i915);
void gen11_enable_guc_interrupts(struct drm_i915_private *i915);
void gen11_disable_guc_interrupts(struct drm_i915_private *i915);

u32 i915_get_vblank_counter(struct drm_crtc *crtc);
u32 g4x_get_vblank_counter(struct drm_crtc *crtc);

int i8xx_enable_vblank(struct drm_crtc *crtc);
int i945gm_enable_vblank(struct drm_crtc *crtc);
int i965_enable_vblank(struct drm_crtc *crtc);
int ilk_enable_vblank(struct drm_crtc *crtc);
int bdw_enable_vblank(struct drm_crtc *crtc);
void i8xx_disable_vblank(struct drm_crtc *crtc);
void i945gm_disable_vblank(struct drm_crtc *crtc);
void i965_disable_vblank(struct drm_crtc *crtc);
void ilk_disable_vblank(struct drm_crtc *crtc);
void bdw_disable_vblank(struct drm_crtc *crtc);

#endif /* __I915_IRQ_H__ */
