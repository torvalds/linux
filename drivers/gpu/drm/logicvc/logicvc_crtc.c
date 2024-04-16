// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "logicvc_crtc.h"
#include "logicvc_drm.h"
#include "logicvc_interface.h"
#include "logicvc_layer.h"
#include "logicvc_regs.h"

#define logicvc_crtc(c) \
	container_of(c, struct logicvc_crtc, drm_crtc)

static enum drm_mode_status
logicvc_crtc_mode_valid(struct drm_crtc *drm_crtc,
			const struct drm_display_mode *mode)
{
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return -EINVAL;

	return 0;
}

static void logicvc_crtc_atomic_begin(struct drm_crtc *drm_crtc,
				      struct drm_atomic_state *state)
{
	struct logicvc_crtc *crtc = logicvc_crtc(drm_crtc);
	struct drm_crtc_state *old_state =
		drm_atomic_get_old_crtc_state(state, drm_crtc);
	struct drm_device *drm_dev = drm_crtc->dev;
	unsigned long flags;

	/*
	 * We need to grab the pending event here if vblank was already enabled
	 * since we won't get a call to atomic_enable to grab it.
	 */
	if (drm_crtc->state->event && old_state->active) {
		spin_lock_irqsave(&drm_dev->event_lock, flags);
		WARN_ON(drm_crtc_vblank_get(drm_crtc) != 0);

		crtc->event = drm_crtc->state->event;
		drm_crtc->state->event = NULL;

		spin_unlock_irqrestore(&drm_dev->event_lock, flags);
	}
}

static void logicvc_crtc_atomic_enable(struct drm_crtc *drm_crtc,
				       struct drm_atomic_state *state)
{
	struct logicvc_crtc *crtc = logicvc_crtc(drm_crtc);
	struct logicvc_drm *logicvc = logicvc_drm(drm_crtc->dev);
	struct drm_crtc_state *old_state =
		drm_atomic_get_old_crtc_state(state, drm_crtc);
	struct drm_crtc_state *new_state =
		drm_atomic_get_new_crtc_state(state, drm_crtc);
	struct drm_display_mode *mode = &new_state->adjusted_mode;

	struct drm_device *drm_dev = drm_crtc->dev;
	unsigned int hact, hfp, hsl, hbp;
	unsigned int vact, vfp, vsl, vbp;
	unsigned long flags;
	u32 ctrl;

	/* Timings */

	hact = mode->hdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsl = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	vact = mode->vdisplay;
	vfp = mode->vsync_start - mode->vdisplay;
	vsl = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;

	regmap_write(logicvc->regmap, LOGICVC_HSYNC_FRONT_PORCH_REG, hfp - 1);
	regmap_write(logicvc->regmap, LOGICVC_HSYNC_REG, hsl - 1);
	regmap_write(logicvc->regmap, LOGICVC_HSYNC_BACK_PORCH_REG, hbp - 1);
	regmap_write(logicvc->regmap, LOGICVC_HRES_REG, hact - 1);

	regmap_write(logicvc->regmap, LOGICVC_VSYNC_FRONT_PORCH_REG, vfp - 1);
	regmap_write(logicvc->regmap, LOGICVC_VSYNC_REG, vsl - 1);
	regmap_write(logicvc->regmap, LOGICVC_VSYNC_BACK_PORCH_REG, vbp - 1);
	regmap_write(logicvc->regmap, LOGICVC_VRES_REG, vact - 1);

	/* Signals */

	ctrl = LOGICVC_CTRL_HSYNC_ENABLE | LOGICVC_CTRL_VSYNC_ENABLE |
	       LOGICVC_CTRL_DE_ENABLE;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		ctrl |= LOGICVC_CTRL_HSYNC_INVERT;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		ctrl |= LOGICVC_CTRL_VSYNC_INVERT;

	if (logicvc->interface) {
		struct drm_connector *connector =
			&logicvc->interface->drm_connector;
		struct drm_display_info *display_info =
			&connector->display_info;

		if (display_info->bus_flags & DRM_BUS_FLAG_DE_LOW)
			ctrl |= LOGICVC_CTRL_DE_INVERT;

		if (display_info->bus_flags &
		    DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
			ctrl |= LOGICVC_CTRL_CLOCK_INVERT;
	}

	regmap_update_bits(logicvc->regmap, LOGICVC_CTRL_REG,
			   LOGICVC_CTRL_HSYNC_ENABLE |
			   LOGICVC_CTRL_HSYNC_INVERT |
			   LOGICVC_CTRL_VSYNC_ENABLE |
			   LOGICVC_CTRL_VSYNC_INVERT |
			   LOGICVC_CTRL_DE_ENABLE |
			   LOGICVC_CTRL_DE_INVERT |
			   LOGICVC_CTRL_PIXEL_INVERT |
			   LOGICVC_CTRL_CLOCK_INVERT, ctrl);

	/* Generate internal state reset. */
	regmap_write(logicvc->regmap, LOGICVC_DTYPE_REG, 0);

	drm_crtc_vblank_on(drm_crtc);

	/* Register our event after vblank is enabled. */
	if (drm_crtc->state->event && !old_state->active) {
		spin_lock_irqsave(&drm_dev->event_lock, flags);
		WARN_ON(drm_crtc_vblank_get(drm_crtc) != 0);

		crtc->event = drm_crtc->state->event;
		drm_crtc->state->event = NULL;
		spin_unlock_irqrestore(&drm_dev->event_lock, flags);
	}
}

static void logicvc_crtc_atomic_disable(struct drm_crtc *drm_crtc,
					struct drm_atomic_state *state)
{
	struct logicvc_drm *logicvc = logicvc_drm(drm_crtc->dev);
	struct drm_device *drm_dev = drm_crtc->dev;

