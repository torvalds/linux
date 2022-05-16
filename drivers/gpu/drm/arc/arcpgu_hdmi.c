// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 */

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_device.h>

#include "arcpgu.h"

static struct drm_encoder_funcs arcpgu_drm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int arcpgu_drm_hdmi_init(struct drm_device *drm, struct device_node *np)
{
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;

	int ret = 0;

	encoder = devm_kzalloc(drm->dev, sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;

	/* Locate drm bridge from the hdmi encoder DT node */
	bridge = of_drm_find_bridge(np);
	if (!bridge)
		return -EPROBE_DEFER;

	encoder->possible_crtcs = 1;
	encoder->possible_clones = 0;
	ret = drm_encoder_init(drm, encoder, &arcpgu_drm_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;

	/* Link drm_bridge to encoder */
	ret = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (ret)
		drm_encoder_cleanup(encoder);

	return ret;
}
