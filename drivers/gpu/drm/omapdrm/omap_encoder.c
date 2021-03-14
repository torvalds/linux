// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 */

#include <linux/list.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
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

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *output = omap_encoder->output;
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
	for (bridge = output->bridge; bridge;
	     bridge = drm_bridge_get_next_bridge(bridge)) {
		if (!bridge->timings)
			continue;

		bus_flags = bridge->timings->input_bus_flags;
		omap_encoder_update_videomode_flags(&vm, bus_flags);
	}

	bus_flags = connector->display_info.bus_flags;
	omap_encoder_update_videomode_flags(&vm, bus_flags);

	/* Set timings for all devices in the display pipeline. */
	dss_mgr_set_timings(output, &vm);
}

static const struct drm_encoder_helper_funcs omap_encoder_helper_funcs = {
	.mode_set = omap_encoder_mode_set,
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
