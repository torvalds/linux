// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include "omap_drv.h"

/*
 * connector funcs
 */

#define to_omap_connector(x) container_of(x, struct omap_connector, base)

struct omap_connector {
	struct drm_connector base;
	struct omap_dss_device *output;
	struct omap_dss_device *hpd;
	bool hdmi_mode;
};

static void omap_connector_hpd_notify(struct drm_connector *connector,
				      enum drm_connector_status status)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev;

	if (status != connector_status_disconnected)
		return;

	/*
	 * Notify all devics in the pipeline of disconnection. This is required
	 * to let the HDMI encoders reset their internal state related to
	 * connection status, such as the CEC address.
	 */
	for (dssdev = omap_connector->output; dssdev; dssdev = dssdev->next) {
		if (dssdev->ops && dssdev->ops->hdmi.lost_hotplug)
			dssdev->ops->hdmi.lost_hotplug(dssdev);
	}
}

static void omap_connector_hpd_cb(void *cb_data,
				  enum drm_connector_status status)
{
	struct omap_connector *omap_connector = cb_data;
	struct drm_connector *connector = &omap_connector->base;
	struct drm_device *dev = connector->dev;
	enum drm_connector_status old_status;

	mutex_lock(&dev->mode_config.mutex);
	old_status = connector->status;
	connector->status = status;
	mutex_unlock(&dev->mode_config.mutex);

	if (old_status == status)
		return;

	omap_connector_hpd_notify(connector, status);

	drm_kms_helper_hotplug_event(dev);
}

void omap_connector_enable_hpd(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *hpd = omap_connector->hpd;

	if (hpd)
		hpd->ops->register_hpd_cb(hpd, omap_connector_hpd_cb,
					  omap_connector);
}

void omap_connector_disable_hpd(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *hpd = omap_connector->hpd;

	if (hpd)
		hpd->ops->unregister_hpd_cb(hpd);
}

bool omap_connector_get_hdmi_mode(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);

	return omap_connector->hdmi_mode;
}

static struct omap_dss_device *
omap_connector_find_device(struct drm_connector *connector,
			   enum omap_dss_device_ops_flag op)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = NULL;
	struct omap_dss_device *d;

	for (d = omap_connector->output; d; d = d->next) {
		if (d->ops_flags & op)
			dssdev = d;
	}

	return dssdev;
}

static enum drm_connector_status omap_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct omap_dss_device *dssdev;
	enum drm_connector_status status;

	dssdev = omap_connector_find_device(connector,
					    OMAP_DSS_DEVICE_OP_DETECT);

	if (dssdev) {
		status = dssdev->ops->detect(dssdev)
		       ? connector_status_connected
		       : connector_status_disconnected;

		omap_connector_hpd_notify(connector, status);
	} else {
		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_DPI:
		case DRM_MODE_CONNECTOR_LVDS:
		case DRM_MODE_CONNECTOR_DSI:
			status = connector_status_connected;
			break;
		default:
			status = connector_status_unknown;
			break;
		}
	}

	VERB("%s: %d (force=%d)", connector->name, status, force);

	return status;
}

static void omap_connector_destroy(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);

	DBG("%s", connector->name);

	if (omap_connector->hpd) {
		struct omap_dss_device *hpd = omap_connector->hpd;

		hpd->ops->unregister_hpd_cb(hpd);
		omapdss_device_put(hpd);
		omap_connector->hpd = NULL;
	}

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	omapdss_device_put(omap_connector->output);

	kfree(omap_connector);
}

#define MAX_EDID  512

static int omap_connector_get_modes_edid(struct drm_connector *connector,
					 struct omap_dss_device *dssdev)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	enum drm_connector_status status;
	void *edid;
	int n;

	status = omap_connector_detect(connector, false);
	if (status != connector_status_connected)
		goto no_edid;

	edid = kzalloc(MAX_EDID, GFP_KERNEL);
	if (!edid)
		goto no_edid;

	if (dssdev->ops->read_edid(dssdev, edid, MAX_EDID) <= 0 ||
	    !drm_edid_is_valid(edid)) {
		kfree(edid);
		goto no_edid;
	}

	drm_connector_update_edid_property(connector, edid);
	n = drm_add_edid_modes(connector, edid);

	omap_connector->hdmi_mode = drm_detect_hdmi_monitor(edid);

	kfree(edid);
	return n;

