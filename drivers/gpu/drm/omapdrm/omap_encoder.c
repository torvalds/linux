// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 */

#include <linux/list.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_edid.h>
#include <drm/drm_panel.h>

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

static void omap_encoder_update_videomode_flags(struct videomode *vm,
						u32 bus_flags)
{
	if (!(vm->flags & (DISPLAY_FLAGS_DE_LOW |
			   DISPLAY_FLAGS_DE_HIGH))) {
		if (bus_flags & DRM_BUS_FLAG_DE_LOW)
			vm->flags |= DISPLAY_FLAGS_DE_LOW;
		else if (bus_flags & DRM_BUS_FLAG_DE_HIGH)
			vm->flags |= DISPLAY_FLAGS_DE_HIGH;
	}

	if (!(vm->flags & (DISPLAY_FLAGS_PIXDATA_POSEDGE |
			   DISPLAY_FLAGS_PIXDATA_NEGEDGE))) {
		if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE)
			vm->flags |= DISPLAY_FLAGS_PIXDATA_POSEDGE;
		else if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
			vm->flags |= DISPLAY_FLAGS_PIXDATA_NEGEDGE;
	}

	if (!(vm->flags & (DISPLAY_FLAGS_SYNC_POSEDGE |
			   DISPLAY_FLAGS_SYNC_NEGEDGE))) {
		if (bus_flags & DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE)
			vm->flags |= DISPLAY_FLAGS_SYNC_POSEDGE;
		else if (bus_flags & DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE)
			vm->flags |= DISPLAY_FLAGS_SYNC_NEGEDGE;
	}
}

static void omap_encoder_hdmi_mode_set(struct drm_connector *connector,
				       struct drm_encoder *encoder,
				       struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->output;
	bool hdmi_mode;

	hdmi_mode = omap_connector_get_hdmi_mode(connector);

	if (dssdev->ops->hdmi.set_hdmi_mode)
		dssdev->ops->hdmi.set_hdmi_mode(dssdev, hdmi_mode);

	if (hdmi_mode && dssdev->ops->hdmi.set_infoframe) {
		struct hdmi_avi_infoframe avi;
		int r;

		r = drm_hdmi_avi_infoframe_from_display_mode(&avi, connector,
							     adjusted_mode);
		if (r == 0)
			dssdev->ops->hdmi.set_infoframe(dssdev, &avi);
	}
}

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *output = omap_encoder->output;
	struct omap_dss_device *dssdev;
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct drm_bridge *bridge;
	struct videomode vm = { 0 };
	u32 bus_flags;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder)
			break;
	}

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
	for (dssdev = output; dssdev; dssdev = dssdev->next)
		omap_encoder_update_videomode_flags(&vm, dssdev->bus_flags);

	for (bridge = output->bridge; bridge; bridge = bridge->next) {
		if (!bridge->timings)
			continue;

		bus_flags = bridge->timings->input_bus_flags;
		omap_encoder_update_videomode_flags(&vm, bus_flags);
	}

	bus_flags = connector->display_info.bus_flags;
	omap_encoder_update_videomode_flags(&vm, bus_flags);

	/* Set timings for all devices in the display pipeline. */
	dss_mgr_set_timings(output, &vm);

	for (dssdev = output; dssdev; dssdev = dssdev->next) {
		if (dssdev->ops->set_timings)
			dssdev->ops->set_timings(dssdev, adjusted_mode);
	}

	/* Set the HDMI mode and HDMI infoframe if applicable. */
	if (output->type == OMAP_DISPLAY_TYPE_HDMI)
		omap_encoder_hdmi_mode_set(connector, encoder, adjusted_mode);
}

static void omap_encoder_disable(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->output;
	struct drm_device *dev = encoder->dev;

	dev_dbg(dev->dev, "disable(%s)\n", dssdev->name);

	/* Disable the panel if present. */
	if (dssdev->panel) {
		drm_panel_disable(dssdev->panel);
		drm_panel_unprepare(dssdev->panel);
	}

	/*
	 * Disable the chain of external devices, starting at the one at the
	 * internal encoder's output.
	 */
	omapdss_device_disable(dssdev->next);

	/*
	 * Disable the internal encoder. This will disable the DSS output. The
	 * DSI is treated as an exception as DSI pipelines still use the legacy
	 * flow where the pipeline output controls the encoder.
	 */
	if (dssdev->type != OMAP_DISPLAY_TYPE_DSI) {
		dssdev->ops->disable(dssdev);
		dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
	}

	/*
	 * Perform the post-disable operations on the chain of external devices
	 * to complete the display pipeline disable.
	 */
	omapdss_device_post_disable(dssdev->next);
}

static void omap_encoder_enable(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->output;
	struct drm_device *dev = encoder->dev;

	dev_dbg(dev->dev, "enable(%s)\n", dssdev->name);

	/* Prepare the chain of external devices for pipeline enable. */
	omapdss_device_pre_enable(dssdev->next);

	/*
	 * Enable the internal encoder. This will enable the DSS output. The
	 * DSI is treated as an exception as DSI pipelines still use the legacy
	 * flow where the pipeline output controls the encoder.
	 */
	if (dssdev->type != OMAP_DISPLAY_TYPE_DSI) {
		dssdev->ops->enable(dssdev);
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
	}

	/*
	 * Enable the chain of external devices, starting at the one at the
	 * internal encoder's output.
	 */
	omapdss_device_enable(dssdev->next);

	/* Enable the panel if present. */
	if (dssdev->panel) {
		drm_panel_prepare(dssdev->panel);
		drm_panel_enable(dssdev->panel);
	}
}

static int omap_encoder_atomic_check(struct drm_encoder *encoder,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	enum drm_mode_status status;

	status = omap_connector_mode_fixup(omap_encoder->output,
					   &crtc_state->mode,
					   &crtc_state->adjusted_mode);
	if (status != MODE_OK) {
		dev_err(encoder->dev->dev, "invalid timings: %d\n", status);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_encoder_helper_funcs omap_encoder_helper_funcs = {
	.mode_set = omap_encoder_mode_set,
	.disable = omap_encoder_disable,
	.enable = omap_encoder_enable,
	.atomic_check = omap_encoder_atomic_check,
};

/* initialize encoder */
struct drm_encoder *omap_encoder_init(struct drm_device *dev,
				      struct omap_dss_device *output)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder)
		goto fail;

	omap_encoder->output = output;

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
