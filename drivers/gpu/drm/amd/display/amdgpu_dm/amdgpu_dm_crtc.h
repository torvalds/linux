/* SPDX-License-Identifier: MIT */
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

#ifndef __AMDGPU_DM_CRTC_H__
#define __AMDGPU_DM_CRTC_H__

void amdgpu_dm_crtc_handle_vblank(struct amdgpu_crtc *acrtc);

bool amdgpu_dm_crtc_modeset_required(struct drm_crtc_state *crtc_state,
		      struct dc_stream_state *new_stream,
		      struct dc_stream_state *old_stream);

int amdgpu_dm_crtc_set_vupdate_irq(struct drm_crtc *crtc, bool enable);

bool amdgpu_dm_crtc_vrr_active_irq(struct amdgpu_crtc *acrtc);

bool amdgpu_dm_crtc_vrr_active(const struct dm_crtc_state *dm_state);

int amdgpu_dm_crtc_enable_vblank(struct drm_crtc *crtc);

void amdgpu_dm_crtc_disable_vblank(struct drm_crtc *crtc);

int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct drm_plane *plane,
			uint32_t link_index);

#endif

