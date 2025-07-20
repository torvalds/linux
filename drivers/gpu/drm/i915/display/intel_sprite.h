/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_SPRITE_H__
#define __INTEL_SPRITE_H__

#include <linux/types.h>

struct intel_crtc_state;
struct intel_display;
struct intel_plane_state;
enum pipe;

#ifdef I915
struct intel_plane *intel_sprite_plane_create(struct intel_display *display,
					      enum pipe pipe, int plane);
int intel_plane_check_src_coordinates(struct intel_plane_state *plane_state);
int chv_plane_check_rotation(const struct intel_plane_state *plane_state);

int ivb_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);
int hsw_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);
int vlv_plane_min_cdclk(const struct intel_crtc_state *crtc_state,
			const struct intel_plane_state *plane_state);
#else
static inline struct intel_plane *intel_sprite_plane_create(struct intel_display *display,
							    int pipe, int plane)
{
	return NULL;
}
#endif

#endif /* __INTEL_SPRITE_H__ */
