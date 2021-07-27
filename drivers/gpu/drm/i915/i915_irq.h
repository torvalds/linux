/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_IRQ_H__
#define __I915_IRQ_H__

#include <linux/ktime.h>
#include <linux/types.h>

#include "display/intel_display.h"
#include "i915_reg.h"

struct drm_crtc;
struct drm_device;
struct drm_display_mode;
struct drm_i915_private;
struct intel_crtc;
struct intel_uncore;

void intel_irq_init(struct drm_i915_private *dev_priv);
void intel_irq_fini(struct drm_i915_private *dev_priv);
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

int intel_get_crtc_scanline(struct intel_crtc *crtc);
void gen8_irq_power_well_post_enable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask);
void gen8_irq_power_well_pre_disable(struct drm_i915_private *dev_priv,
				     u8 pipe_mask);
u32 gen8_de_pipe_underrun_mask(struct drm_i915_private *dev_priv);

bool intel_crtc_get_vblank_timestamp(struct drm_crtc *crtc, int *max_error,
				     ktime_t *vblank_time, bool in_vblank_irq);

u32 i915_get_vblank_counter(struct drm_crtc *crtc);
u32 g4x_get_vblank_counter(struct drm_crtc *crtc);

int i8xx_enable_vblank(struct drm_crtc *crtc);
int i915gm_enable_vblank(struct drm_crtc *crtc);
int i965_enable_vblank(struct drm_crtc *crtc);
int ilk_enable_vblank(struct drm_crtc *crtc);
int bdw_enable_vblank(struct drm_crtc *crtc);
void i8xx_disable_vblank(struct drm_crtc *crtc);
void i915gm_disable_vblank(struct drm_crtc *crtc);
void i965_disable_vblank(struct drm_crtc *crtc);
void ilk_disable_vblank(struct drm_crtc *crtc);
void bdw_disable_vblank(struct drm_crtc *crtc);

void gen2_irq_reset(struct intel_uncore *uncore);
void gen3_irq_reset(struct intel_uncore *uncore, i915_reg_t imr,
		    i915_reg_t iir, i915_reg_t ier);

void gen2_irq_init(struct intel_uncore *uncore,
		   u32 imr_val, u32 ier_val);
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

#define GEN2_IRQ_RESET(uncore) \
	gen2_irq_reset(uncore)

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

#define GEN2_IRQ_INIT(uncore, imr_val, ier_val) \
	gen2_irq_init((uncore), imr_val, ier_val)

#endif /* __I915_IRQ_H__ */
