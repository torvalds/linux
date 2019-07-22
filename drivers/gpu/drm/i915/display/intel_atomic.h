/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_ATOMIC_H__
#define __INTEL_ATOMIC_H__

#include <linux/types.h>

struct drm_atomic_state;
struct drm_connector;
struct drm_connector_state;
struct drm_crtc;
struct drm_crtc_state;
struct drm_device;
struct drm_i915_private;
struct drm_property;
struct intel_crtc;
struct intel_crtc_state;

int intel_digital_connector_atomic_get_property(struct drm_connector *connector,
						const struct drm_connector_state *state,
						struct drm_property *property,
						u64 *val);
int intel_digital_connector_atomic_set_property(struct drm_connector *connector,
						struct drm_connector_state *state,
						struct drm_property *property,
						u64 val);
int intel_digital_connector_atomic_check(struct drm_connector *conn,
					 struct drm_atomic_state *state);
struct drm_connector_state *
intel_digital_connector_duplicate_state(struct drm_connector *connector);

struct drm_crtc_state *intel_crtc_duplicate_state(struct drm_crtc *crtc);
void intel_crtc_destroy_state(struct drm_crtc *crtc,
			       struct drm_crtc_state *state);
struct drm_atomic_state *intel_atomic_state_alloc(struct drm_device *dev);
void intel_atomic_state_clear(struct drm_atomic_state *state);

struct intel_crtc_state *
intel_atomic_get_crtc_state(struct drm_atomic_state *state,
			    struct intel_crtc *crtc);

int intel_atomic_setup_scalers(struct drm_i915_private *dev_priv,
			       struct intel_crtc *intel_crtc,
			       struct intel_crtc_state *crtc_state);

#endif /* __INTEL_ATOMIC_H__ */
