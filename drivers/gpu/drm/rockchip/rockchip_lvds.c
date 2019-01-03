/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Mark Yao <mark.yao@rock-chips.com>
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
#include <drm/drm_panel.h>
#include <drm/drm_of.h>

#include <linux/component.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define HIWORD_UPDATE(v, h, l)  (((v) << (l)) | (GENMASK(h, l) << 16))

#define PX30_GRF_PD_VO_CON1		0x0438
#define PX30_LVDS_SELECT(x)		HIWORD_UPDATE(x, 14, 13)
#define PX30_LVDS_MODE_EN(x)		HIWORD_UPDATE(x, 12, 12)
#define PX30_LVDS_MSBSEL(x)		HIWORD_UPDATE(x, 11, 11)
#define PX30_LVDS_P2S_EN(x)		HIWORD_UPDATE(x, 10, 10)
#define PX30_LVDS_VOP_SEL(x)		HIWORD_UPDATE(x,  1,  1)

#define RK3126_GRF_LVDS_CON0		0x0150
#define RK3126_LVDS_P2S_EN(x)		HIWORD_UPDATE(x,  9,  9)
#define RK3126_LVDS_MODE_EN(x)		HIWORD_UPDATE(x,  6,  6)
#define RK3126_LVDS_MSBSEL(x)		HIWORD_UPDATE(x,  3,  3)
#define RK3126_LVDS_SELECT(x)		HIWORD_UPDATE(x,  2,  1)

#define RK3288_GRF_SOC_CON6		0x025c
#define RK3288_LVDS_LCDC_SEL(x)		HIWORD_UPDATE(x,  3,  3)
#define RK3288_GRF_SOC_CON7		0x0260
#define RK3288_LVDS_PWRDWN(x)		HIWORD_UPDATE(x, 15, 15)
#define RK3288_LVDS_CON_ENABLE_2(x)	HIWORD_UPDATE(x, 12, 12)
#define RK3288_LVDS_CON_ENABLE_1(x)	HIWORD_UPDATE(x, 11, 11)
#define RK3288_LVDS_CON_DEN_POL(x)	HIWORD_UPDATE(x, 10, 10)
#define RK3288_LVDS_CON_HS_POL(x)	HIWORD_UPDATE(x,  9,  9)
#define RK3288_LVDS_CON_CLKINV(x)	HIWORD_UPDATE(x,  8,  8)
#define RK3288_LVDS_CON_STARTPHASE(x)	HIWORD_UPDATE(x,  7,  7)
#define RK3288_LVDS_CON_TTL_EN(x)	HIWORD_UPDATE(x,  6,  6)
#define RK3288_LVDS_CON_STARTSEL(x)	HIWORD_UPDATE(x,  5,  5)
#define RK3288_LVDS_CON_CHASEL(x)	HIWORD_UPDATE(x,  4,  4)
#define RK3288_LVDS_CON_MSBSEL(x)	HIWORD_UPDATE(x,  3,  3)
#define RK3288_LVDS_CON_SELECT(x)	HIWORD_UPDATE(x,  2,  0)

#define RK3368_GRF_SOC_CON7		0x041c
#define RK3368_LVDS_SELECT(x)		HIWORD_UPDATE(x, 14, 13)
#define RK3368_LVDS_MODE_EN(x)		HIWORD_UPDATE(x, 12, 12)
#define RK3368_LVDS_MSBSEL(x)		HIWORD_UPDATE(x, 11, 11)
#define RK3368_LVDS_P2S_EN(x)		HIWORD_UPDATE(x,  6,  6)

enum lvds_format {
	LVDS_8BIT_MODE_FORMAT_1,
	LVDS_8BIT_MODE_FORMAT_2,
	LVDS_8BIT_MODE_FORMAT_3,
	LVDS_6BIT_MODE,
};

struct rockchip_lvds;

struct rockchip_lvds_funcs {
	void (*enable)(struct rockchip_lvds *lvds);
	void (*disable)(struct rockchip_lvds *lvds);
};

struct rockchip_lvds {
	struct device *dev;
	struct phy *phy;
	struct regmap *grf;
	const struct rockchip_lvds_funcs *funcs;
	enum lvds_format format;
	bool data_swap;
	bool dual_channel;

	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_display_mode mode;
};

static inline struct rockchip_lvds *connector_to_lvds(struct drm_connector *c)
{
	return container_of(c, struct rockchip_lvds, connector);
}

static inline struct rockchip_lvds *encoder_to_lvds(struct drm_encoder *e)
{
	return container_of(e, struct rockchip_lvds, encoder);
}

static enum drm_connector_status
rockchip_lvds_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs rockchip_lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rockchip_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int rockchip_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);

	return drm_panel_get_modes(lvds->panel);
}

static struct drm_encoder *
rockchip_lvds_connector_best_encoder(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);

	return &lvds->encoder;
}

static
int rockchip_lvds_connector_loader_protect(struct drm_connector *connector,
					   bool on)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);

	return drm_panel_loader_protect(lvds->panel, on);
}

