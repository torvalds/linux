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

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_plane.h>
#include <drm/drm_probe_helper.h>
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

static inline struct zynqmp_dpsub *to_zynqmp_dpsub(struct drm_device *drm)
{
	return container_of(drm, struct zynqmp_dpsub_drm, dev)->dpsub;
}

/* -----------------------------------------------------------------------------
 * DRM Planes
 */

static int zynqmp_dpsub_plane_atomic_check(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc_state *crtc_state;

	if (!new_plane_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state, new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, false);
}

static void zynqmp_dpsub_plane_atomic_disable(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   plane);
	struct zynqmp_dpsub *dpsub = to_zynqmp_dpsub(plane->dev);
	struct zynqmp_disp_layer *layer = dpsub->layers[plane->index];

	if (!old_state->fb)
		return;

	zynqmp_disp_layer_disable(layer);

	if (plane->index == ZYNQMP_DPSUB_LAYER_GFX)
		zynqmp_disp_blend_set_global_alpha(dpsub->disp, false,
						   plane->state->alpha >> 8);
}

static void zynqmp_dpsub_plane_atomic_update(struct drm_plane *plane,
					     struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state, plane);
	struct zynqmp_dpsub *dpsub = to_zynqmp_dpsub(plane->dev);
	struct zynqmp_disp_layer *layer = dpsub->layers[plane->index];
	bool format_changed = false;

	if (!old_state->fb ||
	    old_state->fb->format->format != new_state->fb->format->format)
		format_changed = true;

	/*
	 * If the format has changed (including going from a previously
	 * disabled state to any format), reconfigure the format. Disable the
	 * plane first if needed.
	 */
	if (format_changed) {
		if (old_state->fb)
			zynqmp_disp_layer_disable(layer);

		zynqmp_disp_layer_set_format(layer, new_state->fb->format);
	}

	zynqmp_disp_layer_update(layer, new_state);

	if (plane->index == ZYNQMP_DPSUB_LAYER_GFX)
		zynqmp_disp_blend_set_global_alpha(dpsub->disp, true,
						   plane->state->alpha >> 8);

	/*
	 * Unconditionally enable the layer, as it may have been disabled
	 * previously either explicitly to reconfigure layer format, or
	 * implicitly after DPSUB reset during display mode change. DRM
	 * framework calls this callback for enabled planes only.
	 */
	zynqmp_disp_layer_enable(layer);
}

static const struct drm_plane_helper_funcs zynqmp_dpsub_plane_helper_funcs = {
	.atomic_check		= zynqmp_dpsub_plane_atomic_check,
	.atomic_update		= zynqmp_dpsub_plane_atomic_update,
	.atomic_disable		= zynqmp_dpsub_plane_atomic_disable,
};

static const struct drm_plane_funcs zynqmp_dpsub_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int zynqmp_dpsub_create_planes(struct zynqmp_dpsub *dpsub)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(dpsub->drm->planes); i++) {
		struct zynqmp_disp_layer *layer = dpsub->layers[i];
		struct drm_plane *plane = &dpsub->drm->planes[i];
		enum drm_plane_type type;
		unsigned int num_formats;
		u32 *formats;

		formats = zynqmp_disp_layer_drm_formats(layer, &num_formats);
		if (!formats)
			return -ENOMEM;

		/* Graphics layer is primary, and video layer is overlay. */
		type = i == ZYNQMP_DPSUB_LAYER_VID
		     ? DRM_PLANE_TYPE_OVERLAY : DRM_PLANE_TYPE_PRIMARY;
		ret = drm_universal_plane_init(&dpsub->drm->dev, plane, 0,
					       &zynqmp_dpsub_plane_funcs,
					       formats, num_formats,
					       NULL, type, NULL);
		kfree(formats);
		if (ret)
			return ret;

		drm_plane_helper_add(plane, &zynqmp_dpsub_plane_helper_funcs);

		drm_plane_create_zpos_immutable_property(plane, i);
		if (i == ZYNQMP_DPSUB_LAYER_GFX)
			drm_plane_create_alpha_property(plane);
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * DRM CRTC
 */

static inline struct zynqmp_dpsub *crtc_to_dpsub(struct drm_crtc *crtc)
{
	return container_of(crtc, struct zynqmp_dpsub_drm, crtc)->dpsub;
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
		zynqmp_dpsub_plane_atomic_disable(crtc->primary, state);

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
	struct drm_plane *plane = &dpsub->drm->planes[ZYNQMP_DPSUB_LAYER_GFX];
	struct drm_crtc *crtc = &dpsub->drm->crtc;
	int ret;

	ret = drm_crtc_init_with_planes(&dpsub->drm->dev, crtc, plane,
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
	u32 possible_crtcs = drm_crtc_mask(&dpsub->drm->crtc);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dpsub->drm->planes); i++)
		dpsub->drm->planes[i].possible_crtcs = possible_crtcs;
}

/**
 * zynqmp_dpsub_drm_handle_vblank - Handle the vblank event
 * @dpsub: DisplayPort subsystem
 *
 * This function handles the vblank interrupt, and sends an event to
 * CRTC object. This will be called by the DP vblank interrupt handler.
 */
void zynqmp_dpsub_drm_handle_vblank(struct zynqmp_dpsub *dpsub)
{
	drm_crtc_handle_vblank(&dpsub->drm->crtc);
}

/* -----------------------------------------------------------------------------
 * Dumb Buffer & Framebuffer Allocation
 */

