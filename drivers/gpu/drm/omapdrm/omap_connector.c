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
	struct omap_dss_device *dssdev;
	bool hdmi_mode;
};

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

	if (old_status != status)
		drm_kms_helper_hotplug_event(dev);
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

	for (dssdev = omap_connector->dssdev; dssdev; dssdev = dssdev->src) {
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
		if (dssdev->ops->detect(dssdev))
			status = connector_status_connected;
		else
			status = connector_status_disconnected;
	} else {
		switch (omap_connector->dssdev->type) {
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

	VERB("%s: %d (force=%d)", omap_connector->dssdev->name, status, force);

	return status;
}

static void omap_connector_destroy(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev;

	DBG("%s", omap_connector->dssdev->name);

	if (connector->polled == DRM_CONNECTOR_POLL_HPD) {
		dssdev = omap_connector_find_device(connector,
						    OMAP_DSS_DEVICE_OP_HPD);
		dssdev->ops->unregister_hpd_cb(dssdev);
	}

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(omap_connector);

	omapdss_device_put(omap_connector->dssdev);
}

#define MAX_EDID  512

static int omap_connector_get_modes(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = omap_connector->dssdev;
	struct drm_device *dev = connector->dev;
	int n = 0;

	DBG("%s", omap_connector->dssdev->name);

	/* if display exposes EDID, then we parse that in the normal way to
	 * build table of supported modes.. otherwise (ie. fixed resolution
	 * LCD panels) we just return a single mode corresponding to the
	 * currently configured timings:
	 */
	if (dssdev->ops->read_edid) {
		void *edid = kzalloc(MAX_EDID, GFP_KERNEL);

		if (!edid)
			return 0;

		if ((dssdev->ops->read_edid(dssdev, edid, MAX_EDID) > 0) &&
				drm_edid_is_valid(edid)) {
			drm_connector_update_edid_property(
					connector, edid);
			n = drm_add_edid_modes(connector, edid);

			omap_connector->hdmi_mode =
				drm_detect_hdmi_monitor(edid);
		} else {
			drm_connector_update_edid_property(
					connector, NULL);
		}

		kfree(edid);
	} else {
		struct drm_display_mode *mode = drm_mode_create(dev);
		struct videomode vm = {0};

		if (!mode)
			return 0;

		dssdev->ops->get_timings(dssdev, &vm);

		drm_display_mode_from_videomode(&vm, mode);

		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);

		if (dssdev->driver && dssdev->driver->get_size) {
			dssdev->driver->get_size(dssdev,
					 &connector->display_info.width_mm,
					 &connector->display_info.height_mm);
		}

		n = 1;
	}

	return n;
}

static int omap_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = omap_connector->dssdev;
	struct videomode vm = {0};
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *new_mode;
	int r, ret = MODE_BAD;

	drm_display_mode_to_videomode(mode, &vm);
	mode->vrefresh = drm_mode_vrefresh(mode);

	/*
	 * if the panel driver doesn't have a check_timings, it's most likely
	 * a fixed resolution panel, check if the timings match with the
	 * panel's timings
	 */
	if (dssdev->ops->check_timings) {
		r = dssdev->ops->check_timings(dssdev, &vm);
	} else {
		struct videomode t = {0};

		dssdev->ops->get_timings(dssdev, &t);

		/*
		 * Ignore the flags, as we don't get them from
		 * drm_display_mode_to_videomode.
		 */
		t.flags = 0;

		if (memcmp(&vm, &t, sizeof(vm)))
			r = -EINVAL;
		else
			r = 0;
	}

	if (!r) {
		/* check if vrefresh is still valid */
		new_mode = drm_mode_duplicate(dev, mode);

		if (!new_mode)
			return MODE_BAD;

		new_mode->clock = vm.pixelclock / 1000;
		new_mode->vrefresh = 0;
		if (mode->vrefresh == drm_mode_vrefresh(new_mode))
			ret = MODE_OK;
		drm_mode_destroy(dev, new_mode);
	}

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

/* initialize connector */
struct drm_connector *omap_connector_init(struct drm_device *dev,
		int connector_type, struct omap_dss_device *dssdev,
		struct drm_encoder *encoder)
{
	struct drm_connector *connector = NULL;
	struct omap_connector *omap_connector;

	DBG("%s", dssdev->name);

	omap_connector = kzalloc(sizeof(*omap_connector), GFP_KERNEL);
	if (!omap_connector)
		goto fail;

	omap_connector->dssdev = omapdss_device_get(dssdev);

	connector = &omap_connector->base;
	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	drm_connector_init(dev, connector, &omap_connector_funcs,
				connector_type);
	drm_connector_helper_add(connector, &omap_connector_helper_funcs);

	/*
	 * Initialize connector status handling. First try to find a device that
	 * supports hot-plug reporting. If it fails, fall back to a device that
	 * support polling. If that fails too, we don't support hot-plug
	 * detection at all.
	 */
	dssdev = omap_connector_find_device(connector, OMAP_DSS_DEVICE_OP_HPD);
	if (dssdev) {
		int ret;

		ret = dssdev->ops->register_hpd_cb(dssdev,
						   omap_connector_hpd_cb,
						   omap_connector);
		if (ret < 0)
			DBG("%s: Failed to register HPD callback (%d).",
			    dssdev->name, ret);
		else
			connector->polled = DRM_CONNECTOR_POLL_HPD;
	}

	if (!connector->polled) {
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
