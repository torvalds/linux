/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FBC_H__
#define __INTEL_FBC_H__

#include <linux/types.h>

#include "intel_frontbuffer.h"

struct drm_i915_private;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_fbc;
struct intel_plane_state;

int intel_fbc_atomic_check(struct intel_atomic_state *state);
bool intel_fbc_is_active(struct intel_fbc *fbc);
bool intel_fbc_is_compressing(struct intel_fbc *fbc);
bool intel_fbc_pre_update(struct intel_atomic_state *state,
			  struct intel_crtc *crtc);
void intel_fbc_post_update(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);
void intel_fbc_init(struct drm_i915_private *dev_priv);
void intel_fbc_cleanup(struct drm_i915_private *dev_priv);
void intel_fbc_update(struct intel_atomic_state *state,
		      struct intel_crtc *crtc);
void intel_fbc_disable(struct intel_crtc *crtc);
void intel_fbc_global_disable(struct drm_i915_private *dev_priv);
void intel_fbc_invalidate(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin);
void intel_fbc_flush(struct drm_i915_private *dev_priv,
		     unsigned int frontbuffer_bits, enum fb_op_origin origin);
void intel_fbc_handle_fifo_underrun_irq(struct drm_i915_private *i915);
void intel_fbc_reset_underrun(struct drm_i915_private *i915);
int intel_fbc_set_false_color(struct intel_fbc *fbc, bool enable);

#endif /* __INTEL_FBC_H__ */
