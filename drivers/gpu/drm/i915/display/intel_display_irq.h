/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_IRQ_H__
#define __INTEL_DISPLAY_IRQ_H__

#include <linux/types.h>

#include "intel_display_limits.h"

enum pipe;
struct drm_i915_private;
struct drm_crtc;

void valleyview_enable_display_irqs(struct drm_i915_private *i915);
void valleyview_disable_display_irqs(struct drm_i915_private *i915);

void ilk_update_display_irq(struct drm_i915_private *i915,
			    u32 interrupt_mask, u32 enabled_irq_mask);
void ilk_enable_display_irq(struct drm_i915_private *i915, u32 bits);
void ilk_disable_display_irq(struct drm_i915_private *i915, u32 bits);

void bdw_update_port_irq(struct drm_i915_private *i915, u32 interrupt_mask, u32 enabled_irq_mask);
void bdw_enable_pipe_irq(struct drm_i915_private *i915, enum pipe pipe, u32 bits);
void bdw_disable_pipe_irq(struct drm_i915_private *i915, enum pipe pipe, u32 bits);

void ibx_display_interrupt_update(struct drm_i915_private *i915,
				  u32 interrupt_mask, u32 enabled_irq_mask);
void ibx_enable_display_interrupt(struct drm_i915_private *i915, u32 bits);
void ibx_disable_display_interrupt(struct drm_i915_private *i915, u32 bits);

void gen8_irq_power_well_post_enable(struct drm_i915_private *i915, u8 pipe_mask);
void gen8_irq_power_well_pre_disable(struct drm_i915_private *i915, u8 pipe_mask);
u32 gen8_de_pipe_underrun_mask(struct drm_i915_private *i915);

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

void ivb_display_irq_handler(struct drm_i915_private *i915, u32 de_iir);
void ilk_display_irq_handler(struct drm_i915_private *i915, u32 de_iir);
void gen8_de_irq_handler(struct drm_i915_private *i915, u32 master_ctl);
void gen11_display_irq_handler(struct drm_i915_private *i915);

u32 gen11_gu_misc_irq_ack(struct drm_i915_private *i915, const u32 master_ctl);
void gen11_gu_misc_irq_handler(struct drm_i915_private *i915, const u32 iir);

void vlv_display_irq_reset(struct drm_i915_private *i915);
void gen8_display_irq_reset(struct drm_i915_private *i915);
void gen11_display_irq_reset(struct drm_i915_private *i915);

void vlv_display_irq_postinstall(struct drm_i915_private *i915);
void ilk_de_irq_postinstall(struct drm_i915_private *i915);
void gen8_de_irq_postinstall(struct drm_i915_private *i915);
void gen11_de_irq_postinstall(struct drm_i915_private *i915);
void dg1_de_irq_postinstall(struct drm_i915_private *i915);

u32 i915_pipestat_enable_mask(struct drm_i915_private *i915, enum pipe pipe);
void i915_enable_pipestat(struct drm_i915_private *i915, enum pipe pipe, u32 status_mask);
void i915_disable_pipestat(struct drm_i915_private *i915, enum pipe pipe, u32 status_mask);
void i915_enable_asle_pipestat(struct drm_i915_private *i915);
void i9xx_pipestat_irq_reset(struct drm_i915_private *i915);

void i9xx_pipestat_irq_ack(struct drm_i915_private *i915, u32 iir, u32 pipe_stats[I915_MAX_PIPES]);

void i915_pipestat_irq_handler(struct drm_i915_private *i915, u32 iir, u32 pipe_stats[I915_MAX_PIPES]);
void i965_pipestat_irq_handler(struct drm_i915_private *i915, u32 iir, u32 pipe_stats[I915_MAX_PIPES]);
void valleyview_pipestat_irq_handler(struct drm_i915_private *i915, u32 pipe_stats[I915_MAX_PIPES]);
void i8xx_pipestat_irq_handler(struct drm_i915_private *i915, u16 iir, u32 pipe_stats[I915_MAX_PIPES]);

void intel_display_irq_init(struct drm_i915_private *i915);

#endif /* __INTEL_DISPLAY_IRQ_H__ */