static const
struct drm_connector_helper_funcs rockchip_lvds_connector_helper_funcs = {
	.get_modes = rockchip_lvds_connector_get_modes,
	.best_encoder = rockchip_lvds_connector_best_encoder,
	.loader_protect = rockchip_lvds_connector_loader_protect,
};

static void rockchip_lvds_encoder_mode_set(struct drm_encoder *encoder,
					  struct drm_display_mode *mode,
					  struct drm_display_mode *adjusted)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	struct drm_connector *connector = &lvds->connector;
	struct drm_display_info *info = &connector->display_info;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	if (info->num_bus_formats)
		bus_format = info->bus_formats[0];

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:	/* jeida-18 */
		lvds->format = LVDS_6BIT_MODE;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:	/* jeida-24 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_2;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:	/* vesa-24 */
	default:
		lvds->format = LVDS_8BIT_MODE_FORMAT_1;
		break;
	}

	drm_mode_copy(&lvds->mode, adjusted);
}

static int
rockchip_lvds_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->output_type = DRM_MODE_CONNECTOR_LVDS;
	s->tv_state = &conn_state->tv;
	s->eotf = TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	return 0;
}

static void rockchip_lvds_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	int ret;

	if (lvds->panel)
		drm_panel_prepare(lvds->panel);

	if (lvds->funcs->enable)
		lvds->funcs->enable(lvds);

	ret = phy_set_mode(lvds->phy, PHY_MODE_VIDEO_LVDS);
	if (ret) {
		dev_err(lvds->dev, "failed to set phy mode: %d\n", ret);
		return;
	}

	phy_power_on(lvds->phy);

	if (lvds->panel)
		drm_panel_enable(lvds->panel);
}

static void rockchip_lvds_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->panel)
		drm_panel_disable(lvds->panel);

	phy_power_off(lvds->phy);

	if (lvds->funcs->disable)
		lvds->funcs->disable(lvds);

	if (lvds->panel)
		drm_panel_unprepare(lvds->panel);
}

static const
struct drm_encoder_helper_funcs rockchip_lvds_encoder_helper_funcs = {
	.mode_set = rockchip_lvds_encoder_mode_set,
	.enable = rockchip_lvds_encoder_enable,
	.disable = rockchip_lvds_encoder_disable,
	.atomic_check = rockchip_lvds_encoder_atomic_check,
};

static const struct drm_encoder_funcs rockchip_lvds_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int rockchip_lvds_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder = &lvds->encoder;
	struct drm_connector *connector = &lvds->connector;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &lvds->panel, &lvds->bridge);
	if (ret)
		return ret;

	encoder->port = dev->of_node;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);
	ret = drm_encoder_init(drm_dev, encoder, &rockchip_lvds_encoder_funcs,
			       DRM_MODE_ENCODER_LVDS, NULL);
	if (ret < 0) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_lvds_encoder_helper_funcs);

	if (lvds->panel) {
		connector->port = dev->of_node;

		ret = drm_connector_init(drm_dev, connector,
					 &rockchip_lvds_connector_funcs,
					 DRM_MODE_CONNECTOR_LVDS);
		if (ret < 0) {
			DRM_ERROR("failed to initialize connector with drm\n");
			goto err_free_encoder;
		}

		drm_connector_helper_add(connector,
					 &rockchip_lvds_connector_helper_funcs);
		drm_mode_connector_attach_encoder(connector, encoder);

		ret = drm_panel_attach(lvds->panel, connector);
		if (ret < 0) {
			DRM_ERROR("failed to attach panel: %d\n", ret);
			goto err_free_connector;
		}
	} else {
		lvds->bridge->encoder = encoder;
		ret = drm_bridge_attach(drm_dev, lvds->bridge);
		if (ret) {
			DRM_ERROR("Failed to attach bridge: %d\n", ret);
			goto err_free_encoder;
		}
		encoder->bridge = lvds->bridge;
	}

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
	return ret;
}

static void rockchip_lvds_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);

	if (lvds->panel) {
		drm_panel_detach(lvds->panel);
		drm_connector_cleanup(&lvds->connector);
	}

	drm_encoder_cleanup(&lvds->encoder);
}

static const struct component_ops rockchip_lvds_component_ops = {
	.bind = rockchip_lvds_bind,
	.unbind = rockchip_lvds_unbind,
};

static int rockchip_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_lvds *lvds;
	int ret;

	lvds = devm_kzalloc(dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;
	lvds->funcs = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, lvds);

	lvds->dual_channel = of_property_read_bool(dev->of_node,
						   "dual-channel");
	lvds->data_swap = of_property_read_bool(dev->of_node,
						"rockchip,data-swap");

	lvds->phy = devm_phy_get(dev, "phy");
	if (IS_ERR(lvds->phy)) {
		ret = PTR_ERR(lvds->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	lvds->grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(lvds->grf)) {
		ret = PTR_ERR(lvds->grf);
		dev_err(dev, "Unable to get grf: %d\n", ret);
		return ret;
	}

	return component_add(dev, &rockchip_lvds_component_ops);
}

