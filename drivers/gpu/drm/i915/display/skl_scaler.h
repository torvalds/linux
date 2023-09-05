/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */
#ifndef INTEL_SCALER_H
#define INTEL_SCALER_H

#include <linux/types.h>

enum drm_scaling_filter;
enum pipe;
struct drm_i915_private;
struct intel_crtc;
struct intel_crtc_state;
struct intel_plane;
struct intel_plane_state;

int skl_update_scaler_crtc(struct intel_crtc_state *crtc_state);

int skl_update_scaler_plane(struct intel_crtc_state *crtc_state,
			    struct intel_plane_state *plane_state);

int intel_atomic_setup_scalers(struct drm_i915_private *dev_priv,
			       struct intel_crtc *intel_crtc,
			       struct intel_crtc_state *crtc_state);

void skl_pfit_enable(const struct intel_crtc_state *crtc_state);

void skl_program_plane_scaler(struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state);
void skl_detach_scalers(const struct intel_crtc_state *crtc_state);
void skl_scaler_disable(const struct intel_crtc_state *old_crtc_state);

void skl_scaler_get_config(struct intel_crtc_state *crtc_state);

#endif
