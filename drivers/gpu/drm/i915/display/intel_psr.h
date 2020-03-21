/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PSR_H__
#define __INTEL_PSR_H__

#include "intel_frontbuffer.h"

struct drm_connector;
struct drm_connector_state;
struct drm_i915_private;
struct intel_crtc_state;
struct intel_dp;

#define CAN_PSR(dev_priv) (HAS_PSR(dev_priv) && dev_priv->psr.sink_support)
void intel_psr_init_dpcd(struct intel_dp *intel_dp);
void intel_psr_enable(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state);
void intel_psr_disable(struct intel_dp *intel_dp,
		       const struct intel_crtc_state *old_crtc_state);
void intel_psr_update(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state);
int intel_psr_debug_set(struct drm_i915_private *dev_priv, u64 value);
void intel_psr_invalidate(struct drm_i915_private *dev_priv,
			  unsigned frontbuffer_bits,
			  enum fb_op_origin origin);
void intel_psr_flush(struct drm_i915_private *dev_priv,
		     unsigned frontbuffer_bits,
		     enum fb_op_origin origin);
void intel_psr_init(struct drm_i915_private *dev_priv);
void intel_psr_compute_config(struct intel_dp *intel_dp,
			      struct intel_crtc_state *crtc_state);
void intel_psr_irq_handler(struct drm_i915_private *dev_priv, u32 psr_iir);
void intel_psr_short_pulse(struct intel_dp *intel_dp);
int intel_psr_wait_for_idle(const struct intel_crtc_state *new_crtc_state,
			    u32 *out_value);
bool intel_psr_enabled(struct intel_dp *intel_dp);
void intel_psr_atomic_check(struct drm_connector *connector,
			    struct drm_connector_state *old_state,
			    struct drm_connector_state *new_state);

#endif /* __INTEL_PSR_H__ */
