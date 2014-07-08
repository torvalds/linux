/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include <drm/exynos_drm.h>
#include "exynos_drm_drv.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_connector.h"

#define to_exynos_connector(x)	container_of(x, struct exynos_drm_connector,\
				drm_connector)

struct exynos_drm_connector {
	struct drm_connector		drm_connector;
	uint32_t			encoder_id;
	struct exynos_drm_display	*display;
};

static int exynos_drm_connector_get_modes(struct drm_connector *connector)
{
	struct exynos_drm_connector *exynos_connector =
					to_exynos_connector(connector);
	struct exynos_drm_display *display = exynos_connector->display;
	struct edid *edid = NULL;
	unsigned int count = 0;
	int ret;

	/*
	 * if get_edid() exists then get_edid() callback of hdmi side
	 * is called to get edid data through i2c interface else
	 * get timing from the FIMD driver(display controller).
	 *
	 * P.S. in case of lcd panel, count is always 1 if success
	 * because lcd panel has only one mode.
	 */
	if (display->ops->get_edid) {
		edid = display->ops->get_edid(display, connector);
		if (IS_ERR_OR_NULL(edid)) {
			ret = PTR_ERR(edid);
			edid = NULL;
			DRM_ERROR("Panel operation get_edid failed %d\n", ret);
			goto out;
		}

		count = drm_add_edid_modes(connector, edid);
		if (!count) {
			DRM_ERROR("Add edid modes failed %d\n", count);
			goto out;
		}

		drm_mode_connector_update_edid_property(connector, edid);
	} else {
		struct exynos_drm_panel_info *panel;
		struct drm_display_mode *mode = drm_mode_create(connector->dev);
		if (!mode) {
			DRM_ERROR("failed to create a new display mode.\n");
			return 0;
		}

		if (display->ops->get_panel)
			panel = display->ops->get_panel(display);
		else {
			drm_mode_destroy(connector->dev, mode);
			return 0;
		}

		drm_display_mode_from_videomode(&panel->vm, mode);
		mode->width_mm = panel->width_mm;
		mode->height_mm = panel->height_mm;
		connector->display_info.width_mm = mode->width_mm;
		connector->display_info.height_mm = mode->height_mm;

		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);

		count = 1;
	}

out:
	kfree(edid);
	return count;
}

static int exynos_drm_connector_mode_valid(struct drm_connector *connector,
					    struct drm_display_mode *mode)
{
	struct exynos_drm_connector *exynos_connector =
					to_exynos_connector(connector);
	struct exynos_drm_display *display = exynos_connector->display;
	int ret = MODE_BAD;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (display->ops->check_mode)
		if (!display->ops->check_mode(display, mode))
			ret = MODE_OK;

	return ret;
}

static struct drm_encoder *exynos_drm_best_encoder(
		struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct exynos_drm_connector *exynos_connector =
					to_exynos_connector(connector);
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	obj = drm_mode_object_find(dev, exynos_connector->encoder_id,
				   DRM_MODE_OBJECT_ENCODER);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown ENCODER ID %d\n",
				exynos_connector->encoder_id);
		return NULL;
	}

	encoder = obj_to_encoder(obj);

	return encoder;
}

static struct drm_connector_helper_funcs exynos_connector_helper_funcs = {
	.get_modes	= exynos_drm_connector_get_modes,
	.mode_valid	= exynos_drm_connector_mode_valid,
	.best_encoder	= exynos_drm_best_encoder,
};

static int exynos_drm_connector_fill_modes(struct drm_connector *connector,
				unsigned int max_width, unsigned int max_height)
{
	struct exynos_drm_connector *exynos_connector =
					to_exynos_connector(connector);
	struct exynos_drm_display *display = exynos_connector->display;
	unsigned int width, height;

	width = max_width;
	height = max_height;

	/*
	 * if specific driver want to find desired_mode using maxmum
	 * resolution then get max width and height from that driver.
	 */
	if (display->ops->get_max_resol)
		display->ops->get_max_resol(display, &width, &height);

	return drm_helper_probe_single_connector_modes(connector, width,
							height);
}

/* get detection status of display device. */
static enum drm_connector_status
exynos_drm_connector_detect(struct drm_connector *connector, bool force)
{
	struct exynos_drm_connector *exynos_connector =
					to_exynos_connector(connector);
	struct exynos_drm_display *display = exynos_connector->display;
	enum drm_connector_status status = connector_status_disconnected;

	if (display->ops->is_connected) {
		if (display->ops->is_connected(display))
			status = connector_status_connected;
		else
			status = connector_status_disconnected;
	}

	return status;
}

static void exynos_drm_connector_destroy(struct drm_connector *connector)
{
	struct exynos_drm_connector *exynos_connector =
		to_exynos_connector(connector);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(exynos_connector);
}

static struct drm_connector_funcs exynos_connector_funcs = {
	.dpms		= drm_helper_connector_dpms,
	.fill_modes	= exynos_drm_connector_fill_modes,
	.detect		= exynos_drm_connector_detect,
	.destroy	= exynos_drm_connector_destroy,
};

struct drm_connector *exynos_drm_connector_create(struct drm_device *dev,
						   struct drm_encoder *encoder)
{
	struct exynos_drm_connector *exynos_connector;
	struct exynos_drm_display *display = exynos_drm_get_display(encoder);
	struct drm_connector *connector;
	int type;
	int err;

	exynos_connector = kzalloc(sizeof(*exynos_connector), GFP_KERNEL);
	if (!exynos_connector)
		return NULL;

	connector = &exynos_connector->drm_connector;

	switch (display->type) {
	case EXYNOS_DISPLAY_TYPE_HDMI:
		type = DRM_MODE_CONNECTOR_HDMIA;
		connector->interlace_allowed = true;
		connector->polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case EXYNOS_DISPLAY_TYPE_VIDI:
		type = DRM_MODE_CONNECTOR_VIRTUAL;
		connector->polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		type = DRM_MODE_CONNECTOR_Unknown;
		break;
	}

	drm_connector_init(dev, connector, &exynos_connector_funcs, type);
	drm_connector_helper_add(connector, &exynos_connector_helper_funcs);

	err = drm_connector_register(connector);
	if (err)
		goto err_connector;

	exynos_connector->encoder_id = encoder->base.id;
	exynos_connector->display = display;
	connector->dpms = DRM_MODE_DPMS_OFF;
	connector->encoder = encoder;

	err = drm_mode_connector_attach_encoder(connector, encoder);
	if (err) {
		DRM_ERROR("failed to attach a connector to a encoder\n");
		goto err_sysfs;
	}

	DRM_DEBUG_KMS("connector has been created\n");

	return connector;

err_sysfs:
	drm_connector_unregister(connector);
err_connector:
	drm_connector_cleanup(connector);
	kfree(exynos_connector);
	return NULL;
}
