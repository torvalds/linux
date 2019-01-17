/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "edp.h"

struct edp_bridge {
	struct drm_bridge base;
	struct msm_edp *edp;
};
#define to_edp_bridge(x) container_of(x, struct edp_bridge, base)

void edp_bridge_destroy(struct drm_bridge *bridge)
{
}

static void edp_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct edp_bridge *edp_bridge = to_edp_bridge(bridge);
	struct msm_edp *edp = edp_bridge->edp;

	DBG("");
	msm_edp_ctrl_power(edp->ctrl, true);
}

static void edp_bridge_enable(struct drm_bridge *bridge)
{
	DBG("");
}

static void edp_bridge_disable(struct drm_bridge *bridge)
{
	DBG("");
}

static void edp_bridge_post_disable(struct drm_bridge *bridge)
{
	struct edp_bridge *edp_bridge = to_edp_bridge(bridge);
	struct msm_edp *edp = edp_bridge->edp;

	DBG("");
	msm_edp_ctrl_power(edp->ctrl, false);
}

static void edp_bridge_mode_set(struct drm_bridge *bridge,
		const struct drm_display_mode *mode,
		const struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = bridge->dev;
	struct drm_connector *connector;
	struct edp_bridge *edp_bridge = to_edp_bridge(bridge);
	struct msm_edp *edp = edp_bridge->edp;

	DBG("set mode: " DRM_MODE_FMT, DRM_MODE_ARG(mode));

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if ((connector->encoder != NULL) &&
			(connector->encoder->bridge == bridge)) {
			msm_edp_ctrl_timing_cfg(edp->ctrl,
				adjusted_mode, &connector->display_info);
			break;
		}
	}
}

static const struct drm_bridge_funcs edp_bridge_funcs = {
	.pre_enable = edp_bridge_pre_enable,
	.enable = edp_bridge_enable,
	.disable = edp_bridge_disable,
	.post_disable = edp_bridge_post_disable,
	.mode_set = edp_bridge_mode_set,
};

/* initialize bridge */
struct drm_bridge *msm_edp_bridge_init(struct msm_edp *edp)
{
	struct drm_bridge *bridge = NULL;
	struct edp_bridge *edp_bridge;
	int ret;

	edp_bridge = devm_kzalloc(edp->dev->dev,
			sizeof(*edp_bridge), GFP_KERNEL);
	if (!edp_bridge) {
		ret = -ENOMEM;
		goto fail;
	}

	edp_bridge->edp = edp;

	bridge = &edp_bridge->base;
	bridge->funcs = &edp_bridge_funcs;

	ret = drm_bridge_attach(edp->encoder, bridge, NULL);
	if (ret)
		goto fail;

	return bridge;

fail:
	if (bridge)
		edp_bridge_destroy(bridge);

	return ERR_PTR(ret);
}
