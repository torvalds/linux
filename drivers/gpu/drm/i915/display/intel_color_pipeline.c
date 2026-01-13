// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */
#include "intel_color.h"
#include "intel_colorop.h"
#include "intel_color_pipeline.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "skl_universal_plane.h"

#define MAX_COLOR_PIPELINES 1
#define PLANE_DEGAMMA_SIZE 128
#define PLANE_GAMMA_SIZE 32

static
int _intel_color_pipeline_plane_init(struct drm_plane *plane, struct drm_prop_enum_list *list,
				     enum pipe pipe)
{
	struct drm_device *dev = plane->dev;
	struct intel_display *display = to_intel_display(dev);
	struct drm_colorop *prev_op;
	struct intel_colorop *colorop;
	int ret;

	colorop = intel_colorop_create(INTEL_PLANE_CB_PRE_CSC_LUT);

	ret = drm_plane_colorop_curve_1d_lut_init(dev, &colorop->base, plane,
						  PLANE_DEGAMMA_SIZE,
						  DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
						  DRM_COLOROP_FLAG_ALLOW_BYPASS);

	if (ret)
		return ret;

	list->type = colorop->base.base.id;

	/* TODO: handle failures and clean up */
	prev_op = &colorop->base;

	colorop = intel_colorop_create(INTEL_PLANE_CB_CSC);
	ret = drm_plane_colorop_ctm_3x4_init(dev, &colorop->base, plane,
					     DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, &colorop->base);
	prev_op = &colorop->base;

	if (DISPLAY_VER(display) >= 35 &&
	    intel_color_crtc_has_3dlut(display, pipe) &&
	    plane->type == DRM_PLANE_TYPE_PRIMARY) {
		colorop = intel_colorop_create(INTEL_PLANE_CB_3DLUT);

		ret = drm_plane_colorop_3dlut_init(dev, &colorop->base, plane, 17,
						   DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL,
						   true);
		if (ret)
			return ret;

		drm_colorop_set_next_property(prev_op, &colorop->base);

		prev_op = &colorop->base;
	}

	colorop = intel_colorop_create(INTEL_PLANE_CB_POST_CSC_LUT);
	ret = drm_plane_colorop_curve_1d_lut_init(dev, &colorop->base, plane,
						  PLANE_GAMMA_SIZE,
						  DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
						  DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		return ret;

	drm_colorop_set_next_property(prev_op, &colorop->base);

	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", list->type);

	return 0;
}

int intel_color_pipeline_plane_init(struct drm_plane *plane, enum pipe pipe)
{
	struct drm_device *dev = plane->dev;
	struct intel_display *display = to_intel_display(dev);
	struct drm_prop_enum_list pipelines[MAX_COLOR_PIPELINES] = {};
	int len = 0;
	int ret = 0;
	int i;

	/* Currently expose pipeline only for HDR planes */
	if (!icl_is_hdr_plane(display, to_intel_plane(plane)->id))
		return 0;

	/* Add pipeline consisting of transfer functions */
	ret = _intel_color_pipeline_plane_init(plane, &pipelines[len], pipe);
	if (ret)
		goto out;
	len++;

	ret = drm_plane_create_color_pipeline_property(plane, pipelines, len);

	for (i = 0; i < len; i++)
		kfree(pipelines[i].name);

out:
	return ret;
}
