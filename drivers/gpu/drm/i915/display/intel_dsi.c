// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

#include <drm/drm_mipi_dsi.h>
#include "intel_dsi.h"
#include "intel_panel.h"

int intel_dsi_bitrate(const struct intel_dsi *intel_dsi)
{
	int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

	if (WARN_ON(bpp < 0))
		bpp = 16;

	return intel_dsi->pclk * bpp / intel_dsi->lane_count;
}

int intel_dsi_tlpx_ns(const struct intel_dsi *intel_dsi)
{
	switch (intel_dsi->escape_clk_div) {
	default:
	case 0:
		return 50;
	case 1:
		return 100;
	case 2:
		return 200;
	}
}

int intel_dsi_get_modes(struct drm_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_display_mode *mode;

	drm_dbg_kms(&i915->drm, "\n");

	if (!intel_connector->panel.fixed_mode) {
		drm_dbg_kms(&i915->drm, "no fixed mode\n");
		return 0;
	}

	mode = drm_mode_duplicate(connector->dev,
				  intel_connector->panel.fixed_mode);
	if (!mode) {
		drm_dbg_kms(&i915->drm, "drm_mode_duplicate failed\n");
		return 0;
	}

	drm_mode_probed_add(connector, mode);
	return 1;
}

enum drm_mode_status intel_dsi_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	const struct drm_display_mode *fixed_mode = intel_connector->panel.fixed_mode;
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;
	enum drm_mode_status status;

	drm_dbg_kms(&dev_priv->drm, "\n");

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	status = intel_panel_mode_valid(intel_connector, mode);
	if (status != MODE_OK)
		return status;

	if (fixed_mode->clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	return intel_mode_valid_max_plane_size(dev_priv, mode, false);
}

struct intel_dsi_host *intel_dsi_host_init(struct intel_dsi *intel_dsi,
					   const struct mipi_dsi_host_ops *funcs,
					   enum port port)
{
	struct intel_dsi_host *host;
	struct mipi_dsi_device *device;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return NULL;

	host->base.ops = funcs;
	host->intel_dsi = intel_dsi;
	host->port = port;

	/*
	 * We should call mipi_dsi_host_register(&host->base) here, but we don't
	 * have a host->dev, and we don't have OF stuff either. So just use the
	 * dsi framework as a library and hope for the best. Create the dsi
	 * devices by ourselves here too. Need to be careful though, because we
	 * don't initialize any of the driver model devices here.
	 */
	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device) {
		kfree(host);
		return NULL;
	}

	device->host = &host->base;
	host->device = device;

	return host;
}

enum drm_panel_orientation
intel_dsi_get_panel_orientation(struct intel_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	enum drm_panel_orientation orientation;

	orientation = dev_priv->vbt.dsi.orientation;
	if (orientation != DRM_MODE_PANEL_ORIENTATION_UNKNOWN)
		return orientation;

	orientation = dev_priv->vbt.orientation;
	if (orientation != DRM_MODE_PANEL_ORIENTATION_UNKNOWN)
		return orientation;

	return DRM_MODE_PANEL_ORIENTATION_NORMAL;
}
