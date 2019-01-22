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

/* crtc_atomic_check is the final check stage, so beside build a display data
 * pipeline according the crtc_state, but still needs to release/disable the
 * unclaimed pipeline resources.
 */
static int
komeda_crtc_atomic_check(struct drm_crtc *crtc,
			 struct drm_crtc_state *state)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_crtc_state *kcrtc_st = to_kcrtc_st(state);
	int err;

	if (state->active) {
		err = komeda_build_display_data_flow(kcrtc, kcrtc_st);
		if (err)
			return err;
	}

	/* release unclaimed pipeline resources */
	err = komeda_release_unclaimed_resources(kcrtc->master, kcrtc_st);
	if (err)
		return err;

	return 0;
}

void komeda_crtc_handle_event(struct komeda_crtc   *kcrtc,
			      struct komeda_events *evts)
{
	struct drm_crtc *crtc = &kcrtc->base;
	u32 events = evts->pipes[kcrtc->master->id];

	if (events & KOMEDA_EVENT_VSYNC)
		drm_crtc_handle_vblank(crtc);

	/* will handle it together with the write back support */
	if (events & KOMEDA_EVENT_EOW)
		DRM_DEBUG("EOW.\n");

	/* will handle it with crtc->flush */
	if (events & KOMEDA_EVENT_FLIP)
		DRM_DEBUG("FLIP Done.\n");
}

static void
komeda_crtc_do_flush(struct drm_crtc *crtc,
		     struct drm_crtc_state *old)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_crtc_state *kcrtc_st = to_kcrtc_st(crtc->state);
	struct komeda_dev *mdev = kcrtc->base.dev->dev_private;
	struct komeda_pipeline *master = kcrtc->master;

	DRM_DEBUG_ATOMIC("CRTC%d_FLUSH: active_pipes: 0x%x, affected: 0x%x.\n",
			 drm_crtc_index(crtc),
			 kcrtc_st->active_pipes, kcrtc_st->affected_pipes);

	/* step 1: update the pipeline/component state to HW */
	if (has_bit(master->id, kcrtc_st->affected_pipes))
		komeda_pipeline_update(master, old->state);

	/* step 2: notify the HW to kickoff the update */
	mdev->funcs->flush(mdev, master->id, kcrtc_st->active_pipes);
}

static void
komeda_crtc_atomic_flush(struct drm_crtc *crtc,
			 struct drm_crtc_state *old)
{
	/* commit with modeset will be handled in enable/disable */
	if (drm_atomic_crtc_needs_modeset(crtc->state))
		return;

	komeda_crtc_do_flush(crtc, old);
}

static enum drm_mode_status
komeda_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *m)
{
	struct komeda_dev *mdev = crtc->dev->dev_private;
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_pipeline *master = kcrtc->master;
	long mode_clk, pxlclk;

	if (m->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	/* main clock/AXI clk must be faster than pxlclk*/
	mode_clk = m->clock * 1000;
	pxlclk = clk_round_rate(master->pxlclk, mode_clk);
	if (pxlclk != mode_clk) {
		DRM_DEBUG_ATOMIC("pxlclk doesn't support %ld Hz\n", mode_clk);

		return MODE_NOCLOCK;
	}

	if (clk_round_rate(mdev->mclk, mode_clk) < pxlclk) {
		DRM_DEBUG_ATOMIC("mclk can't satisfy the requirement of %s-clk: %ld.\n",
				 m->name, pxlclk);

		return MODE_CLOCK_HIGH;
	}

	if (clk_round_rate(master->aclk, mode_clk) < pxlclk) {
		DRM_DEBUG_ATOMIC("aclk can't satisfy the requirement of %s-clk: %ld.\n",
				 m->name, pxlclk);

		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static bool komeda_crtc_mode_fixup(struct drm_crtc *crtc,
				   const struct drm_display_mode *m,
				   struct drm_display_mode *adjusted_mode)
{
	struct komeda_crtc *kcrtc = to_kcrtc(crtc);
	struct komeda_pipeline *master = kcrtc->master;
	long mode_clk = m->clock * 1000;

	adjusted_mode->clock = clk_round_rate(master->pxlclk, mode_clk) / 1000;

	return true;
}

struct drm_crtc_helper_funcs komeda_crtc_helper_funcs = {
	.atomic_check	= komeda_crtc_atomic_check,
	.atomic_flush	= komeda_crtc_atomic_flush,
	.mode_valid	= komeda_crtc_mode_valid,
	.mode_fixup	= komeda_crtc_mode_fixup,
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
