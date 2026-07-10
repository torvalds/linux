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

#ifndef __AMDGPU_DM_FREESYNC_H__
#define __AMDGPU_DM_FREESYNC_H__

#include <linux/types.h>

struct amdgpu_display_manager;
struct dm_crtc_state;
struct dm_connector_state;
struct dc_stream_state;
struct dc_plane_state;
struct drm_crtc_state;

bool amdgpu_dm_is_dc_timing_adjust_needed(struct dm_crtc_state *old_state,
					  struct dm_crtc_state *new_state);

bool amdgpu_dm_is_timing_unchanged_for_freesync(struct drm_crtc_state *old_crtc_state,
						struct drm_crtc_state *new_crtc_state);

void amdgpu_dm_set_freesync_fixed_config(struct dm_crtc_state *dm_new_crtc_state);

void amdgpu_dm_reset_freesync_config_for_crtc(struct dm_crtc_state *new_crtc_state);

void amdgpu_dm_get_freesync_config_for_crtc(struct dm_crtc_state *new_crtc_state,
					    struct dm_connector_state *new_con_state);

void amdgpu_dm_update_freesync_state_on_stream(struct amdgpu_display_manager *dm,
					       struct dm_crtc_state *new_crtc_state,
					       struct dc_stream_state *new_stream,
					       struct dc_plane_state *surface,
					       u32 flip_timestamp_in_us);

void amdgpu_dm_update_stream_irq_parameters(struct amdgpu_display_manager *dm,
					    struct dm_crtc_state *new_crtc_state);

void amdgpu_dm_handle_vrr_transition(struct amdgpu_display_manager *dm,
				     struct dm_crtc_state *old_state,
				     struct dm_crtc_state *new_state);

#endif /* __AMDGPU_DM_FREESYNC_H__ */
