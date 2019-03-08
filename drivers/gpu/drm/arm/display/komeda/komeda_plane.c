// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include "komeda_dev.h"
#include "komeda_kms.h"

static const struct drm_plane_helper_funcs komeda_plane_helper_funcs = {
};

static void komeda_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);

	kfree(to_kplane(plane));
}

static const struct drm_plane_funcs komeda_plane_funcs = {
};

/* for komeda, which is pipeline can be share between crtcs */
static u32 get_possible_crtcs(struct komeda_kms_dev *kms,
			      struct komeda_pipeline *pipe)
{
	struct komeda_crtc *crtc;
	u32 possible_crtcs = 0;
	int i;

	for (i = 0; i < kms->n_crtcs; i++) {
		crtc = &kms->crtcs[i];

		if ((pipe == crtc->master) || (pipe == crtc->slave))
			possible_crtcs |= BIT(i);
	}

	return possible_crtcs;
}

/* use Layer0 as primary */
static u32 get_plane_type(struct komeda_kms_dev *kms,
			  struct komeda_component *c)
{
	bool is_primary = (c->id == KOMEDA_COMPONENT_LAYER0);

	return is_primary ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
}

static int komeda_plane_add(struct komeda_kms_dev *kms,
			    struct komeda_layer *layer)
{
	struct komeda_dev *mdev = kms->base.dev_private;
	struct komeda_component *c = &layer->base;
	struct komeda_plane *kplane;
	struct drm_plane *plane;
	u32 *formats, n_formats = 0;
	int err;

	kplane = kzalloc(sizeof(*kplane), GFP_KERNEL);
	if (!kplane)
		return -ENOMEM;

	plane = &kplane->base;
	kplane->layer = layer;

	formats = komeda_get_layer_fourcc_list(&mdev->fmt_tbl,
					       layer->layer_type, &n_formats);

	err = drm_universal_plane_init(&kms->base, plane,
			get_possible_crtcs(kms, c->pipeline),
			&komeda_plane_funcs,
			formats, n_formats, NULL,
			get_plane_type(kms, c),
			"%s", c->name);

	komeda_put_fourcc_list(formats);

	if (err)
		goto cleanup;

	drm_plane_helper_add(plane, &komeda_plane_helper_funcs);

	return 0;
cleanup:
	komeda_plane_destroy(plane);
	return err;
}

int komeda_kms_add_planes(struct komeda_kms_dev *kms, struct komeda_dev *mdev)
{
	struct komeda_pipeline *pipe;
	int i, j, err;

	for (i = 0; i < mdev->n_pipelines; i++) {
		pipe = mdev->pipelines[i];

		for (j = 0; j < pipe->n_layers; j++) {
			err = komeda_plane_add(kms, pipe->layers[j]);
			if (err)
				return err;
		}
	}

	return 0;
}
