// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "komeda_dev.h"
#include "komeda_kms.h"

struct drm_crtc_helper_funcs komeda_crtc_helper_funcs = {
};

static const struct drm_crtc_funcs komeda_crtc_funcs = {
};

int komeda_kms_setup_crtcs(struct komeda_kms_dev *kms,
			   struct komeda_dev *mdev)
{
	struct komeda_crtc *crtc;
	struct komeda_pipeline *master;
	char str[16];
	int i;

	kms->n_crtcs = 0;

	for (i = 0; i < mdev->n_pipelines; i++) {
		crtc = &kms->crtcs[kms->n_crtcs];
		master = mdev->pipelines[i];

		crtc->master = master;
		crtc->slave  = NULL;

		if (crtc->slave)
			sprintf(str, "pipe-%d", crtc->slave->id);
		else
			sprintf(str, "None");

		DRM_INFO("crtc%d: master(pipe-%d) slave(%s) output: %s.\n",
			 kms->n_crtcs, master->id, str,
			 master->of_output_dev ?
			 master->of_output_dev->full_name : "None");

		kms->n_crtcs++;
	}

	return 0;
}

static struct drm_plane *
get_crtc_primary(struct komeda_kms_dev *kms, struct komeda_crtc *crtc)
{
	struct komeda_plane *kplane;
	struct drm_plane *plane;

	drm_for_each_plane(plane, &kms->base) {
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			continue;

		kplane = to_kplane(plane);
		/* only master can be primary */
		if (kplane->layer->base.pipeline == crtc->master)
			return plane;
	}

	return NULL;
}

static int komeda_crtc_add(struct komeda_kms_dev *kms,
			   struct komeda_crtc *kcrtc)
{
	struct drm_crtc *crtc = &kcrtc->base;
	int err;

	err = drm_crtc_init_with_planes(&kms->base, crtc,
					get_crtc_primary(kms, kcrtc), NULL,
					&komeda_crtc_funcs, NULL);
	if (err)
		return err;

	drm_crtc_helper_add(crtc, &komeda_crtc_helper_funcs);
	drm_crtc_vblank_reset(crtc);

	crtc->port = kcrtc->master->of_output_port;

	return 0;
}

int komeda_kms_add_crtcs(struct komeda_kms_dev *kms, struct komeda_dev *mdev)
{
	int i, err;

	for (i = 0; i < kms->n_crtcs; i++) {
		err = komeda_crtc_add(kms, &kms->crtcs[i]);
		if (err)
			return err;
	}

	return 0;
}
