/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_HOTPLUG_IRQ_H__
#define __INTEL_HOTPLUG_IRQ_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_encoder;

u32 i9xx_hpd_irq_ack(struct drm_i915_private *i915);

void i9xx_hpd_irq_handler(struct drm_i915_private *i915, u32 hotplug_status);
void ibx_hpd_irq_handler(struct drm_i915_private *i915, u32 hotplug_trigger);
void ilk_hpd_irq_handler(struct drm_i915_private *i915, u32 hotplug_trigger);
void gen11_hpd_irq_handler(struct drm_i915_private *i915, u32 iir);
void bxt_hpd_irq_handler(struct drm_i915_private *i915, u32 hotplug_trigger);
void xelpdp_pica_irq_handler(struct drm_i915_private *i915, u32 iir);
void icp_irq_handler(struct drm_i915_private *i915, u32 pch_iir);
void spt_irq_handler(struct drm_i915_private *i915, u32 pch_iir);

void i915_hotplug_interrupt_update_locked(struct drm_i915_private *i915,
					  u32 mask, u32 bits);
void i915_hotplug_interrupt_update(struct drm_i915_private *i915,
				   u32 mask, u32 bits);

void intel_hpd_enable_detection(struct intel_encoder *encoder);
void intel_hpd_irq_setup(struct drm_i915_private *i915);

void intel_hotplug_irq_init(struct drm_i915_private *i915);

#endif /* __INTEL_HOTPLUG_IRQ_H__ */
