/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _I9XX_PLANE_H_
#define _I9XX_PLANE_H_

#include <linux/types.h>

enum pipe;
struct drm_i915_private;
struct intel_crtc;
struct intel_initial_plane_config;
struct intel_plane;
struct intel_plane_state;

#ifdef I915
unsigned int i965_plane_max_stride(struct intel_plane *plane,
				   u32 pixel_format, u64 modifier,
				   unsigned int rotation);
int i9xx_check_plane_surface(struct intel_plane_state *plane_state);

struct intel_plane *
intel_primary_plane_create(struct drm_i915_private *dev_priv, enum pipe pipe);

void i9xx_get_initial_plane_config(struct intel_crtc *crtc,
				   struct intel_initial_plane_config *plane_config);
#else
static inline unsigned int i965_plane_max_stride(struct intel_plane *plane,
						 u32 pixel_format, u64 modifier,
						 unsigned int rotation)
{
	return 0;
}
static inline int i9xx_check_plane_surface(struct intel_plane_state *plane_state)
{
	return 0;
}
static inline struct intel_plane *
intel_primary_plane_create(struct drm_i915_private *dev_priv, int pipe)
{
	return NULL;
}
static inline void i9xx_get_initial_plane_config(struct intel_crtc *crtc,
						 struct intel_initial_plane_config *plane_config)
{
}
#endif

#endif
