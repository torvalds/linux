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

int arcpgu_drm_hdmi_init(struct drm_device *drm, struct device_node *np)
{
	struct arcpgu_drm_private *arcpgu = dev_to_arcpgu(drm);
	struct drm_bridge *bridge;

	/* Locate drm bridge from the hdmi encoder DT node */
	bridge = of_drm_find_bridge(np);
	if (!bridge)
		return -EPROBE_DEFER;

	/* Link drm_bridge to encoder */
	return drm_simple_display_pipe_attach_bridge(&arcpgu->pipe, bridge);
}
