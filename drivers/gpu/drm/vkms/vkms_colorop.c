// SPDX-License-Identifier: GPL-2.0+

#include <linux/slab.h>
#include <drm/drm_colorop.h>
#include <drm/drm_print.h>
#include <drm/drm_property.h>
#include <drm/drm_plane.h>

#include "vkms_drv.h"

static const u64 supported_tfs =
	BIT(DRM_COLOROP_1D_CURVE_SRGB_EOTF) |
	BIT(DRM_COLOROP_1D_CURVE_SRGB_INV_EOTF);

#define MAX_COLOR_PIPELINE_OPS 4

static int vkms_initialize_color_pipeline(struct drm_plane *plane, struct drm_prop_enum_list *list)
{
	struct drm_colorop *ops[MAX_COLOR_PIPELINE_OPS];
	struct drm_device *dev = plane->dev;
	int ret;
	int i = 0, j = 0;

	memset(ops, 0, sizeof(ops));

	/* 1st op: 1d curve */
	ops[i] = kzalloc(sizeof(*ops[i]), GFP_KERNEL);
	if (!ops[i]) {
		drm_err(dev, "KMS: Failed to allocate colorop\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane, supported_tfs,
					      DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	list->type = ops[i]->base.id;

	i++;

	/* 2nd op: 3x4 matrix */
	ops[i] = kzalloc(sizeof(*ops[i]), GFP_KERNEL);
	if (!ops[i]) {
		drm_err(dev, "KMS: Failed to allocate colorop\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_ctm_3x4_init(dev, ops[i], plane, DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	i++;

	/* 3rd op: 3x4 matrix */
	ops[i] = kzalloc(sizeof(*ops[i]), GFP_KERNEL);
	if (!ops[i]) {
		drm_err(dev, "KMS: Failed to allocate colorop\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_ctm_3x4_init(dev, ops[i], plane, DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	i++;

	/* 4th op: 1d curve */
	ops[i] = kzalloc(sizeof(*ops[i]), GFP_KERNEL);
	if (!ops[i]) {
		drm_err(dev, "KMS: Failed to allocate colorop\n");
		ret = -ENOMEM;
		goto cleanup;
	}

	ret = drm_plane_colorop_curve_1d_init(dev, ops[i], plane, supported_tfs,
					      DRM_COLOROP_FLAG_ALLOW_BYPASS);
	if (ret)
		goto cleanup;

	drm_colorop_set_next_property(ops[i - 1], ops[i]);

	list->name = kasprintf(GFP_KERNEL, "Color Pipeline %d", ops[0]->base.id);

	return 0;

cleanup:
	for (j = 0; j < i; j++) {
		if (ops[j]) {
			drm_colorop_cleanup(ops[j]);
			kfree(ops[j]);
		}
	}

	return ret;
}

int vkms_initialize_colorops(struct drm_plane *plane)
{
	struct drm_prop_enum_list pipeline = {};
	int ret = 0;

	/* Add color pipeline */
	ret = vkms_initialize_color_pipeline(plane, &pipeline);
	if (ret)
		goto out;

	/* Create COLOR_PIPELINE property and attach */
	ret = drm_plane_create_color_pipeline_property(plane, &pipeline, 1);

	kfree(pipeline.name);
out:
	return ret;
}
