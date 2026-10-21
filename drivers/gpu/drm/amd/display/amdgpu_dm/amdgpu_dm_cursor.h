/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_DM_CURSOR_H__
#define __AMDGPU_DM_CURSOR_H__

int amdgpu_dm_check_native_cursor_state(struct drm_crtc *new_plane_crtc,
					struct drm_plane *plane,
					struct drm_plane_state *new_plane_state,
					bool enable);

bool amdgpu_dm_should_update_native_cursor(struct drm_atomic_commit *state,
					   struct drm_crtc *old_plane_crtc,
					   struct drm_crtc *new_plane_crtc,
					   bool enable);

int amdgpu_dm_crtc_get_cursor_mode(struct amdgpu_device *adev,
				   struct drm_atomic_commit *state,
				   struct dm_crtc_state *dm_crtc_state,
				   enum amdgpu_dm_cursor_mode *cursor_mode);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
void dm_get_oriented_plane_size(struct drm_plane_state *plane_state,
				int *src_w, int *src_h);
void dm_get_plane_scale(struct drm_plane_state *plane_state,
			int *out_plane_scale_w, int *out_plane_scale_h);
#endif /* CONFIG_DRM_AMD_DC_KUNIT_TEST */

#endif /* __AMDGPU_DM_CURSOR_H__ */
