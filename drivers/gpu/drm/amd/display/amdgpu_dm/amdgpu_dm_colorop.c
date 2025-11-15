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

#include "amdgpu_dm_colorop.h"

const u64 amdgpu_dm_supported_degam_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF);

const u64 amdgpu_dm_supported_shaper_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF);

#define MAX_COLOR_PIPELINE_OPS 10

int amdgpu_dm_initialize_default_pipeline(struct drm_plane *plane, struct drm_prop_enum_list *list)
{
	struct drm_colorop *ops[MAX_COLOR_PIPELINE_OPS];
	struct drm_device *dev = plane->dev;
	int ret;
	int i = 0;

	memset(ops, 0, sizeof(ops));

	/* 1D curve - DEGAM TF */
	ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane, amdgpu_dm_supported_degam_tfs);
	if (ret)
		goto cleanup;

	list->type = ops[i]->base.id;
	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", ops[i]->base.id);

	i++;

	/* 1D curve - SHAPER TF */
	ops[i] = kzalloc(sizeof(*ops[0]), GFP_KERNEL);
	if (!ops[i]) {
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane, amdgpu_dm_supported_shaper_tfs);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	return 0;

cleanup:
	if (ret == -ENOMEM)
		drm_err(plane->dev, "KMS: Failed to allocate colorop\n");

	drm_colorop_pipeline_destroy(dev);

	return ret;
}
