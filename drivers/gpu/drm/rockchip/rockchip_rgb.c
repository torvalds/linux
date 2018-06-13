/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Sandy Huang <hjc@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define HIWORD_UPDATE(v, l, h)	(((v) << (l)) | (GENMASK(h, l) << 16))

#define PX30_GRF_PD_VO_CON1		0x0438
#define PX30_RGB_DATA_SYNC_BYPASS(v)	HIWORD_UPDATE(v, 3, 3)
#define PX30_RGB_VOP_SEL(v)		HIWORD_UPDATE(v, 2, 2)

#define RK1808_GRF_PD_VO_CON1		0x0444
#define RK1808_RGB_DATA_SYNC_BYPASS(v)	HIWORD_UPDATE(v, 3, 3)

#define RK3288_GRF_SOC_CON6		0x025c
#define RK3288_LVDS_LCDC_SEL(x)		HIWORD_UPDATE(x,  3,  3)
#define RK3288_GRF_SOC_CON7		0x0260
#define RK3288_LVDS_PWRDWN(x)		HIWORD_UPDATE(x, 15, 15)
#define RK3288_LVDS_CON_ENABLE_2(x)	HIWORD_UPDATE(x, 12, 12)
#define RK3288_LVDS_CON_ENABLE_1(x)	HIWORD_UPDATE(x, 11, 11)
#define RK3288_LVDS_CON_CLKINV(x)	HIWORD_UPDATE(x,  8,  8)
#define RK3288_LVDS_CON_TTL_EN(x)	HIWORD_UPDATE(x,  6,  6)

struct rockchip_rgb;

struct rockchip_rgb_funcs {
	void (*enable)(struct rockchip_rgb *rgb);
	void (*disable)(struct rockchip_rgb *rgb);
};

struct rockchip_rgb {
	struct device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct phy *phy;
	struct regmap *grf;
	bool data_sync;
	const struct rockchip_rgb_funcs *funcs;
};

static inline struct rockchip_rgb *connector_to_rgb(struct drm_connector *c)
{
	return container_of(c, struct rockchip_rgb, connector);
}

static inline struct rockchip_rgb *encoder_to_rgb(struct drm_encoder *e)
{
	return container_of(e, struct rockchip_rgb, encoder);
}

static enum drm_connector_status
rockchip_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs rockchip_rgb_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rockchip_rgb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int rockchip_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_rgb *rgb = connector_to_rgb(connector);
	struct drm_panel *panel = rgb->panel;

	return drm_panel_get_modes(panel);
}

static struct drm_encoder *
rockchip_rgb_connector_best_encoder(struct drm_connector *connector)
{
	struct rockchip_rgb *rgb = connector_to_rgb(connector);

	return &rgb->encoder;
}

static const
struct drm_connector_helper_funcs rockchip_rgb_connector_helper_funcs = {
	.get_modes = rockchip_rgb_connector_get_modes,
	.best_encoder = rockchip_rgb_connector_best_encoder,
};

static void rockchip_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);
	int ret;

	pinctrl_pm_select_default_state(rgb->dev);

	if (rgb->funcs && rgb->funcs->enable)
		rgb->funcs->enable(rgb);

	if (rgb->phy) {
		ret = phy_set_mode(rgb->phy, PHY_MODE_VIDEO_TTL);
		if (ret) {
			dev_err(rgb->dev, "failed to set phy mode: %d\n", ret);
			return;
		}

		phy_power_on(rgb->phy);
	}

	if (rgb->panel) {
		drm_panel_prepare(rgb->panel);
		drm_panel_enable(rgb->panel);
	}
}

static void rockchip_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);

	if (rgb->panel) {
		drm_panel_disable(rgb->panel);
		drm_panel_unprepare(rgb->panel);
	}

	if (rgb->phy)
		phy_power_off(rgb->phy);

	if (rgb->funcs && rgb->funcs->disable)
		rgb->funcs->disable(rgb);

	pinctrl_pm_select_sleep_state(rgb->dev);
}

static int
rockchip_rgb_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	switch (s->bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X18:
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
		break;
	case MEDIA_BUS_FMT_RGB565_1X16:
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	default:
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		break;
	}

	s->output_type = DRM_MODE_CONNECTOR_DPI;

	return 0;
}

static const
struct drm_encoder_helper_funcs rockchip_rgb_encoder_helper_funcs = {
	.enable = rockchip_rgb_encoder_enable,
	.disable = rockchip_rgb_encoder_disable,
	.atomic_check = rockchip_rgb_encoder_atomic_check,
};

