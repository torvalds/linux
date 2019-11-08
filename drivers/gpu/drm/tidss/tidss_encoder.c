// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/export.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include "tidss_crtc.h"
#include "tidss_drv.h"
#include "tidss_encoder.h"

static int tidss_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct drm_device *ddev = encoder->dev;
	struct tidss_crtc_state *tcrtc_state = to_tidss_crtc_state(crtc_state);
	struct drm_display_info *di = &conn_state->connector->display_info;
	struct drm_bridge *bridge;
	bool bus_flags_set = false;

	dev_dbg(ddev->dev, "%s\n", __func__);

	/*
	 * Take the bus_flags from the first bridge that defines
	 * bridge timings, or from the connector's display_info if no
	 * bridge defines the timings.
	 */
	list_for_each_entry(bridge, &encoder->bridge_chain, chain_node) {
		if (!bridge->timings)
			continue;

		tcrtc_state->bus_flags = bridge->timings->input_bus_flags;
		bus_flags_set = true;
		break;
	}

	if (!di->bus_formats || di->num_bus_formats == 0)  {
		dev_err(ddev->dev, "%s: No bus_formats in connected display\n",
			__func__);
		return -EINVAL;
	}

	// XXX any cleaner way to set bus format and flags?
	tcrtc_state->bus_format = di->bus_formats[0];
	if (!bus_flags_set)
		tcrtc_state->bus_flags = di->bus_flags;

	return 0;
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.atomic_check = tidss_encoder_atomic_check,
};

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

struct drm_encoder *tidss_encoder_create(struct tidss_device *tidss,
					 u32 encoder_type, u32 possible_crtcs)
{
	struct drm_encoder *enc;
	int ret;

	enc = devm_kzalloc(tidss->dev, sizeof(*enc), GFP_KERNEL);
	if (!enc)
		return ERR_PTR(-ENOMEM);

	enc->possible_crtcs = possible_crtcs;

	ret = drm_encoder_init(&tidss->ddev, enc, &encoder_funcs,
			       encoder_type, NULL);
	if (ret < 0)
		return ERR_PTR(ret);

	drm_encoder_helper_add(enc, &encoder_helper_funcs);

	dev_dbg(tidss->dev, "Encoder create done\n");

	return enc;
}