static int zynqmp_dpsub_dumb_create(struct drm_file *file_priv,
				    struct drm_device *drm,
				    struct drm_mode_create_dumb *args)
{
	struct zynqmp_dpsub *dpsub = to_zynqmp_dpsub(drm);
	unsigned int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/* Enforce the alignment constraints of the DMA engine. */
	args->pitch = ALIGN(pitch, dpsub->dma_align);

	return drm_gem_dma_dumb_create_internal(file_priv, drm, args);
}

static struct drm_framebuffer *
zynqmp_dpsub_fb_create(struct drm_device *drm, struct drm_file *file_priv,
		       const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct zynqmp_dpsub *dpsub = to_zynqmp_dpsub(drm);
	struct drm_mode_fb_cmd2 cmd = *mode_cmd;
	unsigned int i;

	/* Enforce the alignment constraints of the DMA engine. */
	for (i = 0; i < ARRAY_SIZE(cmd.pitches); ++i)
		cmd.pitches[i] = ALIGN(cmd.pitches[i], dpsub->dma_align);

	return drm_gem_fb_create(drm, file_priv, &cmd);
}

static const struct drm_mode_config_funcs zynqmp_dpsub_mode_config_funcs = {
	.fb_create		= zynqmp_dpsub_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

/* -----------------------------------------------------------------------------
 * DRM/KMS Driver
 */

DEFINE_DRM_GEM_DMA_FOPS(zynqmp_dpsub_drm_fops);

static const struct drm_driver zynqmp_dpsub_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_ATOMIC,

	DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(zynqmp_dpsub_dumb_create),
	DRM_FBDEV_DMA_DRIVER_OPS,

	.fops				= &zynqmp_dpsub_drm_fops,

	.name				= "zynqmp-dpsub",
	.desc				= "Xilinx DisplayPort Subsystem Driver",
	.major				= 1,
	.minor				= 0,
};

static int zynqmp_dpsub_kms_init(struct zynqmp_dpsub *dpsub)
{
	struct drm_encoder *encoder = &dpsub->drm->encoder;
	struct drm_connector *connector;
	int ret;

	/* Create the planes and the CRTC. */
	ret = zynqmp_dpsub_create_planes(dpsub);
	if (ret)
		return ret;

	ret = zynqmp_dpsub_create_crtc(dpsub);
	if (ret < 0)
		return ret;

	zynqmp_dpsub_map_crtc_to_plane(dpsub);

	/* Create the encoder and attach the bridge. */
	encoder->possible_crtcs |= drm_crtc_mask(&dpsub->drm->crtc);
	drm_simple_encoder_init(&dpsub->drm->dev, encoder, DRM_MODE_ENCODER_NONE);

	ret = drm_bridge_attach(encoder, dpsub->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(dpsub->dev, "failed to attach bridge to encoder\n");
		goto err_encoder;
	}

	/* Create the connector for the chain of bridges. */
	connector = drm_bridge_connector_init(&dpsub->drm->dev, encoder);
	if (IS_ERR(connector)) {
		dev_err(dpsub->dev, "failed to created connector\n");
		ret = PTR_ERR(connector);
		goto err_encoder;
	}

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0) {
		dev_err(dpsub->dev, "failed to attach connector to encoder\n");
		goto err_encoder;
	}

	return 0;

err_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void zynqmp_dpsub_drm_release(struct drm_device *drm, void *res)
{
	struct zynqmp_dpsub_drm *dpdrm = res;

	zynqmp_dpsub_release(dpdrm->dpsub);
}

int zynqmp_dpsub_drm_init(struct zynqmp_dpsub *dpsub)
{
	struct zynqmp_dpsub_drm *dpdrm;
	struct drm_device *drm;
	int ret;

	/*
	 * Allocate the drm_device and immediately add a cleanup action to
	 * release the zynqmp_dpsub instance. If any of those operations fail,
	 * dpsub->drm will remain NULL, which tells the caller that it must
	 * cleanup manually.
	 */
	dpdrm = devm_drm_dev_alloc(dpsub->dev, &zynqmp_dpsub_drm_driver,
				   struct zynqmp_dpsub_drm, dev);
	if (IS_ERR(dpdrm))
		return PTR_ERR(dpdrm);

	dpdrm->dpsub = dpsub;
	drm = &dpdrm->dev;

	ret = drmm_add_action(drm, zynqmp_dpsub_drm_release, dpdrm);
	if (ret < 0)
		return ret;

	dpsub->drm = dpdrm;

	/* Initialize mode config, vblank and the KMS poll helper. */
	ret = drmm_mode_config_init(drm);
	if (ret < 0)
		return ret;

	drm->mode_config.funcs = &zynqmp_dpsub_mode_config_funcs;
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = ZYNQMP_DISP_MAX_WIDTH;
	drm->mode_config.max_height = ZYNQMP_DISP_MAX_HEIGHT;

	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;

	ret = zynqmp_dpsub_kms_init(dpsub);
	if (ret < 0)
		goto err_poll_fini;

	drm_kms_helper_poll_init(drm);

	/* Reset all components and register the DRM device. */
	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_poll_fini;

	/* Initialize fbdev generic emulation. */
	drm_client_setup_with_fourcc(drm, DRM_FORMAT_RGB888);

	return 0;

err_poll_fini:
	drm_kms_helper_poll_fini(drm);
	return ret;
}

void zynqmp_dpsub_drm_cleanup(struct zynqmp_dpsub *dpsub)
{
	struct drm_device *drm = &dpsub->drm->dev;

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
	drm_encoder_cleanup(&dpsub->drm->encoder);
	drm_kms_helper_poll_fini(drm);
}
