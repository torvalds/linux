/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_DM_COLOROP_H__
#define __AMDGPU_DM_COLOROP_H__

extern const u64 amdgpu_dm_supported_degam_tfs;
extern const u64 amdgpu_dm_supported_shaper_tfs;
extern const u64 amdgpu_dm_supported_blnd_tfs;
extern const u64 amdgpu_dm_supported_fm;

int amdgpu_dm_initialize_default_pipeline(struct drm_plane *plane, struct drm_prop_enum_list *list);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
#include <drm/drm_colorop.h>

struct amdgpu_dm_colorop_kunit_ops {
	struct drm_colorop *(*colorop_kzalloc_obj)(void);
	int (*curve_1d_init)(struct drm_device *dev, struct drm_colorop *colorop,
			     struct drm_plane *plane, const struct drm_colorop_funcs *funcs,
			     u64 supported_tfs, uint32_t flags);
	int (*curve_1d_lut_init)(struct drm_device *dev, struct drm_colorop *colorop,
				 struct drm_plane *plane,
				 const struct drm_colorop_funcs *funcs,
				 uint32_t lut_size,
				 enum drm_colorop_lut1d_interpolation_type interpolation,
				 uint32_t flags);
	int (*ctm_3x4_init)(struct drm_device *dev, struct drm_colorop *colorop,
			    struct drm_plane *plane, const struct drm_colorop_funcs *funcs,
			    uint32_t flags);
	int (*mult_init)(struct drm_device *dev, struct drm_colorop *colorop,
			 struct drm_plane *plane, const struct drm_colorop_funcs *funcs,
			 uint32_t flags);
	int (*lut3d_init)(struct drm_device *dev, struct drm_colorop *colorop,
			  struct drm_plane *plane,
			  const struct drm_colorop_funcs *funcs,
			  uint32_t lut_size,
			  enum drm_colorop_lut3d_interpolation_type interpolation,
			  uint32_t flags);
};

int amdgpu_dm_build_default_pipeline(struct drm_device *dev, struct drm_plane *plane,
				      bool hw_3d_lut, struct drm_prop_enum_list *list);
void amdgpu_dm_colorop_kunit_set_ops(const struct amdgpu_dm_colorop_kunit_ops *ops);
#endif

#endif /* __AMDGPU_DM_COLOROP_H__*/
