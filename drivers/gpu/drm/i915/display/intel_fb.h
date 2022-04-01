/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_FB_H__
#define __INTEL_FB_H__

#include <linux/bits.h>
#include <linux/types.h>

struct drm_device;
struct drm_file;
struct drm_framebuffer;
struct drm_i915_gem_object;
struct drm_i915_private;
struct drm_mode_fb_cmd2;
struct intel_fb_view;
struct intel_framebuffer;
struct intel_plane;
struct intel_plane_state;

#define INTEL_PLANE_CAP_NONE		0
#define INTEL_PLANE_CAP_CCS_RC		BIT(0)
#define INTEL_PLANE_CAP_CCS_RC_CC	BIT(1)
#define INTEL_PLANE_CAP_CCS_MC		BIT(2)
#define INTEL_PLANE_CAP_TILING_X	BIT(3)
#define INTEL_PLANE_CAP_TILING_Y	BIT(4)
#define INTEL_PLANE_CAP_TILING_Yf	BIT(5)

bool intel_fb_is_ccs_modifier(u64 modifier);
bool intel_fb_is_rc_ccs_cc_modifier(u64 modifier);
bool intel_fb_is_mc_ccs_modifier(u64 modifier);

bool intel_fb_is_ccs_aux_plane(const struct drm_framebuffer *fb, int color_plane);
int intel_fb_rc_ccs_cc_plane(const struct drm_framebuffer *fb);

u64 *intel_fb_plane_get_modifiers(struct drm_i915_private *i915,
				  u8 plane_caps);
bool intel_fb_plane_supports_modifier(struct intel_plane *plane, u64 modifier);

const struct drm_format_info *
intel_fb_get_format_info(const struct drm_mode_fb_cmd2 *cmd);

bool
intel_format_info_is_yuv_semiplanar(const struct drm_format_info *info,
				    u64 modifier);

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane);

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane);
int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane);
int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane);

unsigned int intel_tile_size(const struct drm_i915_private *i915);
unsigned int intel_tile_width_bytes(const struct drm_framebuffer *fb, int color_plane);
unsigned int intel_tile_height(const struct drm_framebuffer *fb, int color_plane);
unsigned int intel_tile_row_size(const struct drm_framebuffer *fb, int color_plane);
unsigned int intel_fb_align_height(const struct drm_framebuffer *fb,
				   int color_plane, unsigned int height);
unsigned int intel_cursor_alignment(const struct drm_i915_private *i915);
unsigned int intel_surf_alignment(const struct drm_framebuffer *fb,
				  int color_plane);

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

int intel_framebuffer_init(struct intel_framebuffer *ifb,
			   struct drm_i915_gem_object *obj,
			   struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *
intel_user_framebuffer_create(struct drm_device *dev,
			      struct drm_file *filp,
			      const struct drm_mode_fb_cmd2 *user_mode_cmd);

bool intel_fb_uses_dpt(const struct drm_framebuffer *fb);

#endif /* __INTEL_FB_H__ */