static const struct drm_encoder_funcs rockchip_rgb_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int rockchip_rgb_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct rockchip_rgb *rgb = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder = &rgb->encoder;
	struct drm_connector *connector;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &rgb->panel, &rgb->bridge);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to find panel or bridge: %d\n", ret);
		return ret;
	}

	encoder->port = dev->of_node;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_rgb_encoder_funcs,
			       DRM_MODE_ENCODER_DPI, NULL);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to initialize encoder: %d\n", ret);
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_rgb_encoder_helper_funcs);

	if (rgb->panel) {
		connector = &rgb->connector;
		ret = drm_connector_init(drm_dev, connector,
					 &rockchip_rgb_connector_funcs,
					 DRM_MODE_CONNECTOR_DPI);
		if (ret < 0) {
			DRM_DEV_ERROR(dev,
				      "failed to initialize connector: %d\n",
				      ret);
			goto err_free_encoder;
		}

		drm_connector_helper_add(connector,
					 &rockchip_rgb_connector_helper_funcs);

		ret = drm_mode_connector_attach_encoder(connector, encoder);
		if (ret < 0) {
			DRM_DEV_ERROR(dev,
				      "failed to attach encoder: %d\n", ret);
			goto err_free_connector;
		}

		ret = drm_panel_attach(rgb->panel, connector);
		if (ret < 0) {
			DRM_DEV_ERROR(dev, "failed to attach panel: %d\n", ret);
			goto err_free_connector;
		}
		connector->port = dev->of_node;
	} else {
		rgb->bridge->encoder = encoder;
		ret = drm_bridge_attach(drm_dev, rgb->bridge);
		if (ret) {
			DRM_DEV_ERROR(dev,
				      "failed to attach bridge: %d\n", ret);
			goto err_free_encoder;
		}
		encoder->bridge = rgb->bridge;
	}

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void rockchip_rgb_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_rgb *rgb = dev_get_drvdata(dev);

	if (rgb->panel) {
		drm_panel_detach(rgb->panel);
		drm_connector_cleanup(&rgb->connector);
	}

	drm_encoder_cleanup(&rgb->encoder);
}

static const struct component_ops rockchip_rgb_component_ops = {
	.bind = rockchip_rgb_bind,
	.unbind = rockchip_rgb_unbind,
};

static int rockchip_rgb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_rgb *rgb;
	int ret;

	rgb = devm_kzalloc(&pdev->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->dev = dev;
	rgb->funcs = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, rgb);

	rgb->data_sync = of_property_read_bool(dev->of_node,
					       "rockchip,data-sync");

	if (dev->parent && dev->parent->of_node) {
		rgb->grf = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(rgb->grf)) {
			ret = PTR_ERR(rgb->grf);
			dev_err(dev, "Unable to get grf: %d\n", ret);
			return ret;
		}
	}

	rgb->phy = devm_phy_optional_get(dev, "phy");
	if (IS_ERR(rgb->phy)) {
		ret = PTR_ERR(rgb->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	return component_add(dev, &rockchip_rgb_component_ops);
}

static int rockchip_rgb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_rgb_component_ops);

	return 0;
}

static void px30_rgb_enable(struct rockchip_rgb *rgb)
{
	int pipe = drm_of_encoder_active_endpoint_id(rgb->dev->of_node,
						     &rgb->encoder);

	regmap_write(rgb->grf, PX30_GRF_PD_VO_CON1, PX30_RGB_VOP_SEL(pipe) |
		     PX30_RGB_DATA_SYNC_BYPASS(!rgb->data_sync));
}

static const struct rockchip_rgb_funcs px30_rgb_funcs = {
	.enable = px30_rgb_enable,
};

static void rk1808_rgb_enable(struct rockchip_rgb *rgb)
{
	regmap_write(rgb->grf, RK1808_GRF_PD_VO_CON1,
		     RK1808_RGB_DATA_SYNC_BYPASS(!rgb->data_sync));
}

static const struct rockchip_rgb_funcs rk1808_rgb_funcs = {
	.enable = rk1808_rgb_enable,
};

static void rk3288_rgb_enable(struct rockchip_rgb *rgb)
{
	int pipe = drm_of_encoder_active_endpoint_id(rgb->dev->of_node,
						     &rgb->encoder);

	regmap_write(rgb->grf, RK3288_GRF_SOC_CON6, RK3288_LVDS_LCDC_SEL(pipe));
	regmap_write(rgb->grf, RK3288_GRF_SOC_CON7,
		     RK3288_LVDS_PWRDWN(0) | RK3288_LVDS_CON_ENABLE_2(1) |
		     RK3288_LVDS_CON_ENABLE_1(1) | RK3288_LVDS_CON_CLKINV(0) |
		     RK3288_LVDS_CON_TTL_EN(1));
}

static void rk3288_rgb_disable(struct rockchip_rgb *rgb)
{
	regmap_write(rgb->grf, RK3288_GRF_SOC_CON7,
		     RK3288_LVDS_PWRDWN(1) | RK3288_LVDS_CON_ENABLE_2(0) |
		     RK3288_LVDS_CON_ENABLE_1(0) | RK3288_LVDS_CON_TTL_EN(0));
}

static const struct rockchip_rgb_funcs rk3288_rgb_funcs = {
	.enable = rk3288_rgb_enable,
	.disable = rk3288_rgb_disable,
};

static const struct of_device_id rockchip_rgb_dt_ids[] = {
	{ .compatible = "rockchip,px30-rgb", .data = &px30_rgb_funcs },
	{ .compatible = "rockchip,rk1808-rgb", .data = &rk1808_rgb_funcs },
	{ .compatible = "rockchip,rk3066-rgb", },
	{ .compatible = "rockchip,rk3128-rgb", },
	{ .compatible = "rockchip,rk3288-rgb", .data = &rk3288_rgb_funcs },
	{ .compatible = "rockchip,rk3308-rgb", },
	{ .compatible = "rockchip,rk3368-rgb", },
	{ .compatible = "rockchip,rv1108-rgb", },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_rgb_dt_ids);

static struct platform_driver rockchip_rgb_driver = {
	.probe = rockchip_rgb_probe,
	.remove = rockchip_rgb_remove,
	.driver = {
		.name = "rockchip-rgb",
		.of_match_table = of_match_ptr(rockchip_rgb_dt_ids),
	},
};

module_platform_driver(rockchip_rgb_driver);

MODULE_AUTHOR("Sandy Huang <hjc@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP RGB Driver");
MODULE_LICENSE("GPL v2");
