/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_IRQ_H__
#define __INTEL_DISPLAY_IRQ_H__

#include <linux/types.h>

#include "intel_display_limits.h"

enum pipe;
struct drm_crtc;
struct drm_printer;
struct intel_display;
struct intel_display_irq_snapshot;

void valleyview_enable_display_irqs(struct intel_display *display);
void valleyview_disable_display_irqs(struct intel_display *display);

void ilk_update_display_irq(struct intel_display *display,
			    u32 interrupt_mask, u32 enabled_irq_mask);
void ilk_enable_display_irq(struct intel_display *display, u32 bits);
void ilk_disable_display_irq(struct intel_display *display, u32 bits);

void bdw_update_port_irq(struct intel_display *display, u32 interrupt_mask, u32 enabled_irq_mask);
void bdw_enable_pipe_irq(struct intel_display *display, enum pipe pipe, u32 bits);
void bdw_disable_pipe_irq(struct intel_display *display, enum pipe pipe, u32 bits);

void ibx_display_interrupt_update(struct intel_display *display,
				  u32 interrupt_mask, u32 enabled_irq_mask);
void ibx_enable_display_interrupt(struct intel_display *display, u32 bits);
void ibx_disable_display_interrupt(struct intel_display *display, u32 bits);

void gen8_irq_power_well_post_enable(struct intel_display *display, u8 pipe_mask);
void gen8_irq_power_well_pre_disable(struct intel_display *display, u8 pipe_mask);

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

void ivb_display_irq_handler(struct intel_display *display, u32 de_iir);
void ilk_display_irq_handler(struct intel_display *display, u32 de_iir);
void gen8_de_irq_handler(struct intel_display *display, u32 master_ctl);
void gen11_display_irq_handler(struct intel_display *display);

u32 gen11_gu_misc_irq_ack(struct intel_display *display, const u32 master_ctl);
void gen11_gu_misc_irq_handler(struct intel_display *display, const u32 iir);

void i9xx_display_irq_reset(struct intel_display *display);
void ibx_display_irq_reset(struct intel_display *display);
void vlv_display_irq_reset(struct intel_display *display);
void gen8_display_irq_reset(struct intel_display *display);
void gen11_display_irq_reset(struct intel_display *display);

void i915_display_irq_postinstall(struct intel_display *display);
void i965_display_irq_postinstall(struct intel_display *display);
void vlv_display_irq_postinstall(struct intel_display *display);
void ilk_de_irq_postinstall(struct intel_display *display);
void gen8_de_irq_postinstall(struct intel_display *display);
void gen11_de_irq_postinstall(struct intel_display *display);
void dg1_de_irq_postinstall(struct intel_display *display);

u32 i915_pipestat_enable_mask(struct intel_display *display, enum pipe pipe);
void i915_enable_pipestat(struct intel_display *display, enum pipe pipe, u32 status_mask);
void i915_disable_pipestat(struct intel_display *display, enum pipe pipe, u32 status_mask);

void i9xx_pipestat_irq_ack(struct intel_display *display, u32 iir, u32 pipe_stats[I915_MAX_PIPES]);

void i915_pipestat_irq_handler(struct intel_display *display, u32 iir, u32 pipe_stats[I915_MAX_PIPES]);
void i965_pipestat_irq_handler(struct intel_display *display, u32 iir, u32 pipe_stats[I915_MAX_PIPES]);
void valleyview_pipestat_irq_handler(struct intel_display *display, u32 pipe_stats[I915_MAX_PIPES]);

void vlv_display_error_irq_ack(struct intel_display *display, u32 *eir, u32 *dpinvgtt);
void vlv_display_error_irq_handler(struct intel_display *display, u32 eir, u32 dpinvgtt);

void intel_display_irq_init(struct intel_display *display);

void i915gm_irq_cstate_wa(struct intel_display *display, bool enable);

struct intel_display_irq_snapshot *intel_display_irq_snapshot_capture(struct intel_display *display);
void intel_display_irq_snapshot_print(const struct intel_display_irq_snapshot *snapshot, struct drm_printer *p);

#endif /* __INTEL_DISPLAY_IRQ_H__ */
