// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012 Sascha Hauer, Pengutronix
 *
 * bridge driver for legacy DT bindings, utilizing display-timings node
 *
 * Author: Dmitry Baryshkov <dmitry.baryshkov@oss.qualcomm.com>
 */

#include <linux/export.h>

#include <drm/drm_bridge.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <drm/bridge/of-display-mode-bridge.h>

#include <video/of_display_timing.h>
#include <video/of_videomode.h>

struct of_display_mode_bridge {
	struct drm_bridge base;

	struct drm_display_mode mode;
	u32 bus_flags;
};

#define to_of_display_mode_bridge(bridge) container_of(bridge, struct of_display_mode_bridge, base)

static int of_display_mode_bridge_attach(struct drm_bridge *bridge,
					 struct drm_encoder *encoder,
					 enum drm_bridge_attach_flags flags)
{
	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	return 0;
}

static int of_display_mode_bridge_get_modes(struct drm_bridge *bridge,
					    struct drm_connector *connector)
{
	struct of_display_mode_bridge *of_bridge = to_of_display_mode_bridge(bridge);
	int ret;

	ret = drm_connector_helper_get_modes_fixed(connector, &of_bridge->mode);
	if (ret)
		return ret;

	connector->display_info.bus_flags = of_bridge->bus_flags;

	return 0;
}

struct drm_bridge_funcs of_display_mode_bridge_funcs = {
	.attach = of_display_mode_bridge_attach,
	.get_modes = of_display_mode_bridge_get_modes,
};

struct drm_bridge *devm_drm_of_display_mode_bridge(struct device *dev,
						   struct device_node *np,
						   int type)
{
	struct of_display_mode_bridge *of_bridge;
	int ret;

	of_bridge = devm_drm_bridge_alloc(dev, struct of_display_mode_bridge,
					  base, &of_display_mode_bridge_funcs);
	if (IS_ERR(of_bridge))
		return ERR_CAST(of_bridge);

	ret = of_get_drm_display_mode(np,
				      &of_bridge->mode,
				      &of_bridge->bus_flags,
				      OF_USE_NATIVE_MODE);
	if (ret)
		return ERR_PTR(ret);

	of_bridge->mode.type |= DRM_MODE_TYPE_DRIVER;

	of_bridge->base.of_node = np;
	of_bridge->base.ops = DRM_BRIDGE_OP_MODES;
	of_bridge->base.type = type;

	ret = devm_drm_bridge_add(dev, &of_bridge->base);
	if (ret)
		return ERR_PTR(ret);

	return &of_bridge->base;
}
EXPORT_SYMBOL_GPL(devm_drm_of_display_mode_bridge);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DRM bridge driver for legacy DT bindings");
