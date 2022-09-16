/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_HOTPLUG_H__
#define __INTEL_HOTPLUG_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_connector;
struct intel_digital_port;
struct intel_encoder;
enum port;

void intel_hpd_poll_enable(struct drm_i915_private *dev_priv);
void intel_hpd_poll_disable(struct drm_i915_private *dev_priv);
enum intel_hotplug_state intel_encoder_hotplug(struct intel_encoder *encoder,
					       struct intel_connector *connector);
void intel_hpd_irq_handler(struct drm_i915_private *dev_priv,
			   u32 pin_mask, u32 long_mask);
void intel_hpd_trigger_irq(struct intel_digital_port *dig_port);
void intel_hpd_init(struct drm_i915_private *dev_priv);
void intel_hpd_init_early(struct drm_i915_private *i915);
void intel_hpd_cancel_work(struct drm_i915_private *dev_priv);
enum hpd_pin intel_hpd_pin_default(struct drm_i915_private *dev_priv,
				   enum port port);
bool intel_hpd_disable(struct drm_i915_private *dev_priv, enum hpd_pin pin);
void intel_hpd_enable(struct drm_i915_private *dev_priv, enum hpd_pin pin);
void intel_hpd_debugfs_register(struct drm_i915_private *i915);

#endif /* __INTEL_HOTPLUG_H__ */
