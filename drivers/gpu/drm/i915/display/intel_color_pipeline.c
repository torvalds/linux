// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */
#include <drm/drm_print.h>

#include "intel_color.h"
#include "intel_colorop.h"
#include "intel_color_pipeline.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "skl_universal_plane.h"

#define MAX_COLOR_PIPELINES 1
#define MAX_COLOROP 4
#define PLANE_DEGAMMA_SIZE 128
#define PLANE_GAMMA_SIZE 32

static const struct drm_colorop_funcs intel_colorop_funcs = {
	.destroy = intel_colorop_destroy,
};

/*
 * 3DLUT can be bound to all three HDR planes. However, even with the latest
 * color pipeline UAPI, there is no good way to represent a HW block which
 * can be shared/attached at different stages of the pipeline. So right now,
 * we expose 3DLUT only attached with the primary plane.
 *
 * That way we don't confuse the userspace with opaque commit failures
 * on trying to enable it on multiple planes which would otherwise make
 * the pipeline totally unusable.
 */
static const enum intel_color_block xe3plpd_primary_plane_pipeline[] = {
	INTEL_PLANE_CB_PRE_CSC_LUT,
	INTEL_PLANE_CB_CSC,
	INTEL_PLANE_CB_3DLUT,
	INTEL_PLANE_CB_POST_CSC_LUT,
};

static const enum intel_color_block hdr_plane_pipeline[] = {
	INTEL_PLANE_CB_PRE_CSC_LUT,
	INTEL_PLANE_CB_CSC,
	INTEL_PLANE_CB_POST_CSC_LUT,
};

static bool plane_has_3dlut(struct intel_display *display, enum pipe pipe,
			    struct drm_plane *plane)
{
	return (DISPLAY_VER(display) >= 35 &&
		intel_color_crtc_has_3dlut(display, pipe) &&
		plane->type == DRM_PLANE_TYPE_PRIMARY);
}

static
struct intel_colorop *intel_color_pipeline_plane_add_colorop(struct drm_plane *plane,
							     struct intel_colorop *prev,
							     enum intel_color_block id)
{
	struct drm_device *dev = plane->dev;
	struct intel_colorop *colorop;
	int ret;

	colorop = intel_colorop_create(id);

	if (IS_ERR(colorop))
		return colorop;

	switch (id) {
	case INTEL_PLANE_CB_PRE_CSC_LUT:
		ret = drm_plane_colorop_curve_1d_lut_init(dev,
							  &colorop->base, plane,
							  &intel_colorop_funcs,
							  PLANE_DEGAMMA_SIZE,
							  DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
							  DRM_COLOROP_FLAG_ALLOW_BYPASS);
		break;
	case INTEL_PLANE_CB_CSC:
		ret = drm_plane_colorop_ctm_3x4_init(dev, &colorop->base, plane,
						     &intel_colorop_funcs,
						     DRM_COLOROP_FLAG_ALLOW_BYPASS);
		break;
	case INTEL_PLANE_CB_3DLUT:
		ret = drm_plane_colorop_3dlut_init(dev, &colorop->base, plane,
						   &intel_colorop_funcs, 17,
						   DRM_COLOROP_LUT3D_INTERPOLATION_TETRAHEDRAL,
						   true);
		break;
	case INTEL_PLANE_CB_POST_CSC_LUT:
		ret = drm_plane_colorop_curve_1d_lut_init(dev, &colorop->base, plane,
							  &intel_colorop_funcs,
							  PLANE_GAMMA_SIZE,
							  DRM_COLOROP_LUT1D_INTERPOLATION_LINEAR,
							  DRM_COLOROP_FLAG_ALLOW_BYPASS);
		break;
	default:
		drm_err(plane->dev, "Invalid colorop id [%d]", id);
		ret = -EINVAL;
	}

	if (ret)
		goto cleanup;

	if (prev)
		drm_colorop_set_next_property(&prev->base, &colorop->base);

	return colorop;

cleanup:
	intel_colorop_destroy(&colorop->base);
	return ERR_PTR(ret);
}

static
int _intel_color_pipeline_plane_init(struct drm_plane *plane, struct drm_prop_enum_list *list,
				     enum pipe pipe)
{
	struct drm_device *dev = plane->dev;
	struct intel_display *display = to_intel_display(dev);
	struct intel_colorop *colorop[MAX_COLOROP];
	struct intel_colorop *prev = NULL;
	const enum intel_color_block *pipeline;
	int pipeline_len;
	int ret = 0;
	int i;

	if (plane_has_3dlut(display, pipe, plane)) {
		pipeline = xe3plpd_primary_plane_pipeline;
		pipeline_len = ARRAY_SIZE(xe3plpd_primary_plane_pipeline);
	} else {
		pipeline = hdr_plane_pipeline;
		pipeline_len = ARRAY_SIZE(hdr_plane_pipeline);
	}

	for (i = 0; i < pipeline_len; i++) {
		colorop[i] = intel_color_pipeline_plane_add_colorop(plane, prev,
								    pipeline[i]);
		if (IS_ERR(colorop[i])) {
			ret = PTR_ERR(colorop[i]);
			goto cleanup;
		}

		prev = colorop[i];
	}

	list->type = colorop[0]->base.base.id;
	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", colorop[0]->base.base.id);

	return 0;

cleanup:
	while (--i >= 0)
		intel_colorop_destroy(&colorop[i]->base);
	return ret;
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
