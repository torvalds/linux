// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Sandy Huang <hjc@rock-chips.com>
 */

#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define HIWORD_UPDATE(v, l, h)	(((v) << (l)) | (GENMASK(h, l) << 16))

#define PX30_GRF_PD_VO_CON1		0x0438
#define PX30_RGB_DATA_SYNC_BYPASS(v)	HIWORD_UPDATE(v, 3, 3)
#define PX30_RGB_VOP_SEL(v)		HIWORD_UPDATE(v, 2, 2)

#define RK1808_GRF_PD_VO_CON1		0x0444
#define RK1808_RGB_DATA_SYNC_BYPASS(v)	HIWORD_UPDATE(v, 3, 3)

#define RV1106_VENC_GRF_VOP_IO_WRAPPER	0x1000c
#define RV1106_IO_BYPASS_SEL(v)		HIWORD_UPDATE(v, 0, 1)
#define RV1106_VOGRF_VOP_PIPE_BYPASS	0x60034
#define RV1106_VOP_PIPE_BYPASS(v)	HIWORD_UPDATE(v, 0, 1)

#define RV1126_GRF_IOFUNC_CON3		0x1026c
#define RV1126_LCDC_IO_BYPASS(v)	HIWORD_UPDATE(v, 0, 0)

#define RK3288_GRF_SOC_CON6		0x025c
#define RK3288_LVDS_LCDC_SEL(x)		HIWORD_UPDATE(x,  3,  3)
#define RK3288_GRF_SOC_CON7		0x0260
#define RK3288_LVDS_PWRDWN(x)		HIWORD_UPDATE(x, 15, 15)
#define RK3288_LVDS_CON_ENABLE_2(x)	HIWORD_UPDATE(x, 12, 12)
#define RK3288_LVDS_CON_ENABLE_1(x)	HIWORD_UPDATE(x, 11, 11)
#define RK3288_LVDS_CON_CLKINV(x)	HIWORD_UPDATE(x,  8,  8)
#define RK3288_LVDS_CON_TTL_EN(x)	HIWORD_UPDATE(x,  6,  6)

#define RK3568_GRF_VO_CON1		0X0364
#define RK3568_RGB_DATA_BYPASS(v)	HIWORD_UPDATE(v, 6, 6)

struct rockchip_rgb;

struct rockchip_rgb_funcs {
	void (*enable)(struct rockchip_rgb *rgb);
	void (*disable)(struct rockchip_rgb *rgb);
};

struct rockchip_rgb_data {
	u32 max_dclk_rate;
	const struct rockchip_rgb_funcs *funcs;
};

struct rockchip_rgb {
	u8 id;
	u32 max_dclk_rate;
	struct device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct phy *phy;
	struct regmap *grf;
	bool data_sync_bypass;
	const struct rockchip_rgb_funcs *funcs;
	struct rockchip_drm_sub_dev sub_dev;
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

static int
rockchip_rgb_atomic_connector_get_property(struct drm_connector *connector,
					   const struct drm_connector_state *state,
					   struct drm_property *property,
					   uint64_t *val)
{
	struct rockchip_rgb *rgb = connector_to_rgb(connector);
	struct rockchip_drm_private *private = connector->dev->dev_private;

	if (property == private->connector_id_prop) {
		*val = rgb->id;
		return 0;
	}

	DRM_ERROR("failed to get rockchip RGB property\n");
	return -EINVAL;
}

static const struct drm_connector_funcs rockchip_rgb_connector_funcs = {
	.detect = rockchip_rgb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_get_property = rockchip_rgb_atomic_connector_get_property,
};

static int rockchip_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_rgb *rgb = connector_to_rgb(connector);
	struct drm_panel *panel = rgb->panel;

	return drm_panel_get_modes(panel, connector);
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

	pinctrl_pm_select_default_state(rgb->dev);

	if (rgb->funcs && rgb->funcs->enable)
		rgb->funcs->enable(rgb);