	drm_crtc_vblank_off(drm_crtc);

	/* Disable and clear CRTC bits. */
	regmap_update_bits(logicvc->regmap, LOGICVC_CTRL_REG,
			   LOGICVC_CTRL_HSYNC_ENABLE |
			   LOGICVC_CTRL_HSYNC_INVERT |
			   LOGICVC_CTRL_VSYNC_ENABLE |
			   LOGICVC_CTRL_VSYNC_INVERT |
			   LOGICVC_CTRL_DE_ENABLE |
			   LOGICVC_CTRL_DE_INVERT |
			   LOGICVC_CTRL_PIXEL_INVERT |
			   LOGICVC_CTRL_CLOCK_INVERT, 0);

	/* Generate internal state reset. */
	regmap_write(logicvc->regmap, LOGICVC_DTYPE_REG, 0);

	/* Consume any leftover event since vblank is now disabled. */
	if (drm_crtc->state->event && !drm_crtc->state->active) {
		spin_lock_irq(&drm_dev->event_lock);

		drm_crtc_send_vblank_event(drm_crtc, drm_crtc->state->event);
		drm_crtc->state->event = NULL;
		spin_unlock_irq(&drm_dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs logicvc_crtc_helper_funcs = {
	.mode_valid		= logicvc_crtc_mode_valid,
	.atomic_begin		= logicvc_crtc_atomic_begin,
	.atomic_enable		= logicvc_crtc_atomic_enable,
	.atomic_disable		= logicvc_crtc_atomic_disable,
};

static int logicvc_crtc_enable_vblank(struct drm_crtc *drm_crtc)
{
	struct logicvc_drm *logicvc = logicvc_drm(drm_crtc->dev);

	/* Clear any pending V_SYNC interrupt. */
	regmap_write_bits(logicvc->regmap, LOGICVC_INT_STAT_REG,
			  LOGICVC_INT_STAT_V_SYNC, LOGICVC_INT_STAT_V_SYNC);

	/* Unmask V_SYNC interrupt. */
	regmap_write_bits(logicvc->regmap, LOGICVC_INT_MASK_REG,
			  LOGICVC_INT_MASK_V_SYNC, 0);

	return 0;
}

static void logicvc_crtc_disable_vblank(struct drm_crtc *drm_crtc)
{
	struct logicvc_drm *logicvc = logicvc_drm(drm_crtc->dev);

	/* Mask V_SYNC interrupt. */
	regmap_write_bits(logicvc->regmap, LOGICVC_INT_MASK_REG,
			  LOGICVC_INT_MASK_V_SYNC, LOGICVC_INT_MASK_V_SYNC);
}

static const struct drm_crtc_funcs logicvc_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= logicvc_crtc_enable_vblank,
	.disable_vblank		= logicvc_crtc_disable_vblank,
};

void logicvc_crtc_vblank_handler(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct logicvc_crtc *crtc = logicvc->crtc;
	unsigned long flags;

	if (!crtc)
		return;

	drm_crtc_handle_vblank(&crtc->drm_crtc);

	if (crtc->event) {
		spin_lock_irqsave(&drm_dev->event_lock, flags);
		drm_crtc_send_vblank_event(&crtc->drm_crtc, crtc->event);
		drm_crtc_vblank_put(&crtc->drm_crtc);
		crtc->event = NULL;
		spin_unlock_irqrestore(&drm_dev->event_lock, flags);
	}
}

int logicvc_crtc_init(struct logicvc_drm *logicvc)
{
	struct drm_device *drm_dev = &logicvc->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct logicvc_crtc *crtc;
	struct logicvc_layer *layer_primary;
	int ret;

	crtc = devm_kzalloc(dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	layer_primary = logicvc_layer_get_primary(logicvc);
	if (!layer_primary) {
		drm_err(drm_dev, "Failed to get primary layer\n");
		return -EINVAL;
	}

	ret = drm_crtc_init_with_planes(drm_dev, &crtc->drm_crtc,
					&layer_primary->drm_plane, NULL,
					&logicvc_crtc_funcs, NULL);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize CRTC\n");
		return ret;
	}

	drm_crtc_helper_add(&crtc->drm_crtc, &logicvc_crtc_helper_funcs);

	crtc->drm_crtc.port = of_graph_get_port_by_id(of_node, 1);

	logicvc->crtc = crtc;

	return 0;
}
