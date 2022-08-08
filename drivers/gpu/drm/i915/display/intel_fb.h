/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_FB_H__
#define __INTEL_FB_H__

#include <linux/types.h>

struct drm_framebuffer;

struct drm_i915_private;

struct intel_fb_view;
struct intel_framebuffer;
struct intel_plane_state;

bool is_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_plane(const struct drm_framebuffer *fb, int plane);
bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int plane);
bool is_semiplanar_uv_plane(const struct drm_framebuffer *fb, int color_plane);

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane);

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane);
int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane);
int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane);

unsigned int intel_tile_size(const struct drm_i915_private *i915);
unsigned int intel_tile_height(const struct drm_framebuffer *fb, int color_plane);
unsigned int intel_tile_row_size(const struct drm_framebuffer *fb, int color_plane);

unsigned int intel_cursor_alignment(const struct drm_i915_private *i915);

void intel_fb_plane_get_subsampling(int *hsub, int *vsub,
				    const struct drm_framebuffer *fb,
				    int color_plane);

u32 intel_plane_adjust_aligned_offset(int *x, int *y,
				      const struct intel_plane_state *state,
				      int color_plane,
				      u32 old_offset, u32 new_offset);
u32 intel_plane_compute_aligned_offset(int *x, int *y,
				       const struct intel_plane_state *state,
				       int color_plane);

bool intel_fb_needs_pot_stride_remap(const struct intel_framebuffer *fb);
bool intel_fb_supports_90_270_rotation(const struct intel_framebuffer *fb);

int intel_fill_fb_info(struct drm_i915_private *i915, struct intel_framebuffer *fb);
void intel_fb_fill_view(const struct intel_framebuffer *fb, unsigned int rotation,
			struct intel_fb_view *view);
int intel_plane_compute_gtt(struct intel_plane_state *plane_state);

#endif /* __INTEL_FB_H__ */
