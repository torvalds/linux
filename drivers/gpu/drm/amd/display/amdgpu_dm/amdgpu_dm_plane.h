// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __AMDGPU_DM_PLANE_H__
#define __AMDGPU_DM_PLANE_H__

#include "dc.h"
#include <drm/drm_plane.h>
#include "amdgpu.h"

int amdgpu_dm_plane_get_cursor_position(struct drm_plane *plane, struct drm_crtc *crtc,
					struct dc_cursor_position *position);

void amdgpu_dm_plane_handle_cursor_update(struct drm_plane *plane,
			  struct drm_plane_state *old_plane_state);

int amdgpu_dm_plane_fill_dc_scaling_info(struct amdgpu_device *adev,
			 const struct drm_plane_state *state,
			 struct dc_scaling_info *scaling_info);

int amdgpu_dm_plane_helper_check_state(struct drm_plane_state *state,
				struct drm_crtc_state *new_crtc_state);

int amdgpu_dm_plane_fill_plane_buffer_attributes(struct amdgpu_device *adev,
				 const struct amdgpu_framebuffer *afb,
				 const enum surface_pixel_format format,
				 const enum dc_rotation_angle rotation,
				 const uint64_t tiling_flags,
				 struct dc_tiling_info *tiling_info,
				 struct plane_size *plane_size,
				 struct dc_plane_dcc_param *dcc,
				 struct dc_plane_address *address,
				 bool tmz_surface);

int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
			 struct drm_plane *plane,
			 unsigned long possible_crtcs,
			 const struct dc_plane_cap *plane_cap);

const struct drm_format_info *amdgpu_dm_plane_get_format_info(u32 pixel_format, u64 modifier);

void amdgpu_dm_plane_fill_blending_from_plane_state(const struct drm_plane_state *plane_state,
				    bool *per_pixel_alpha, bool *pre_multiplied_alpha,
				    bool *global_alpha, int *global_alpha_value);

bool amdgpu_dm_plane_is_video_format(uint32_t format);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
void amdgpu_dm_plane_add_modifier(uint64_t **mods, uint64_t *size,
				  uint64_t *cap, uint64_t mod);
void amdgpu_dm_plane_fill_gfx8_tiling_info_from_flags(struct dc_tiling_info *tiling_info,
						      uint64_t tiling_flags);
void amdgpu_dm_plane_fill_gfx9_tiling_info_from_device(const struct amdgpu_device *adev,
						       struct dc_tiling_info *tiling_info);
void amdgpu_dm_plane_fill_gfx9_tiling_info_from_modifier(const struct amdgpu_device *adev,
							 struct dc_tiling_info *tiling_info,
							 uint64_t modifier);
int amdgpu_dm_plane_validate_dcc(struct amdgpu_device *adev,
				 const enum surface_pixel_format format,
				 const enum dc_rotation_angle rotation,
				 const struct dc_tiling_info *tiling_info,
				 const struct dc_plane_dcc_param *dcc,
				 const struct dc_plane_address *address,
				 const struct plane_size *plane_size);
bool amdgpu_dm_plane_modifier_has_dcc(uint64_t modifier);
unsigned int amdgpu_dm_plane_modifier_gfx9_swizzle_mode(uint64_t modifier);
int amdgpu_dm_plane_get_plane_modifiers(struct amdgpu_device *adev,
					unsigned int plane_type, uint64_t **mods);
int amdgpu_dm_plane_get_plane_formats(const struct drm_plane *plane,
				      const struct dc_plane_cap *plane_cap,
				      uint32_t *formats, int max_formats);
int amdgpu_dm_plane_fill_gfx9_attrs_from_modifiers(struct amdgpu_device *adev,
							      const struct amdgpu_framebuffer *afb,
							      const enum surface_pixel_format format,
							      const enum dc_rotation_angle rotation,
							      const struct plane_size *plane_size,
							      struct dc_tiling_info *tiling_info,
							      struct dc_plane_dcc_param *dcc,
							      struct dc_plane_address *address);
int amdgpu_dm_plane_fill_gfx12_attrs_from_modifiers(struct amdgpu_device *adev,
							       const struct amdgpu_framebuffer *afb,
							       const enum surface_pixel_format format,
							       const enum dc_rotation_angle rotation,
							       const struct plane_size *plane_size,
							       struct dc_tiling_info *tiling_info,
							       struct dc_plane_dcc_param *dcc,
							       struct dc_plane_address *address);
bool amdgpu_dm_plane_format_mod_supported(struct drm_plane *plane,
					  uint32_t format,
					  uint64_t modifier);
void amdgpu_dm_plane_get_min_max_dc_plane_scaling(struct drm_device *dev,
						  struct drm_framebuffer *fb,
						  int *min_downscale,
						  int *max_upscale);
int amdgpu_dm_plane_atomic_async_check(struct drm_plane *plane,
				       struct drm_atomic_commit *state, bool flip);
int amdgpu_dm_plane_atomic_check(struct drm_plane *plane,
				 struct drm_atomic_commit *state);
void amdgpu_dm_plane_panic_flush(struct drm_plane *plane);
void amdgpu_dm_plane_drm_plane_reset(struct drm_plane *plane);
struct drm_plane_state *amdgpu_dm_plane_drm_plane_duplicate_state(struct drm_plane *plane);
void amdgpu_dm_plane_drm_plane_destroy_state(struct drm_plane *plane,
					     struct drm_plane_state *state);
#endif
#endif
