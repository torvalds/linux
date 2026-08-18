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

#ifndef __AMDGPU_DM_COLOR_H__
#define __AMDGPU_DM_COLOR_H__

#define MAX_DRM_LUT_VALUE    0xFFFF
#define MAX_DRM_LUT32_VALUE  0xFFFFFFFF

#include <linux/types.h>

struct drm_color_lut;
struct drm_color_lut32;
struct drm_color_ctm;
struct drm_color_ctm_3x4;
struct drm_colorop_state;
struct drm_property_blob;
struct dc_gamma;
struct dc_rgb;
struct dc_plane_state;
struct fixed31_32;
struct tetrahedral_params;
struct dc_transfer_func;

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
/*
 * Prototypes for functions exposed to KUnit tests. The enum types
 * used below (dc_transfer_func_predefined, amdgpu_transfer_function,
 * drm_colorop_curve_1d_type) must be defined before this header is
 * included — the source file (amdgpu_dm_color.c) ensures this via
 * its own includes of dc.h, amdgpu_dm.h, and drm/drm_colorop.h.
 */
struct fixed31_32 amdgpu_dm_fixpt_from_s3132(__u64 x);
bool __is_lut_linear(const struct drm_color_lut *lut, uint32_t size);
void __drm_lut_to_dc_gamma(const struct drm_color_lut *lut,
			    struct dc_gamma *gamma, bool is_legacy);
void __drm_lut32_to_dc_gamma(const struct drm_color_lut32 *lut,
			      struct dc_gamma *gamma);
void __drm_ctm_to_dc_matrix(const struct drm_color_ctm *ctm,
			     struct fixed31_32 *matrix);
void __drm_ctm_3x4_to_dc_matrix(const struct drm_color_ctm_3x4 *ctm,
				 struct fixed31_32 *matrix);
enum dc_transfer_func_predefined
amdgpu_tf_to_dc_tf(enum amdgpu_transfer_function tf);
enum dc_transfer_func_predefined
amdgpu_colorop_tf_to_dc_tf(enum drm_colorop_curve_1d_type tf);
const struct drm_color_lut *
__extract_blob_lut(const struct drm_property_blob *blob, uint32_t *size);
const struct drm_color_lut32 *
__extract_blob_lut32(const struct drm_property_blob *blob, uint32_t *size);
void __to_dc_lut3d_color(struct dc_rgb *rgb,
			 const struct drm_color_lut lut,
			 int bit_precision);
void __drm_3dlut_to_dc_3dlut(const struct drm_color_lut *lut,
			     uint32_t lut3d_size,
			     struct tetrahedral_params *params,
			     bool use_tetrahedral_9,
			     int bit_depth);
void __to_dc_lut3d_32_color(struct dc_rgb *rgb,
			    const struct drm_color_lut32 lut,
			    int bit_precision);
void __drm_3dlut32_to_dc_3dlut(const struct drm_color_lut32 *lut,
				uint32_t lut3d_size,
				struct tetrahedral_params *params,
				bool use_tetrahedral_9,
				int bit_depth);
struct dc_3dlut;
void amdgpu_dm_atomic_lut3d(const struct drm_color_lut *drm_lut3d,
			     uint32_t drm_lut3d_size,
			     struct dc_3dlut *lut);
int __set_colorop_3dlut(const struct drm_color_lut32 *drm_lut3d,
			uint32_t drm_lut3d_size,
			struct dc_3dlut *lut);
void __set_tf_bypass(struct dc_transfer_func *tf);
void __set_tf_distributed_points(struct dc_transfer_func *tf,
				 enum dc_transfer_func_predefined predefined_tf);
int amdgpu_dm_set_atomic_regamma(struct dc_transfer_func *out_tf,
				 const struct drm_color_lut *regamma_lut,
				 uint32_t regamma_size, bool has_rom,
				 enum dc_transfer_func_predefined tf);
int amdgpu_dm_atomic_shaper_lut(const struct drm_color_lut *shaper_lut,
				bool has_rom,
				enum dc_transfer_func_predefined tf,
				uint32_t shaper_size,
				struct dc_transfer_func *func_shaper);
int amdgpu_dm_atomic_blend_lut(const struct drm_color_lut *blend_lut,
			       bool has_rom,
			       enum dc_transfer_func_predefined tf,
			       uint32_t blend_size,
			       struct dc_transfer_func *func_blend);
int __set_colorop_in_tf_1d_curve(struct dc_plane_state *dc_plane_state,
				 struct drm_colorop_state *colorop_state);
#endif

#endif /* __AMDGPU_DM_COLOR_H__ */
