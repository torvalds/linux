/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SPRITE_H__
#define __INTEL_SPRITE_H__

#include <linux/types.h>

struct drm_device;
struct drm_display_mode;
struct drm_file;
struct drm_i915_private;
struct intel_crtc_state;
struct intel_plane_state;
enum pipe;

struct intel_plane *intel_sprite_plane_create(struct drm_i915_private *dev_priv,
					      enum pipe pipe, int plane);
int intel_sprite_set_colorkey_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int intel_plane_check_src_coordinates(struct intel_plane_state *plane_state);
int chv_plane_check_rotation(const struct intel_plane_state *plane_state);

int ivb_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);
int hsw_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);
int vlv_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);

#endif /* __INTEL_SPRITE_H__ */
