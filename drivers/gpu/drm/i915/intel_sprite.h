/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SPRITE_H__
#define __INTEL_SPRITE_H__

#include <linux/types.h>

#include "i915_drv.h"
#include "intel_display.h"

struct drm_device;
struct drm_display_mode;
struct drm_file;
struct drm_i915_private;
struct intel_crtc_state;
struct intel_plane_state;

bool is_planar_yuv_format(u32 pixelformat);
int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs);
struct intel_plane *intel_sprite_plane_create(struct drm_i915_private *dev_priv,
					      enum pipe pipe, int plane);
int intel_sprite_set_colorkey_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
void intel_pipe_update_start(const struct intel_crtc_state *new_crtc_state);
void intel_pipe_update_end(struct intel_crtc_state *new_crtc_state);
int intel_plane_check_stride(const struct intel_plane_state *plane_state);
int intel_plane_check_src_coordinates(struct intel_plane_state *plane_state);
int chv_plane_check_rotation(const struct intel_plane_state *plane_state);
struct intel_plane *
skl_universal_plane_create(struct drm_i915_private *dev_priv,
			   enum pipe pipe, enum plane_id plane_id);

static inline bool icl_is_nv12_y_plane(enum plane_id id)
{
	/* Don't need to do a gen check, these planes are only available on gen11 */
	if (id == PLANE_SPRITE4 || id == PLANE_SPRITE5)
		return true;

	return false;
}

static inline bool icl_is_hdr_plane(struct drm_i915_private *dev_priv,
				    enum plane_id plane_id)
{
	if (INTEL_GEN(dev_priv) < 11)
		return false;

	return plane_id < PLANE_SPRITE2;
}

#endif /* __INTEL_SPRITE_H__ */
