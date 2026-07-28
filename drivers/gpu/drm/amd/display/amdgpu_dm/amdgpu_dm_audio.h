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
 */

#ifndef __AMDGPU_DM_AUDIO_H__
#define __AMDGPU_DM_AUDIO_H__

struct amdgpu_device;
struct drm_device;
struct drm_atomic_state;
struct drm_connector;
struct audio_info;
struct dc_sink;

int amdgpu_dm_audio_init(struct amdgpu_device *adev);
void amdgpu_dm_audio_fini(struct amdgpu_device *adev);
void amdgpu_dm_commit_audio(struct drm_device *dev,
			    struct drm_atomic_commit *state);
void amdgpu_dm_fill_audio_info(struct audio_info *audio_info,
		     const struct drm_connector *drm_connector,
		     const struct dc_sink *dc_sink);
void amdgpu_dm_audio_eld_notify(struct amdgpu_device *adev, int pin);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
struct device;

int amdgpu_dm_audio_component_bind(struct device *kdev,
				   struct device *hda_kdev, void *data);
void amdgpu_dm_audio_component_unbind(struct device *kdev,
				      struct device *hda_kdev, void *data);
int amdgpu_dm_audio_get_param(void);
void amdgpu_dm_audio_set_param(int val);
void amdgpu_dm_audio_init_pins(struct amdgpu_device *adev, int audio_count,
			       const unsigned int *inst_array);
#endif

#endif /* __AMDGPU_DM_AUDIO_H__ */