	if (rgb->phy)
		phy_power_on(rgb->phy);

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
		s->output_if = VOP_OUTPUT_IF_RGB;
		break;
	case MEDIA_BUS_FMT_RGB565_1X16:
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
		s->output_if = VOP_OUTPUT_IF_RGB;
		break;
	case MEDIA_BUS_FMT_RGB888_3X8:
		s->output_mode = ROCKCHIP_OUT_MODE_S888;
		s->output_if = VOP_OUTPUT_IF_RGB;
		break;
	case MEDIA_BUS_FMT_RGB888_DUMMY_4X8:
		s->output_mode = ROCKCHIP_OUT_MODE_S888_DUMMY;
		s->output_if = VOP_OUTPUT_IF_RGB;
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
		s->output_mode = ROCKCHIP_OUT_MODE_BT656;
		s->output_if = VOP_OUTPUT_IF_BT656;
		break;
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YVYU8_1X16:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		s->output_mode = ROCKCHIP_OUT_MODE_BT1120;
		s->output_if = VOP_OUTPUT_IF_BT1120;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	default:
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
		s->output_if = VOP_OUTPUT_IF_RGB;
		break;
	}

	s->output_type = DRM_MODE_CONNECTOR_DPI;
	s->bus_flags = info->bus_flags;
	s->tv_state = &conn_state->tv;
	s->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	return 0;
}

static void rockchip_rgb_encoder_loader_protect(struct drm_encoder *encoder,
						bool on)
{
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);

	if (rgb->panel)
		panel_simple_loader_protect(rgb->panel);
}

