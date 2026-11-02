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
#include "dm_helpers.h"
#include "dc.h"

const u64 amdgpu_dm_supported_degam_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF) |
	BIT(DRM_COLOROP_1D_CURVE_GAMMA22);
EXPORT_IF_KUNIT(amdgpu_dm_supported_degam_tfs);

const u64 amdgpu_dm_supported_shaper_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_INV_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_OETF) |
	BIT(DRM_COLOROP_1D_CURVE_GAMMA22_INV);
EXPORT_IF_KUNIT(amdgpu_dm_supported_shaper_tfs);

const u64 amdgpu_dm_supported_blnd_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_PQ_125_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_BT2020_INV_OETF) |
	BIT(DRM_COLOROP_1D_CURVE_GAMMA22);
EXPORT_IF_KUNIT(amdgpu_dm_supported_blnd_tfs);

const u64 amdgpu_dm_supported_fm =
	BIT(DRM_COLOROP_FM_YCBCR601_FULL_RGB) |
	BIT(DRM_COLOROP_FM_YCBCR601_LIMITED_RGB) |
	BIT(DRM_COLOROP_FM_YCBCR709_FULL_RGB) |
	BIT(DRM_COLOROP_FM_YCBCR709_LIMITED_RGB) |
	BIT(DRM_COLOROP_FM_YCBCR2020_NC_FULL_RGB) |
	BIT(DRM_COLOROP_FM_YCBCR2020_NC_LIMITED_RGB);

#define MAX_COLOR_PIPELINE_OPS 10

#define LUT3D_SIZE		17

static const struct drm_colorop_funcs dm_colorop_funcs = {
	.destroy = drm_colorop_destroy,
};

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
static struct drm_colorop *amdgpu_dm_colorop_kzalloc(void)
{
	return kzalloc_obj(struct drm_colorop);
}

static const struct amdgpu_dm_colorop_kunit_ops amdgpu_dm_colorop_default_ops = {
	.colorop_kzalloc_obj = amdgpu_dm_colorop_kzalloc,
	.curve_1d_init = drm_plane_colorop_curve_1d_init,
	.curve_1d_lut_init = drm_plane_colorop_curve_1d_lut_init,
	.ctm_3x4_init = drm_plane_colorop_ctm_3x4_init,
	.mult_init = drm_plane_colorop_mult_init,
	.lut3d_init = drm_plane_colorop_3dlut_init,
};

static const struct amdgpu_dm_colorop_kunit_ops *amdgpu_dm_colorop_ops =
	&amdgpu_dm_colorop_default_ops;

void amdgpu_dm_colorop_kunit_set_ops(const struct amdgpu_dm_colorop_kunit_ops *ops)
{
	amdgpu_dm_colorop_ops = ops ? ops : &amdgpu_dm_colorop_default_ops;
}
EXPORT_IF_KUNIT(amdgpu_dm_colorop_kunit_set_ops);

#define colorop_kzalloc_obj		amdgpu_dm_colorop_ops->colorop_kzalloc_obj
#define colorop_curve_1d_init		amdgpu_dm_colorop_ops->curve_1d_init
#define colorop_curve_1d_lut_init	amdgpu_dm_colorop_ops->curve_1d_lut_init
#define colorop_ctm_3x4_init		amdgpu_dm_colorop_ops->ctm_3x4_init
#define colorop_mult_init		amdgpu_dm_colorop_ops->mult_init
#define colorop_3dlut_init		amdgpu_dm_colorop_ops->lut3d_init

#else

#define colorop_kzalloc_obj()		kzalloc_obj(struct drm_colorop)
#define colorop_curve_1d_init		drm_plane_colorop_curve_1d_init
#define colorop_curve_1d_lut_init	drm_plane_colorop_curve_1d_lut_init
#define colorop_ctm_3x4_init		drm_plane_colorop_ctm_3x4_init
#define colorop_mult_init		drm_plane_colorop_mult_init
#define colorop_3dlut_init		drm_plane_colorop_3dlut_init

#endif