static int rockchip_lvds_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_lvds_component_ops);

	return 0;
}

static void px30_lvds_enable(struct rockchip_lvds *lvds)
{
	int pipe = drm_of_encoder_active_endpoint_id(lvds->dev->of_node,
						     &lvds->encoder);

	regmap_write(lvds->grf, PX30_GRF_PD_VO_CON1,
		     PX30_LVDS_SELECT(lvds->format) |
		     PX30_LVDS_MODE_EN(1) | PX30_LVDS_MSBSEL(1) |
		     PX30_LVDS_P2S_EN(1) | PX30_LVDS_VOP_SEL(pipe));
}

static void px30_lvds_disable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, PX30_GRF_PD_VO_CON1,
		     PX30_LVDS_MODE_EN(0) | PX30_LVDS_P2S_EN(0));
}

static const struct rockchip_lvds_funcs px30_lvds_funcs = {
	.enable = px30_lvds_enable,
	.disable = px30_lvds_disable,
};

static void rk3126_lvds_enable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3126_GRF_LVDS_CON0,
		     RK3126_LVDS_P2S_EN(1) | RK3126_LVDS_MODE_EN(1) |
		     RK3126_LVDS_MSBSEL(1) | RK3126_LVDS_SELECT(lvds->format));
}

static void rk3126_lvds_disable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3126_GRF_LVDS_CON0,
		     RK3126_LVDS_P2S_EN(0) | RK3126_LVDS_MODE_EN(0));
}

static const struct rockchip_lvds_funcs rk3126_lvds_funcs = {
	.enable = rk3126_lvds_enable,
	.disable = rk3126_lvds_disable,
};

static void rk3288_lvds_enable(struct rockchip_lvds *lvds)
{
	struct drm_display_mode *mode = &lvds->mode;
	int pipe;
	u32 val;

	pipe = drm_of_encoder_active_endpoint_id(lvds->dev->of_node,
						 &lvds->encoder);
	regmap_write(lvds->grf, RK3288_GRF_SOC_CON6,
		     RK3288_LVDS_LCDC_SEL(pipe));

	val = RK3288_LVDS_PWRDWN(0) | RK3288_LVDS_CON_CLKINV(0) |
	      RK3288_LVDS_CON_CHASEL(lvds->dual_channel) |
	      RK3288_LVDS_CON_SELECT(lvds->format);

	if (lvds->dual_channel) {
		u32 h_bp = mode->htotal - mode->hsync_start;

		val |= RK3288_LVDS_CON_ENABLE_2(1) |
		       RK3288_LVDS_CON_ENABLE_1(1) |
		       RK3288_LVDS_CON_STARTSEL(lvds->data_swap);

		if (h_bp % 2)
			val |= RK3288_LVDS_CON_STARTPHASE(1);
		else
			val |= RK3288_LVDS_CON_STARTPHASE(0);

	} else {
		val |= RK3288_LVDS_CON_ENABLE_2(0) |
		       RK3288_LVDS_CON_ENABLE_1(1);
	}

	regmap_write(lvds->grf, RK3288_GRF_SOC_CON7, val);

	phy_set_bus_width(lvds->phy, lvds->dual_channel ? 2 : 1);
}

static void rk3288_lvds_disable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3288_GRF_SOC_CON7, RK3288_LVDS_PWRDWN(1));
}

static const struct rockchip_lvds_funcs rk3288_lvds_funcs = {
	.enable = rk3288_lvds_enable,
	.disable = rk3288_lvds_disable,
};

static void rk3368_lvds_enable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3368_GRF_SOC_CON7,
		     RK3368_LVDS_SELECT(lvds->format) |
		     RK3368_LVDS_MODE_EN(1) | RK3368_LVDS_MSBSEL(1) |
		     RK3368_LVDS_P2S_EN(1));
}

static void rk3368_lvds_disable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3368_GRF_SOC_CON7,
		     RK3368_LVDS_MODE_EN(0) | RK3368_LVDS_P2S_EN(0));
}

static const struct rockchip_lvds_funcs rk3368_lvds_funcs = {
	.enable = rk3368_lvds_enable,
	.disable = rk3368_lvds_disable,
};

static const struct of_device_id rockchip_lvds_dt_ids[] = {
	{ .compatible = "rockchip,px30-lvds", .data = &px30_lvds_funcs },
	{ .compatible = "rockchip,rk3126-lvds", .data = &rk3126_lvds_funcs },
	{ .compatible = "rockchip,rk3288-lvds", .data = &rk3288_lvds_funcs },
	{ .compatible = "rockchip,rk3368-lvds", .data = &rk3368_lvds_funcs },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_lvds_dt_ids);

static struct platform_driver rockchip_lvds_driver = {
	.probe = rockchip_lvds_probe,
	.remove = rockchip_lvds_remove,
	.driver = {
		.name = "rockchip-lvds",
		.of_match_table = of_match_ptr(rockchip_lvds_dt_ids),
	},
};
module_platform_driver(rockchip_lvds_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("ROCKCHIP LVDS Driver");
MODULE_LICENSE("GPL v2");
