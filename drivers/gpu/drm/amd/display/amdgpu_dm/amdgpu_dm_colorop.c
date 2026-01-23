// SPDX-License-Identifier: MIT
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

#include <drm/drm_print.h>
#include <drm/drm_plane.h>
#include <drm/drm_property.h>
#include <drm/drm_colorop.h>

#include "amdgpu.h"
#include "amdgpu_dm_colorop.h"
#include "dc.h"

const u64 amdgpu_dm_supported_degam_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF) |
	BIT(DRM_COLOROP_1D_CURVE_GAMMA22_INV);

const u64 amdgpu_dm_supported_shaper_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_OETF) |
	BIT(DRM_COLOROP_1D_CURVE_GAMMA22);

const u64 amdgpu_dm_supported_blnd_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF) |
	BIT(DRM_COLOROP_1D_CURVE_GAMMA22_INV);

#define MAX_COLOR_PIPELINE_OPS 10

#define LUT3D_SIZE		17

int amdgpu_dm_initialize_default_pipeline(struct drm_plane *plane, struct drm_prop_enum_list *list)
{
	struct drm_colorop *ops[MAX_COLOR_PIPELINE_OPS];
	struct drm_device *dev = plane->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	int ret;
	int i = 0;

	memset(ops, 0, sizeof(ops));

	/* 1D curve - DEGAM TF */
	ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane,
					      amdgpu_dm_supported_degam_tfs,
					      DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	list->type = ops[i]->base.id;

	i++;

	/* Multiplier */
	ops[i] = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_mult_init(dev, ops[i], plane, DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i-1], ops[i]);

	i++;

	/* 3x4 matrix */
	ops[i] = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_ctm_3x4_init(dev, ops[i], plane, DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i-1], ops[i]);

	i++;

	if (adev->dm.dc->caps.color.dpp.hw_3d_lut) {
		/* 1D curve - SHAPER TF */
		ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
		if (!ops[i]) {
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane,
						amdgpu_dm_supported_shaper_tfs,
						DRM_COLOROP_FLAG_ALLOW_BYPASS);
		if (ret)
			goto cleanup;

		drm_colorop_set_next_property(ops[i-1], ops[i]);

		i++;

		/* 1D LUT - SHAPER LUT */
		ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
		if (!ops[i]) {
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = drm_plane_colorop_curve_1d_lut_init(dev, ops[i], plane, MAX_COLOR_LUT_ENTRIES,
							DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
							DRM_COLOROP_FLAG_ALLOW_BYPASS);
		if (ret)
			goto cleanup;

		drm_colorop_set_next_property(ops[i-1], ops[i]);

		i++;

		/* 3D LUT */
		ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
		if (!ops[i]) {
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = drm_plane_colorop_3dlut_init(dev, ops[i], plane, LUT3D_SIZE,
					DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL,
					DRM_COLOROP_FLAG_ALLOW_BYPASS);
		if (ret)
			goto cleanup;

		drm_colorop_set_next_property(ops[i-1], ops[i]);

		i++;
	}

	/* 1D curve - BLND TF */
	ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane,
					      amdgpu_dm_supported_blnd_tfs,
					      DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	i++;

	/* 1D LUT - BLND LUT */
	ops[i] = kzalloc(sizeof(struct drm_colorop), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_lut_init(dev, ops[i], plane, MAX_COLOR_LUT_ENTRIES,
						  DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
						  DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i-1], ops[i]);

	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", ops[0]->base.id);

	return 0;

cleanup:
	if (ret == -ENOMEM)
		drm_err(plane->dev, "KMS: Failed to allocate colorop\n");

	drm_colorop_pipeline_destroy(dev);

	return ret;
}