STATIC_IFN_KUNIT int
amdgpu_dm_build_default_pipeline(struct drm_device *dev, struct drm_plane *plane,
				  bool hw_3d_lut, struct drm_prop_enum_list *list)
{
	struct drm_colorop *ops[MAX_COLOR_PIPELINE_OPS];
	int ret;
	int i = 0;

	memset(ops, 0, sizeof(ops));

	/* Fixed Matrix (YUV to RGB) */
	ops[i] = kzalloc_obj(*ops[0]);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_fixed_matrix_init(dev, ops[i], plane, &dm_colorop_funcs,
						  amdgpu_dm_supported_fm,
						  DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	list->type = ops[i]->base.id;

	i++;

	/* 1D curve - DEGAM TF */
	ops[i] = colorop_kzalloc_obj();
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = colorop_curve_1d_init(dev, ops[i], plane, &dm_colorop_funcs,
				    amdgpu_dm_supported_degam_tfs,
				    DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	i++;

	/* Multiplier */
	ops[i] = colorop_kzalloc_obj();
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = colorop_mult_init(dev, ops[i], plane, &dm_colorop_funcs,
				DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i-1], ops[i]);

	i++;

	/* 3x4 matrix */
	ops[i] = colorop_kzalloc_obj();
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = colorop_ctm_3x4_init(dev, ops[i], plane, &dm_colorop_funcs,
				   DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i-1], ops[i]);

	i++;

	if (hw_3d_lut) {
		/* 1D curve - SHAPER TF */
		ops[i] = colorop_kzalloc_obj();
		if (!ops[i]) {
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = colorop_curve_1d_init(dev, ops[i], plane, &dm_colorop_funcs,
					    amdgpu_dm_supported_shaper_tfs,
					    DRM_COLOROP_FLAG_ALLOW_BYPASS);
		if (ret)
			goto cleanup;

		drm_colorop_set_next_property(ops[i-1], ops[i]);

		i++;

		/* 1D LUT - SHAPER LUT */
		ops[i] = colorop_kzalloc_obj();
		if (!ops[i]) {
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = colorop_curve_1d_lut_init(dev, ops[i], plane, &dm_colorop_funcs,
						MAX_COLOR_LUT_ENTRIES,
						DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
						DRM_COLOROP_FLAG_ALLOW_BYPASS);
		if (ret)
			goto cleanup;

		drm_colorop_set_next_property(ops[i-1], ops[i]);

		i++;

		/* 3D LUT */
		ops[i] = colorop_kzalloc_obj();
		if (!ops[i]) {
			ret = -ENOMEM;
			goto cleanup;
		}

		ret = colorop_3dlut_init(dev, ops[i], plane, &dm_colorop_funcs,
					 LUT3D_SIZE,
					 DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL,
					 DRM_COLOROP_FLAG_ALLOW_BYPASS);
		if (ret)
			goto cleanup;

		drm_colorop_set_next_property(ops[i-1], ops[i]);

		i++;
	}

	/* 1D curve - BLND TF */
	ops[i] = colorop_kzalloc_obj();
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = colorop_curve_1d_init(dev, ops[i], plane, &dm_colorop_funcs,
				    amdgpu_dm_supported_blnd_tfs,
				    DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	i++;

	/* 1D LUT - BLND LUT */
	ops[i] = colorop_kzalloc_obj();
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = colorop_curve_1d_lut_init(dev, ops[i], plane, &dm_colorop_funcs,
					MAX_COLOR_LUT_ENTRIES,
					DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
					DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i-1], ops[i]);

	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", ops[0]->base.id);

	return 0;

cleanup:
	if (ret == -ENOMEM)
		drm_err(dev, "KMS: Failed to allocate colorop\n");

	drm_colorop_pipeline_destroy(dev);

	return ret;
}
EXPORT_IF_KUNIT(amdgpu_dm_build_default_pipeline);

int amdgpu_dm_initialize_default_pipeline(struct drm_plane *plane, struct drm_prop_enum_list *list)
{
	struct drm_device *dev = plane->dev;
	struct amdgpu_device *adev = drm_to_adev(dev);
	bool hw_3d_lut = adev->dm.dc->caps.color.dpp.hw_3d_lut ||
			 adev->dm.dc->caps.color.mpc.preblend;

	return amdgpu_dm_build_default_pipeline(dev, plane, hw_3d_lut, list);
}
EXPORT_IF_KUNIT(amdgpu_dm_initialize_default_pipeline);
