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

#include <linux/list.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include "omap_drv.h"

/*
 * encoder funcs
 */

#define to_omap_encoder(x) container_of(x, struct omap_encoder, base)

/* The encoder and connector both map to same dssdev.. the encoder
 * handles the 'active' parts, ie. anything the modifies the state
 * of the hw, and the connector handles the 'read-only' parts, like
 * detecting connection and reading edid.
 */
struct omap_encoder {
	struct drm_encoder base;
	struct omap_dss_device *output;
	struct omap_dss_device *display;
};

static void omap_encoder_destroy(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(omap_encoder);
}

static const struct drm_encoder_funcs omap_encoder_funcs = {
	.destroy = omap_encoder_destroy,
};

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_connector *connector;
	struct omap_dss_device *dssdev;
	struct videomode vm = { 0 };
	bool hdmi_mode;
	int r;

	drm_display_mode_to_videomode(adjusted_mode, &vm);

	/*
	 * HACK: This fixes the vm flags.
	 * struct drm_display_mode does not contain the VSYNC/HSYNC/DE flags and
	 * they get lost when converting back and forth between struct
	 * drm_display_mode and struct videomode. The hack below goes and
	 * fetches the missing flags.
	 *
	 * A better solution is to use DRM's bus-flags through the whole driver.
	 */
	for (dssdev = omap_encoder->output; dssdev; dssdev = dssdev->next) {
		unsigned long bus_flags = dssdev->bus_flags;

		if (!(vm.flags & (DISPLAY_FLAGS_DE_LOW |
				  DISPLAY_FLAGS_DE_HIGH))) {
			if (bus_flags & DRM_BUS_FLAG_DE_LOW)
				vm.flags |= DISPLAY_FLAGS_DE_LOW;
			else if (bus_flags & DRM_BUS_FLAG_DE_HIGH)
				vm.flags |= DISPLAY_FLAGS_DE_HIGH;
		}

		if (!(vm.flags & (DISPLAY_FLAGS_PIXDATA_POSEDGE |
				  DISPLAY_FLAGS_PIXDATA_NEGEDGE))) {
			if (bus_flags & DRM_BUS_FLAG_PIXDATA_POSEDGE)
				vm.flags |= DISPLAY_FLAGS_PIXDATA_POSEDGE;
			else if (bus_flags & DRM_BUS_FLAG_PIXDATA_NEGEDGE)
				vm.flags |= DISPLAY_FLAGS_PIXDATA_NEGEDGE;
		}

		if (!(vm.flags & (DISPLAY_FLAGS_SYNC_POSEDGE |
				  DISPLAY_FLAGS_SYNC_NEGEDGE))) {
			if (bus_flags & DRM_BUS_FLAG_SYNC_POSEDGE)
				vm.flags |= DISPLAY_FLAGS_SYNC_POSEDGE;
			else if (bus_flags & DRM_BUS_FLAG_SYNC_NEGEDGE)
				vm.flags |= DISPLAY_FLAGS_SYNC_NEGEDGE;
		}
	}

	/* Set timings for all devices in the display pipeline. */
	dss_mgr_set_timings(omap_encoder->output, &vm);

	for (dssdev = omap_encoder->output; dssdev; dssdev = dssdev->next) {
		if (dssdev->ops->set_timings)
			dssdev->ops->set_timings(dssdev, &vm);
	}

	/* Set the HDMI mode and HDMI infoframe if applicable. */
	hdmi_mode = false;
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			hdmi_mode = omap_connector_get_hdmi_mode(connector);
			break;
		}
	}

	dssdev = omap_encoder->output;

	if (dssdev->ops->hdmi.set_hdmi_mode)
		dssdev->ops->hdmi.set_hdmi_mode(dssdev, hdmi_mode);

	if (hdmi_mode && dssdev->ops->hdmi.set_infoframe) {
		struct hdmi_avi_infoframe avi;

		r = drm_hdmi_avi_infoframe_from_display_mode(&avi, adjusted_mode,
							     false);
		if (r == 0)
			dssdev->ops->hdmi.set_infoframe(dssdev, &avi);
	}
}

static void omap_encoder_disable(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->display;

	dssdev->ops->disable(dssdev);
}

static void omap_encoder_enable(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->display;
	int r;

	r = dssdev->ops->enable(dssdev);
	if (r)
		dev_err(encoder->dev->dev,
			"Failed to enable display '%s': %d\n",
			dssdev->name, r);
}

static int omap_encoder_atomic_check(struct drm_encoder *encoder,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	enum omap_channel channel = omap_encoder->output->dispc_channel;
	struct drm_device *dev = encoder->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_dss_device *dssdev;
	struct videomode vm = { 0 };
	int ret;

	drm_display_mode_to_videomode(&crtc_state->mode, &vm);

	ret = priv->dispc_ops->mgr_check_timings(priv->dispc, channel, &vm);
	if (ret)
		goto done;

	for (dssdev = omap_encoder->output; dssdev; dssdev = dssdev->next) {
		if (!dssdev->ops->check_timings)
			continue;

		ret = dssdev->ops->check_timings(dssdev, &vm);
		if (ret)
			goto done;
	}

	drm_display_mode_from_videomode(&vm, &crtc_state->adjusted_mode);

done:
	if (ret)
		dev_err(dev->dev, "invalid timings: %d\n", ret);

	return ret;
}

static const struct drm_encoder_helper_funcs omap_encoder_helper_funcs = {
	.mode_set = omap_encoder_mode_set,
	.disable = omap_encoder_disable,
	.enable = omap_encoder_enable,
	.atomic_check = omap_encoder_atomic_check,
};

/* initialize encoder */
struct drm_encoder *omap_encoder_init(struct drm_device *dev,
				      struct omap_dss_device *output,
				      struct omap_dss_device *display)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder)
		goto fail;

	omap_encoder->output = output;
	omap_encoder->display = display;

	encoder = &omap_encoder->base;

	drm_encoder_init(dev, encoder, &omap_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(encoder, &omap_encoder_helper_funcs);

	return encoder;

fail:
	if (encoder)
		omap_encoder_destroy(encoder);

	return NULL;
}