no_edid:
	drm_connector_update_edid_property(connector, NULL);
	return 0;
}

static int omap_connector_get_modes(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev;

	DBG("%s", connector->name);

	/*
	 * If display exposes EDID, then we parse that in the normal way to
	 * build table of supported modes.
	 */
	dssdev = omap_connector_find_device(connector,
					    OMAP_DSS_DEVICE_OP_EDID);
	if (dssdev)
		return omap_connector_get_modes_edid(connector, dssdev);

	/*
	 * Otherwise if the display pipeline reports modes (e.g. with a fixed
	 * resolution panel or an analog TV output), query it.
	 */
	dssdev = omap_connector_find_device(connector,
					    OMAP_DSS_DEVICE_OP_MODES);
	if (dssdev)
		return dssdev->ops->get_modes(dssdev, connector);

	/*
	 * Otherwise if the display pipeline uses a drm_panel, we delegate the
	 * operation to the panel API.
	 */
	if (omap_connector->output->panel)
		return drm_panel_get_modes(omap_connector->output->panel,
					   connector);

	/*
	 * We can't retrieve modes, which can happen for instance for a DVI or
	 * VGA output with the DDC bus unconnected. The KMS core will add the
	 * default modes.
	 */
	return 0;
}

enum drm_mode_status omap_connector_mode_fixup(struct omap_dss_device *dssdev,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	int ret;

	drm_mode_copy(adjusted_mode, mode);

	for (; dssdev; dssdev = dssdev->next) {
		if (!dssdev->ops->check_timings)
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

static int omap_connector_get_type(struct omap_dss_device *output)
{
	struct omap_dss_device *display;
	enum omap_display_type type;

	display = omapdss_display_get(output);
	type = display->type;
	omapdss_device_put(display);

	switch (type) {
	case OMAP_DISPLAY_TYPE_HDMI:
		return DRM_MODE_CONNECTOR_HDMIA;
	case OMAP_DISPLAY_TYPE_DVI:
		return DRM_MODE_CONNECTOR_DVID;
	case OMAP_DISPLAY_TYPE_DSI:
		return DRM_MODE_CONNECTOR_DSI;
	case OMAP_DISPLAY_TYPE_DPI:
	case OMAP_DISPLAY_TYPE_DBI:
		return DRM_MODE_CONNECTOR_DPI;
	case OMAP_DISPLAY_TYPE_VENC:
		/* TODO: This could also be composite */
		return DRM_MODE_CONNECTOR_SVIDEO;
	case OMAP_DISPLAY_TYPE_SDI:
		return DRM_MODE_CONNECTOR_LVDS;
	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

/* initialize connector */
struct drm_connector *omap_connector_init(struct drm_device *dev,
					  struct omap_dss_device *output,
					  struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;
	struct omap_connector *omap_connector;
	struct omap_dss_device *dssdev;

	DBG("%s", output->name);

	omap_connector = kzalloc(sizeof(*omap_connector), GFP_KERNEL);
	if (!omap_connector)
		goto fail;

	omap_connector->output = omapdss_device_get(output);

	connector = &omap_connector->base;
	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	drm_connector_init(dev, connector, &omap_connector_funcs,
			   omap_connector_get_type(output));
	drm_connector_helper_add(connector, &omap_connector_helper_funcs);

	/*
	 * Initialize connector status handling. First try to find a device that
	 * supports hot-plug reporting. If it fails, fall back to a device that
	 * support polling. If that fails too, we don't support hot-plug
	 * detection at all.
	 */
	dssdev = omap_connector_find_device(connector, OMAP_DSS_DEVICE_OP_HPD);
	if (dssdev) {
		omap_connector->hpd = omapdss_device_get(dssdev);
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	} else {
		dssdev = omap_connector_find_device(connector,
						    OMAP_DSS_DEVICE_OP_DETECT);
		if (dssdev)
			connector->polled = DRM_CONNECTOR_POLL_CONNECT |
					    DRM_CONNECTOR_POLL_DISCONNECT;
	}

	return connector;

fail:
	if (connector)
		omap_connector_destroy(connector);

	return NULL;
}
