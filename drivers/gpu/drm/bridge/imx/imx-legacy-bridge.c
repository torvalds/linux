// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale i.MX drm driver
 *
 * bridge driver for legacy DT bindings, utilizing display-timings node
 */

#include <linux/export.h>

#include <drm/drm_bridge.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <drm/bridge/imx.h>

#include <video/of_display_timing.h>
#include <video/of_videomode.h>

struct imx_legacy_bridge {
	struct drm_bridge base;

	struct drm_display_mode mode;
	u32 bus_flags;
};

#define to_imx_legacy_bridge(bridge)	container_of(bridge, struct imx_legacy_bridge, base)

static int imx_legacy_bridge_attach(struct drm_bridge *bridge,
				    struct drm_encoder *encoder,
				    enum drm_bridge_attach_flags flags)
{
	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	return 0;
}

static int imx_legacy_bridge_get_modes(struct drm_bridge *bridge,
				       struct drm_connector *connector)
{
	struct imx_legacy_bridge *imx_bridge = to_imx_legacy_bridge(bridge);
	int ret;

	ret = drm_connector_helper_get_modes_fixed(connector, &imx_bridge->mode);
	if (ret)
		return ret;

	connector->display_info.bus_flags = imx_bridge->bus_flags;

	return 0;
}

struct drm_bridge_funcs imx_legacy_bridge_funcs = {
	.attach = imx_legacy_bridge_attach,
	.get_modes = imx_legacy_bridge_get_modes,
};

struct drm_bridge *devm_imx_drm_legacy_bridge(struct device *dev,
					      struct device_node *np,
					      int type)
{
	struct imx_legacy_bridge *imx_bridge;
	int ret;

	imx_bridge = devm_drm_bridge_alloc(dev, struct imx_legacy_bridge,
					   base, &imx_legacy_bridge_funcs);
	if (IS_ERR(imx_bridge))
		return ERR_CAST(imx_bridge);

	ret = of_get_drm_display_mode(np,
				      &imx_bridge->mode,
				      &imx_bridge->bus_flags,
				      OF_USE_NATIVE_MODE);
	if (ret)
		return ERR_PTR(ret);

	imx_bridge->mode.type |= DRM_MODE_TYPE_DRIVER;

	imx_bridge->base.of_node = np;
	imx_bridge->base.ops = DRM_BRIDGE_OP_MODES;
	imx_bridge->base.type = type;

	ret = devm_drm_bridge_add(dev, &imx_bridge->base);
	if (ret)
		return ERR_PTR(ret);

	return &imx_bridge->base;
}
EXPORT_SYMBOL_GPL(devm_imx_drm_legacy_bridge);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Freescale i.MX DRM bridge driver for legacy DT bindings");
