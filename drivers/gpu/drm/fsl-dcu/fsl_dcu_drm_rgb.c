// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 */

#include <linux/backlight.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "fsl_dcu_drm_drv.h"
#include "fsl_tcon.h"

int fsl_dcu_drm_encoder_create(struct fsl_dcu_drm_device *fsl_dev,
			       struct drm_crtc *crtc)
{
	struct drm_encoder *encoder = &fsl_dev->encoder;
	int ret;

	encoder->possible_crtcs = 1;

	/* Use bypass mode for parallel RGB/LVDS encoder */
	if (fsl_dev->tcon)
		fsl_tcon_bypass_enable(fsl_dev->tcon);

	ret = drm_simple_encoder_init(fsl_dev->drm, encoder,
				      DRM_MODE_ENCODER_LVDS);
	if (ret < 0)
		return ret;

	return 0;
}

static void fsl_dcu_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs fsl_dcu_drm_connector_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.destroy = fsl_dcu_drm_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.reset = drm_atomic_helper_connector_reset,
};

static int fsl_dcu_drm_connector_get_modes(struct drm_connector *connector)
{
	struct fsl_dcu_drm_connector *fsl_connector;

	fsl_connector = to_fsl_dcu_connector(connector);
	return drm_panel_get_modes(fsl_connector->panel, connector);
}

static enum drm_mode_status
fsl_dcu_drm_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	if (mode->hdisplay & 0xf)
		return MODE_ERROR;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = fsl_dcu_drm_connector_get_modes,
	.mode_valid = fsl_dcu_drm_connector_mode_valid,
};

static int fsl_dcu_attach_panel(struct fsl_dcu_drm_device *fsl_dev,
				 struct drm_panel *panel)
{
	struct drm_encoder *encoder = &fsl_dev->encoder;
	struct drm_connector *connector = &fsl_dev->connector.base;
	int ret;

	fsl_dev->connector.encoder = encoder;

	ret = drm_connector_init(fsl_dev->drm, connector,
				 &fsl_dcu_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret < 0)
		goto err_cleanup;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		goto err_sysfs;

	return 0;

err_sysfs:
	drm_connector_unregister(connector);
err_cleanup:
	drm_connector_cleanup(connector);
	return ret;
}

int fsl_dcu_create_outputs(struct fsl_dcu_drm_device *fsl_dev)
{
	struct device_node *panel_node;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	int ret;

	/* This is for backward compatibility */
	panel_node = of_parse_phandle(fsl_dev->np, "fsl,panel", 0);
	if (panel_node) {
		fsl_dev->connector.panel = of_drm_find_panel(panel_node);
		of_node_put(panel_node);
		if (IS_ERR(fsl_dev->connector.panel))
			return PTR_ERR(fsl_dev->connector.panel);

		return fsl_dcu_attach_panel(fsl_dev, fsl_dev->connector.panel);
	}

	ret = drm_of_find_panel_or_bridge(fsl_dev->np, 0, 0, &panel, &bridge);
	if (ret)
		return ret;

	if (panel) {
		fsl_dev->connector.panel = panel;
		return fsl_dcu_attach_panel(fsl_dev, panel);
	}

	return drm_bridge_attach(&fsl_dev->encoder, bridge, NULL, 0);
}
