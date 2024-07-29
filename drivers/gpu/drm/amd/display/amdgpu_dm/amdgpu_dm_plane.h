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
				 union dc_tiling_info *tiling_info,
				 struct plane_size *plane_size,
				 struct dc_plane_dcc_param *dcc,
				 struct dc_plane_address *address,
				 bool tmz_surface,
				 bool force_disable_dcc);

int amdgpu_dm_plane_init(struct amdgpu_display_manager *dm,
			 struct drm_plane *plane,
			 unsigned long possible_crtcs,
			 const struct dc_plane_cap *plane_cap);

const struct drm_format_info *amdgpu_dm_plane_get_format_info(const struct drm_mode_fb_cmd2 *cmd);

void amdgpu_dm_plane_fill_blending_from_plane_state(const struct drm_plane_state *plane_state,
				    bool *per_pixel_alpha, bool *pre_multiplied_alpha,
				    bool *global_alpha, int *global_alpha_value);

bool amdgpu_dm_plane_is_video_format(uint32_t format);
#endif
