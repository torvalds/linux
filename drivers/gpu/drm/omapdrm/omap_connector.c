// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_probe_helper.h>

#include "omap_drv.h"

/*
 * connector funcs
 */

#define to_omap_connector(x) container_of(x, struct omap_connector, base)

struct omap_connector {
	struct drm_connector base;
	struct omap_dss_device *output;
};

static enum drm_connector_status omap_connector_detect(
		struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void omap_connector_destroy(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);

	DBG("%s", connector->name);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	omapdss_device_put(omap_connector->output);

	kfree(omap_connector);
}

static int omap_connector_get_modes(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = NULL;
	struct omap_dss_device *d;

	DBG("%s", connector->name);

	/*
	 * If the display pipeline reports modes (e.g. with a fixed resolution
	 * panel or an analog TV output), query it.
	 */
	for (d = omap_connector->output; d; d = d->next) {
		if (d->ops_flags & OMAP_DSS_DEVICE_OP_MODES)
			dssdev = d;
	}

	if (dssdev)
		return dssdev->ops->get_modes(dssdev, connector);

	/* We can't retrieve modes. The KMS core will add the default modes. */
	return 0;
}

enum drm_mode_status omap_connector_mode_fixup(struct omap_dss_device *dssdev,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	int ret;

	drm_mode_copy(adjusted_mode, mode);

	for (; dssdev; dssdev = dssdev->next) {
		if (!dssdev->ops || !dssdev->ops->check_timings)
			continue;

		ret = dssdev->ops->check_timings(dssdev, adjusted_mode);
		if (ret)
			return MODE_BAD;
	}

	return MODE_OK;
}

static enum drm_mode_status omap_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct drm_display_mode new_mode = { { 0 } };
	enum drm_mode_status status;

	status = omap_connector_mode_fixup(omap_connector->output, mode,
					   &new_mode);
	if (status != MODE_OK)
		goto done;

	/* Check if vrefresh is still valid. */
	if (drm_mode_vrefresh(mode) != drm_mode_vrefresh(&new_mode))
		status = MODE_NOCLOCK;

done:
	DBG("connector: mode %s: " DRM_MODE_FMT,
			(status == MODE_OK) ? "valid" : "invalid",
			DRM_MODE_ARG(mode));

	return status;
}

static const struct drm_connector_funcs omap_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = omap_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = omap_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs omap_connector_helper_funcs = {
	.get_modes = omap_connector_get_modes,
	.mode_valid = omap_connector_mode_valid,
};

/* initialize connector */
struct drm_connector *omap_connector_init(struct drm_device *dev,
					  struct omap_dss_device *output,
					  struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;
	struct omap_connector *omap_connector;

	DBG("%s", output->name);

	omap_connector = kzalloc(sizeof(*omap_connector), GFP_KERNEL);
	if (!omap_connector)
		goto fail;

	omap_connector->output = omapdss_device_get(output);

	connector = &omap_connector->base;
	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	drm_connector_init(dev, connector, &omap_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);
	drm_connector_helper_add(connector, &omap_connector_helper_funcs);

	return connector;

fail:
	if (connector)
		omap_connector_destroy(connector);

	return NULL;
}