static enum drm_mode_status
rockchip_rgb_encoder_mode_valid(struct drm_encoder *encoder,
				 const struct drm_display_mode *mode)
{
	struct rockchip_rgb *rgb = encoder_to_rgb(encoder);
	u32 request_clock = mode->clock;
	u32 max_clock = rgb->max_dclk_rate;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		request_clock *= 2;

	if (max_clock != 0 && request_clock > max_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const
struct drm_encoder_helper_funcs rockchip_rgb_encoder_helper_funcs = {
	.enable = rockchip_rgb_encoder_enable,
	.disable = rockchip_rgb_encoder_disable,
	.atomic_check = rockchip_rgb_encoder_atomic_check,
	.mode_valid = rockchip_rgb_encoder_mode_valid,
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

	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_rgb_encoder_funcs,
			       DRM_MODE_ENCODER_DPI, NULL);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to initialize encoder: %d\n", ret);
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_rgb_encoder_helper_funcs);

	if (rgb->panel) {
		struct rockchip_drm_private *private = drm_dev->dev_private;

		connector = &rgb->connector;
		connector->interlace_allowed = true;
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

		ret = drm_connector_attach_encoder(connector, encoder);
		if (ret < 0) {
			DRM_DEV_ERROR(dev,
				      "failed to attach encoder: %d\n", ret);
			goto err_free_connector;
		}
		rgb->sub_dev.connector = &rgb->connector;
		rgb->sub_dev.of_node = rgb->dev->of_node;
		rgb->sub_dev.loader_protect = rockchip_rgb_encoder_loader_protect;
		drm_object_attach_property(&connector->base, private->connector_id_prop, 0);
		rockchip_drm_register_sub_dev(&rgb->sub_dev);
	} else {
		rgb->bridge->encoder = encoder;
		ret = drm_bridge_attach(encoder, rgb->bridge, NULL, 0);
		if (ret) {
			DRM_DEV_ERROR(dev,
				      "failed to attach bridge: %d\n", ret);
			goto err_free_encoder;
		}
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

	if (rgb->sub_dev.connector)
		rockchip_drm_register_sub_dev(&rgb->sub_dev);
	if (rgb->panel)
		drm_connector_cleanup(&rgb->connector);

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
	const struct rockchip_rgb_data *rgb_data;
	int ret, id;

	rgb = devm_kzalloc(&pdev->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	id = of_alias_get_id(dev->of_node, "rgb");
	if (id < 0)
		id = 0;

	rgb_data = of_device_get_match_data(dev);
	rgb->id = id;
	rgb->dev = dev;
	rgb->max_dclk_rate = rgb_data->max_dclk_rate;
	rgb->funcs = rgb_data->funcs;
	platform_set_drvdata(pdev, rgb);

	rgb->data_sync_bypass =
	    of_property_read_bool(dev->of_node, "rockchip,data-sync-bypass");

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
		     PX30_RGB_DATA_SYNC_BYPASS(rgb->data_sync_bypass));
}

static const struct rockchip_rgb_funcs px30_rgb_funcs = {
	.enable = px30_rgb_enable,
};

static const struct rockchip_rgb_data px30_rgb = {
	.funcs = &px30_rgb_funcs,
};

static void rk1808_rgb_enable(struct rockchip_rgb *rgb)
{
	regmap_write(rgb->grf, RK1808_GRF_PD_VO_CON1,
		     RK1808_RGB_DATA_SYNC_BYPASS(rgb->data_sync_bypass));
}

static const struct rockchip_rgb_funcs rk1808_rgb_funcs = {
	.enable = rk1808_rgb_enable,
};

static const struct rockchip_rgb_data rk1808_rgb = {
	.funcs = &rk1808_rgb_funcs,
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

static const struct rockchip_rgb_data rk3288_rgb = {
	.funcs = &rk3288_rgb_funcs,
};

static void rk3568_rgb_enable(struct rockchip_rgb *rgb)
{
	regmap_write(rgb->grf, RK3568_GRF_VO_CON1,
		     RK3568_RGB_DATA_BYPASS(rgb->data_sync_bypass));
}

static const struct rockchip_rgb_funcs rk3568_rgb_funcs = {
	.enable = rk3568_rgb_enable,
};

static const struct rockchip_rgb_data rk3568_rgb = {
	.funcs = &rk3568_rgb_funcs,
};

static void rv1126_rgb_enable(struct rockchip_rgb *rgb)
{
	regmap_write(rgb->grf, RV1126_GRF_IOFUNC_CON3,
		     RV1126_LCDC_IO_BYPASS(rgb->data_sync_bypass));
}

static const struct rockchip_rgb_funcs rv1126_rgb_funcs = {
	.enable = rv1126_rgb_enable,
};

static const struct rockchip_rgb_data rv1126_rgb = {
	.funcs = &rv1126_rgb_funcs,
};

static void rv1106_rgb_enable(struct rockchip_rgb *rgb)
{
	regmap_write(rgb->grf, RV1106_VENC_GRF_VOP_IO_WRAPPER,
		     RV1106_IO_BYPASS_SEL(rgb->data_sync_bypass ? 0x3 : 0x0));
	regmap_write(rgb->grf, RV1106_VOGRF_VOP_PIPE_BYPASS,
		     RV1106_VOP_PIPE_BYPASS(rgb->data_sync_bypass ? 0x3 : 0x0));
}

static const struct rockchip_rgb_funcs rv1106_rgb_funcs = {
	.enable = rv1106_rgb_enable,
};

static const struct rockchip_rgb_data rv1106_rgb = {
	.max_dclk_rate = 74250,
	.funcs = &rv1106_rgb_funcs,
};

static const struct of_device_id rockchip_rgb_dt_ids[] = {
	{ .compatible = "rockchip,px30-rgb", .data = &px30_rgb },
	{ .compatible = "rockchip,rk1808-rgb", .data = &rk1808_rgb },
	{ .compatible = "rockchip,rk3066-rgb", },
	{ .compatible = "rockchip,rk3128-rgb", },
	{ .compatible = "rockchip,rk3288-rgb", .data = &rk3288_rgb },
	{ .compatible = "rockchip,rk3308-rgb", },
	{ .compatible = "rockchip,rk3368-rgb", },
	{ .compatible = "rockchip,rk3568-rgb", .data = &rk3568_rgb },
	{ .compatible = "rockchip,rk3588-rgb", },
	{ .compatible = "rockchip,rv1106-rgb", .data = &rv1106_rgb},
	{ .compatible = "rockchip,rv1108-rgb", },
	{ .compatible = "rockchip,rv1126-rgb", .data = &rv1126_rgb},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_rgb_dt_ids);

struct platform_driver rockchip_rgb_driver = {
	.probe = rockchip_rgb_probe,
	.remove = rockchip_rgb_remove,
	.driver = {
		.name = "rockchip-rgb",
		.of_match_table = of_match_ptr(rockchip_rgb_dt_ids),
	},
};
