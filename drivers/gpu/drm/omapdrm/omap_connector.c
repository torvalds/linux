/*
 * drivers/gpu/drm/omapdrm/omap_connector.c
 *
 * Copyright (C) 2011 Texas Instruments
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

static enum drm_connector_status omap_connector_detect(
		struct drm_connector *connector, bool force)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = omap_connector->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;
	enum drm_connector_status ret;

	if (dssdrv->detect) {
		if (dssdrv->detect(dssdev))
			ret = connector_status_connected;
		else
			ret = connector_status_disconnected;
	} else if (dssdev->type == OMAP_DISPLAY_TYPE_DPI ||
			dssdev->type == OMAP_DISPLAY_TYPE_DBI ||
			dssdev->type == OMAP_DISPLAY_TYPE_SDI ||
			dssdev->type == OMAP_DISPLAY_TYPE_DSI) {
		ret = connector_status_connected;
	} else {
		ret = connector_status_unknown;
	}

	VERB("%s: %d (force=%d)", omap_connector->dssdev->name, ret, force);

	return ret;
}

static void omap_connector_destroy(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = omap_connector->dssdev;

	DBG("%s", omap_connector->dssdev->name);
	if (connector->polled == DRM_CONNECTOR_POLL_HPD &&
	    dssdev->driver->unregister_hpd_cb) {
		dssdev->driver->unregister_hpd_cb(dssdev);
	}
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(omap_connector);

	omap_dss_put_device(dssdev);
}

#define MAX_EDID  512

static int omap_connector_get_modes(struct drm_connector *connector)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = omap_connector->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;
	struct drm_device *dev = connector->dev;
	int n = 0;

	DBG("%s", omap_connector->dssdev->name);

	/* if display exposes EDID, then we parse that in the normal way to
	 * build table of supported modes.. otherwise (ie. fixed resolution
	 * LCD panels) we just return a single mode corresponding to the
	 * currently configured timings:
	 */
	if (dssdrv->read_edid) {
		void *edid = kzalloc(MAX_EDID, GFP_KERNEL);

		if ((dssdrv->read_edid(dssdev, edid, MAX_EDID) > 0) &&
				drm_edid_is_valid(edid)) {
			drm_mode_connector_update_edid_property(
					connector, edid);
			n = drm_add_edid_modes(connector, edid);

			omap_connector->hdmi_mode =
				drm_detect_hdmi_monitor(edid);
		} else {
			drm_mode_connector_update_edid_property(
					connector, NULL);
		}

		kfree(edid);
	} else {
		struct drm_display_mode *mode = drm_mode_create(dev);
		struct videomode vm = {0};

		dssdrv->get_timings(dssdev, &vm);

		drm_display_mode_from_videomode(&vm, mode);

		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);

		n = 1;
	}

	return n;
}

static int omap_connector_mode_valid(struct drm_connector *connector,
				 struct drm_display_mode *mode)
{
	struct omap_connector *omap_connector = to_omap_connector(connector);
	struct omap_dss_device *dssdev = omap_connector->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;
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
	if (dssdrv->check_timings) {
		r = dssdrv->check_timings(dssdev, &vm);
	} else {
		struct videomode t = {0};

		dssdrv->get_timings(dssdev, &t);

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
	bool hpd_supported = false;

	DBG("%s", dssdev->name);

	omap_dss_get_device(dssdev);

	omap_connector = kzalloc(sizeof(*omap_connector), GFP_KERNEL);
	if (!omap_connector)
		goto fail;

	omap_connector->dssdev = dssdev;

	connector = &omap_connector->base;

	drm_connector_init(dev, connector, &omap_connector_funcs,
				connector_type);
	drm_connector_helper_add(connector, &omap_connector_helper_funcs);

	if (dssdev->driver->register_hpd_cb) {
		int ret = dssdev->driver->register_hpd_cb(dssdev,
							  omap_connector_hpd_cb,
							  omap_connector);
		if (!ret)
			hpd_supported = true;
		else if (ret != -ENOTSUPP)
			DBG("%s: Failed to register HPD callback (%d).",
			    dssdev->name, ret);
	}

	if (hpd_supported)
		connector->polled = DRM_CONNECTOR_POLL_HPD;
	else if (dssdev->driver->detect)
		connector->polled = DRM_CONNECTOR_POLL_CONNECT |
				    DRM_CONNECTOR_POLL_DISCONNECT;
	else
		connector->polled = 0;

	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	return connector;

fail:
	if (connector)
		omap_connector_destroy(connector);

	return NULL;
}
