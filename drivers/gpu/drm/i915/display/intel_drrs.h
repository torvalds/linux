/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_DRRS_H__
#define __INTEL_DRRS_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_crtc_state;
struct intel_connector;
struct intel_dp;

void intel_drrs_enable(struct intel_dp *intel_dp,
		       const struct intel_crtc_state *crtc_state);
void intel_drrs_disable(struct intel_dp *intel_dp,
			const struct intel_crtc_state *crtc_state);
void intel_drrs_update(struct intel_dp *intel_dp,
		       const struct intel_crtc_state *crtc_state);
void intel_drrs_invalidate(struct drm_i915_private *dev_priv,
			   unsigned int frontbuffer_bits);
void intel_drrs_flush(struct drm_i915_private *dev_priv,
		      unsigned int frontbuffer_bits);
void intel_drrs_compute_config(struct intel_dp *intel_dp,
			       struct intel_crtc_state *pipe_config,
			       int output_bpp, bool constant_n);
struct drm_display_mode *intel_drrs_init(struct intel_connector *connector,
					 struct drm_display_mode *fixed_mode);

#endif /* __INTEL_DRRS_H__ */
