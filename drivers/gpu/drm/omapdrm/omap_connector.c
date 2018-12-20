/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "omap_drv.h"

/*
 * connector funcs
 */

#define to_omap_connector(x) container_of(x, struct omap_connector, base)

struct omap_connector {
	struct drm_connector base;
	struct omap_dss_device *output;
	struct omap_dss_device *display;
	struct omap_dss_device *hpd;
	bool hdmi_mode;
};

static void omap_connector_hpd_notify(struct drm_connector *connector,
				      struct omap_dss_device *src,
				      enum drm_connector_status status)
{
	if (status == connector_status_disconnected) {
		/*
		 * If the source is an HDMI encoder, notify it of disconnection.
		 * This is required to let the HDMI encoder reset any internal
		 * state related to connection status, such as the CEC address.
		 */
		if (src && src->type == OMAP_DISPLAY_TYPE_HDMI &&
		    src->ops->hdmi.lost_hotplug)
			src->ops->hdmi.lost_hotplug(src);
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

	omap_connector_hpd_notify(connector, omap_connector->hpd, status);

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
	struct omap_dss_device *dssdev;

	for (dssdev = omap_connector->display; dssdev; dssdev = dssdev->src) {
		if (dssdev->ops_flags & op)
			return dssdev;
	}

	return NULL;
}

static enum drm_connector_status omap_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev;
	enum drm_connector_status status;

	dssdev = omap_connector_find_device(connector,
					    OMAP_DSS_DEVICE_OP_DETECT);

	if (dssdev) {
		status = dssdev->ops->detect(dssdev)
		       ? connector_status_connected
		       : connector_status_disconnected;

		omap_connector_hpd_notify(connector, dssdev->src, status);
	} else {
		switch (omap_connector->display->type) {
		case OMAP_DISPLAY_TYPE_DPI:
		case OMAP_DISPLAY_TYPE_DBI:
		case OMAP_DISPLAY_TYPE_SDI:
		case OMAP_DISPLAY_TYPE_DSI:
			status = connector_status_connected;
			break;
		default:
			status = connector_status_unknown;
			break;
		}
	}

	VERB("%s: %d (force=%d)", omap_connector->display->name, status, force);

	return status;
}

static void omap_connector_destroy(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);

	DBG("%s", omap_connector->display->name);

	if (omap_connector->hpd) {
		struct omap_dss_device *hpd = omap_connector->hpd;

		hpd->ops->unregister_hpd_cb(hpd);
		omapdss_device_put(hpd);
		omap_connector->hpd = NULL;
	}

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);

	omapdss_device_put(omap_connector->output);
	omapdss_device_put(omap_connector->display);

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
	struct drm_display_mode *mode;
	struct videomode vm = {0};

	DBG("%s", omap_connector->display->name);

	/*
	 * If display exposes EDID, then we parse that in the normal way to
	 * build table of supported modes.
	 */
	dssdev = omap_connector_find_device(connector,
					    OMAP_DSS_DEVICE_OP_EDID);
	if (dssdev)
		return omap_connector_get_modes_edid(connector, dssdev);

	/*
	 * Otherwise we have either a fixed resolution panel or an output that
	 * doesn't support modes discovery (e.g. DVI or VGA with the DDC bus
	 * unconnected, or analog TV). Start by querying the size.
	 */
	dssdev = omap_connector->display;
	if (dssdev->driver && dssdev->driver->get_size)
		dssdev->driver->get_size(dssdev,
					 &connector->display_info.width_mm,
					 &connector->display_info.height_mm);

	/*
	 * Iterate over the pipeline to find the first device that can provide
	 * timing information. If we can't find any, we just let the KMS core
	 * add the default modes.
	 */
	for (dssdev = omap_connector->display; dssdev; dssdev = dssdev->src) {
		if (dssdev->ops->get_timings)
			break;
	}
	if (!dssdev)
		return 0;

	/* Add a single mode corresponding to the fixed panel timings. */
	mode = drm_mode_create(connector->dev);
	if (!mode)
		return 0;

	dssdev->ops->get_timings(dssdev, &vm);

	drm_display_mode_from_videomode(&vm, mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static int omap_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	enum omap_channel channel = omap_connector->output->dispc_channel;
	struct omap_drm_private *priv = connector->dev->dev_private;
	struct omap_dss_device *dssdev;
	struct videomode vm = {0};
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *new_mode;
	int r, ret = MODE_BAD;

	drm_display_mode_to_videomode(mode, &vm);
	mode->vrefresh = drm_mode_vrefresh(mode);

	r = priv->dispc_ops->mgr_check_timings(priv->dispc, channel, &vm);
	if (r)
		goto done;

	for (dssdev = omap_connector->output; dssdev; dssdev = dssdev->next) {
		if (!dssdev->ops->check_timings)
			continue;

		r = dssdev->ops->check_timings(dssdev, &vm);
		if (r)
			goto done;
	}

	/* check if vrefresh is still valid */
	new_mode = drm_mode_duplicate(dev, mode);
	if (!new_mode)
		return MODE_BAD;

	new_mode->clock = vm.pixelclock / 1000;
	new_mode->vrefresh = 0;
	if (mode->vrefresh == drm_mode_vrefresh(new_mode))
		ret = MODE_OK;
	drm_mode_destroy(dev, new_mode);

done:
	DBG("connector: mode %s: "
			"%d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			(ret == MODE_OK) ? "valid" : "invalid",
			mode->base.id, mode->name, mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal, mode->type, mode->flags);

	return ret;
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

static int omap_connector_get_type(struct omap_dss_device *display)
{
	switch (display->type) {
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
					  struct omap_dss_device *display,
					  struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;
	struct omap_connector *omap_connector;
	struct omap_dss_device *dssdev;

	DBG("%s", display->name);

	omap_connector = kzalloc(sizeof(*omap_connector), GFP_KERNEL);
	if (!omap_connector)
		goto fail;

	omap_connector->output = omapdss_device_get(output);
	omap_connector->display = omapdss_device_get(display);

	connector = &omap_connector->base;
	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	drm_connector_init(dev, connector, &omap_connector_funcs,
			   omap_connector_get_type(display));
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
