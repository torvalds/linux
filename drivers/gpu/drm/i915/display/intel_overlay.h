/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_OVERLAY_H__
#define __INTEL_OVERLAY_H__

struct drm_device;
struct drm_file;
struct drm_i915_error_state_buf;
struct drm_i915_private;
struct intel_overlay;
struct intel_overlay_error_state;

void intel_overlay_setup(struct drm_i915_private *dev_priv);
void intel_overlay_cleanup(struct drm_i915_private *dev_priv);
int intel_overlay_switch_off(struct intel_overlay *overlay);
int intel_overlay_put_image_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
int intel_overlay_attrs_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
void intel_overlay_reset(struct drm_i915_private *dev_priv);
struct intel_overlay_error_state *
intel_overlay_capture_error_state(struct drm_i915_private *dev_priv);
void intel_overlay_print_error_state(struct drm_i915_error_state_buf *e,
				     struct intel_overlay_error_state *error);

#endif /* __INTEL_OVERLAY_H__ */
