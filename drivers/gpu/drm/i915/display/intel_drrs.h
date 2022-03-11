/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_DRRS_H__
#define __INTEL_DRRS_H__

#include <linux/types.h>

enum drrs_type;
struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_connector;

const char *intel_drrs_type_str(enum drrs_type drrs_type);
bool intel_drrs_is_enabled(struct intel_crtc *crtc);
void intel_drrs_enable(const struct intel_crtc_state *crtc_state);
void intel_drrs_disable(const struct intel_crtc_state *crtc_state);
void intel_drrs_update(struct intel_atomic_state *state,
		       struct intel_crtc *crtc);
void intel_drrs_invalidate(struct drm_i915_private *dev_priv,
			   unsigned int frontbuffer_bits);
void intel_drrs_flush(struct drm_i915_private *dev_priv,
		      unsigned int frontbuffer_bits);
void intel_drrs_page_flip(struct intel_crtc *crtc);
void intel_drrs_compute_config(struct intel_connector *connector,
			       struct intel_crtc_state *pipe_config,
			       int output_bpp, bool constant_n);
void intel_crtc_drrs_init(struct intel_crtc *crtc);
struct drm_display_mode *intel_drrs_init(struct intel_connector *connector,
					 const struct drm_display_mode *fixed_mode);

#endif /* __INTEL_DRRS_H__ */
