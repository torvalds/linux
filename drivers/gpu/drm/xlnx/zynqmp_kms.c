// SPDX-License-Identifier: GPL-2.0
/*
 * ZynqMP DisplayPort Subsystem - KMS API
 *
 * Copyright (C) 2017 - 2021 Xilinx, Inc.
 *
 * Authors:
 * - Hyun Woo Kwon <hyun.kwon@xilinx.com>
 * - Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_plane.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include "zynqmp_disp.h"
#include "zynqmp_dp.h"
#include "zynqmp_dpsub.h"
#include "zynqmp_kms.h"

/* -----------------------------------------------------------------------------
 * DRM CRTC
 */

static inline struct zynqmp_dpsub *crtc_to_dpsub(struct drm_crtc *crtc)
{
	return container_of(crtc, struct zynqmp_dpsub, crtc);
}

static void zynqmp_dpsub_crtc_atomic_enable(struct drm_crtc *crtc,
					    struct drm_atomic_state *state)
{
	struct zynqmp_dpsub *dpsub = crtc_to_dpsub(crtc);
	struct drm_display_mode *adjusted_mode = &crtc->state->adjusted_mode;
	int ret, vrefresh;

	pm_runtime_get_sync(dpsub->dev);

	zynqmp_disp_setup_clock(dpsub->disp, adjusted_mode->clock * 1000);

	ret = clk_prepare_enable(dpsub->vid_clk);
	if (ret) {
		dev_err(dpsub->dev, "failed to enable a pixel clock\n");
		pm_runtime_put_sync(dpsub->dev);
		return;
	}

	zynqmp_disp_enable(dpsub->disp);

	/* Delay of 3 vblank intervals for timing gen to be stable */
	vrefresh = (adjusted_mode->clock * 1000) /
		   (adjusted_mode->vtotal * adjusted_mode->htotal);
	msleep(3 * 1000 / vrefresh);
}

static void zynqmp_dpsub_crtc_atomic_disable(struct drm_crtc *crtc,
					     struct drm_atomic_state *state)
{
	struct zynqmp_dpsub *dpsub = crtc_to_dpsub(crtc);
	struct drm_plane_state *old_plane_state;

	/*
	 * Disable the plane if active. The old plane state can be NULL in the
	 * .shutdown() path if the plane is already disabled, skip
	 * zynqmp_disp_plane_atomic_disable() in that case.
	 */
	old_plane_state = drm_atomic_get_old_plane_state(state, crtc->primary);
	if (old_plane_state)
		zynqmp_disp_plane_atomic_disable(crtc->primary, state);

	zynqmp_disp_disable(dpsub->disp);

	drm_crtc_vblank_off(crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	clk_disable_unprepare(dpsub->vid_clk);
	pm_runtime_put_sync(dpsub->dev);
}

static int zynqmp_dpsub_crtc_atomic_check(struct drm_crtc *crtc,
					  struct drm_atomic_state *state)
{
	return drm_atomic_add_affected_planes(state, crtc);
}

static void zynqmp_dpsub_crtc_atomic_begin(struct drm_crtc *crtc,
					   struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static void zynqmp_dpsub_crtc_atomic_flush(struct drm_crtc *crtc,
					   struct drm_atomic_state *state)
{
	if (crtc->state->event) {
		struct drm_pending_vblank_event *event;

		/* Consume the flip_done event from atomic helper. */
		event = crtc->state->event;
		crtc->state->event = NULL;

		event->pipe = drm_crtc_index(crtc);

		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_arm_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs zynqmp_dpsub_crtc_helper_funcs = {
	.atomic_enable	= zynqmp_dpsub_crtc_atomic_enable,
	.atomic_disable	= zynqmp_dpsub_crtc_atomic_disable,
	.atomic_check	= zynqmp_dpsub_crtc_atomic_check,
	.atomic_begin	= zynqmp_dpsub_crtc_atomic_begin,
	.atomic_flush	= zynqmp_dpsub_crtc_atomic_flush,
};

static int zynqmp_dpsub_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct zynqmp_dpsub *dpsub = crtc_to_dpsub(crtc);

	zynqmp_dp_enable_vblank(dpsub->dp);

	return 0;
}

static void zynqmp_dpsub_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct zynqmp_dpsub *dpsub = crtc_to_dpsub(crtc);

	zynqmp_dp_disable_vblank(dpsub->dp);
}

static const struct drm_crtc_funcs zynqmp_dpsub_crtc_funcs = {
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= zynqmp_dpsub_crtc_enable_vblank,
	.disable_vblank		= zynqmp_dpsub_crtc_disable_vblank,
};

static int zynqmp_dpsub_create_crtc(struct zynqmp_dpsub *dpsub)
{
	struct drm_plane *plane = &dpsub->planes[ZYNQMP_DPSUB_LAYER_GFX];
	struct drm_crtc *crtc = &dpsub->crtc;
	int ret;

	ret = drm_crtc_init_with_planes(&dpsub->drm, crtc, plane,
					NULL, &zynqmp_dpsub_crtc_funcs, NULL);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &zynqmp_dpsub_crtc_helper_funcs);

	/* Start with vertical blanking interrupt reporting disabled. */
	drm_crtc_vblank_off(crtc);

	return 0;
}

static void zynqmp_dpsub_map_crtc_to_plane(struct zynqmp_dpsub *dpsub)
{
	u32 possible_crtcs = drm_crtc_mask(&dpsub->crtc);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dpsub->planes); i++)
		dpsub->planes[i].possible_crtcs = possible_crtcs;
}

/**
 * zynqmp_dpsub_handle_vblank - Handle the vblank event
 * @dpsub: DisplayPort subsystem
 *
 * This function handles the vblank interrupt, and sends an event to
 * CRTC object. This will be called by the DP vblank interrupt handler.
 */
void zynqmp_dpsub_handle_vblank(struct zynqmp_dpsub *dpsub)
{
	drm_crtc_handle_vblank(&dpsub->crtc);
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

int zynqmp_dpsub_kms_init(struct zynqmp_dpsub *dpsub)
{
	struct drm_encoder *encoder = &dpsub->encoder;
	struct drm_connector *connector;
	int ret;

	/*
	 * Initialize the DISP and DP components. This will creates planes,
	 * CRTC, and a bridge for the DP encoder.
	 */
	ret = zynqmp_disp_drm_init(dpsub);
	if (ret)
		return ret;

	ret = zynqmp_dpsub_create_crtc(dpsub);
	if (ret < 0)
		return ret;

	zynqmp_dpsub_map_crtc_to_plane(dpsub);

	ret = zynqmp_dp_drm_init(dpsub);
	if (ret)
		return ret;

	/* Create the encoder and attach the bridge. */
	encoder->possible_crtcs |= drm_crtc_mask(&dpsub->crtc);
	drm_simple_encoder_init(&dpsub->drm, encoder, DRM_MODE_ENCODER_NONE);

	ret = drm_bridge_attach(encoder, dpsub->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(dpsub->dev, "failed to attach bridge to encoder\n");
		return ret;
	}

	/* Create the connector for the chain of bridges. */
	connector = drm_bridge_connector_init(&dpsub->drm, encoder);
	if (IS_ERR(connector)) {
		dev_err(dpsub->dev, "failed to created connector\n");
		return PTR_ERR(connector);
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_err(dpsub->dev, "failed to attach connector to encoder\n");
		return ret;
	}

	return 0;
}
