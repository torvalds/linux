/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _I9XX_PLANE_H_
#define _I9XX_PLANE_H_

#include <linux/types.h>

enum pipe;
struct drm_framebuffer;
struct intel_crtc;
struct intel_display;
struct intel_initial_plane_config;
struct intel_plane;
struct intel_plane_state;

#ifdef I915
unsigned int i965_plane_max_stride(struct intel_plane *plane,
				   u32 pixel_format, u64 modifier,
				   unsigned int rotation);
unsigned int vlv_plane_min_alignment(struct intel_plane *plane,
				     const struct drm_framebuffer *fb,
				     int colot_plane);
int i9xx_check_plane_surface(struct intel_plane_state *plane_state);
u32 i965_plane_surf_offset(const struct intel_plane_state *plane_state);

struct intel_plane *
intel_primary_plane_create(struct intel_display *display, enum pipe pipe);

void i9xx_get_initial_plane_config(struct intel_crtc *crtc,
				   struct intel_initial_plane_config *plane_config);
bool i9xx_fixup_initial_plane_config(struct intel_crtc *crtc,
				     const struct intel_initial_plane_config *plane_config);
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
intel_primary_plane_create(struct intel_display *display, int pipe)
{
	return NULL;
}
static inline void i9xx_get_initial_plane_config(struct intel_crtc *crtc,
						 struct intel_initial_plane_config *plane_config)
{
}
static inline bool i9xx_fixup_initial_plane_config(struct intel_crtc *crtc,
						   const struct intel_initial_plane_config *plane_config)
{
	return false;
}
#endif

#endif
