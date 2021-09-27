// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:
 *      Mark Yao <mark.yao@rock-chips.com>
 *      Sandy Huang <hjc@rock-chips.com>
 */

#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define HIWORD_UPDATE(v, h, l)  (((v) << (l)) | (GENMASK(h, l) << 16))

#define PX30_GRF_PD_VO_CON1		0x0438
#define PX30_LVDS_SELECT(x)		HIWORD_UPDATE(x, 14, 13)
#define PX30_LVDS_MODE_EN(x)		HIWORD_UPDATE(x, 12, 12)
#define PX30_LVDS_MSBSEL(x)		HIWORD_UPDATE(x, 11, 11)
#define PX30_LVDS_P2S_EN(x)		HIWORD_UPDATE(x,  6,  6)
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

#define RK3568_GRF_VO_CON0		0x0360
#define RK3568_LVDS1_SELECT(x)		HIWORD_UPDATE(x, 13, 12)
#define RK3568_LVDS1_MSBSEL(x)		HIWORD_UPDATE(x, 11, 11)
#define RK3568_LVDS0_SELECT(x)		HIWORD_UPDATE(x,  5,  4)
#define RK3568_LVDS0_MSBSEL(x)		HIWORD_UPDATE(x,  3,  3)
#define RK3568_GRF_VO_CON2		0x0368
#define RK3568_LVDS0_DCLK_INV_SEL(x)	HIWORD_UPDATE(x,  9,  9)
#define RK3568_LVDS0_DCLK_DIV2_SEL(x)	HIWORD_UPDATE(x,  8,  8)
#define RK3568_LVDS0_MODE_EN(x)		HIWORD_UPDATE(x,  1,  1)
#define RK3568_LVDS0_P2S_EN(x)		HIWORD_UPDATE(x,  0,  0)
#define RK3568_GRF_VO_CON3		0x036c
#define RK3568_LVDS1_DCLK_INV_SEL(x)	HIWORD_UPDATE(x,  9,  9)
#define RK3568_LVDS1_DCLK_DIV2_SEL(x)	HIWORD_UPDATE(x,  8,  8)
#define RK3568_LVDS1_MODE_EN(x)		HIWORD_UPDATE(x,  1,  1)
#define RK3568_LVDS1_P2S_EN(x)		HIWORD_UPDATE(x,  0,  0)

enum lvds_format {
	LVDS_8BIT_MODE_FORMAT_1,
	LVDS_8BIT_MODE_FORMAT_2,
	LVDS_8BIT_MODE_FORMAT_3,
	LVDS_6BIT_MODE,
	LVDS_10BIT_MODE_FORMAT_1,
	LVDS_10BIT_MODE_FORMAT_2,
};

struct rockchip_lvds;

struct rockchip_lvds_funcs {
	int (*probe)(struct rockchip_lvds *lvds);
	void (*enable)(struct rockchip_lvds *lvds);
	void (*disable)(struct rockchip_lvds *lvds);
};

struct rockchip_lvds {
	int id;
	struct device *dev;
	struct phy *phy;
	struct regmap *grf;
	const struct rockchip_lvds_funcs *funcs;
	enum lvds_format format;
	bool data_swap;
	bool dual_channel;
	enum drm_lvds_dual_link_pixels pixel_order;

	struct rockchip_lvds *primary;
	struct rockchip_lvds *secondary;

	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_display_mode mode;
	struct rockchip_drm_sub_dev sub_dev;
};

static inline struct rockchip_lvds *connector_to_lvds(struct drm_connector *c)
{
	return container_of(c, struct rockchip_lvds, connector);
}

static inline struct rockchip_lvds *encoder_to_lvds(struct drm_encoder *e)
{
	return container_of(e, struct rockchip_lvds, encoder);
}

static int
rockchip_lvds_atomic_connector_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t *val)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);
	struct rockchip_drm_private *private = connector->dev->dev_private;

	if (property == private->connector_id_prop) {
		*val = lvds->id;
		return 0;
	}

	DRM_ERROR("failed to get rockchip LVDS property\n");
	return -EINVAL;
}

static const struct drm_connector_funcs rockchip_lvds_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_get_property = rockchip_lvds_atomic_connector_get_property,
};

static int rockchip_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rockchip_lvds *lvds = connector_to_lvds(connector);
	struct drm_panel *panel = lvds->panel;

	return drm_panel_get_modes(panel, connector);
}

static const
struct drm_connector_helper_funcs rockchip_lvds_connector_helper_funcs = {
	.get_modes = rockchip_lvds_connector_get_modes,
};

static void
rockchip_lvds_encoder_atomic_mode_set(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	struct drm_connector *connector = &lvds->connector;
	struct drm_display_info *info = &connector->display_info;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	if (info->num_bus_formats)
		bus_format = info->bus_formats[0];

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:	/* jeida-24 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_2;
		break;
	case MEDIA_BUS_FMT_RGB101010_1X7X5_JEIDA: /* jeida-30 */
		lvds->format = LVDS_10BIT_MODE_FORMAT_2;
		break;
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:	/* vesa-18 */
		lvds->format = LVDS_8BIT_MODE_FORMAT_3;
		break;
	case MEDIA_BUS_FMT_RGB101010_1X7X5_SPWG: /* vesa-30 */
		lvds->format = LVDS_10BIT_MODE_FORMAT_1;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:	/* vesa-24 */
	default:
		lvds->format = LVDS_8BIT_MODE_FORMAT_1;
		break;
	}

	if (lvds->secondary)
		lvds->secondary->format = lvds->format;

	drm_mode_copy(&lvds->mode, &crtc_state->adjusted_mode);
}

static int
rockchip_lvds_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	struct drm_connector *connector = conn_state->connector;
	struct drm_display_info *info = &connector->display_info;

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	s->output_mode = ROCKCHIP_OUT_MODE_P888;

	if (s->bus_format == MEDIA_BUS_FMT_RGB101010_1X7X5_SPWG ||
	    s->bus_format == MEDIA_BUS_FMT_RGB101010_1X7X5_JEIDA)
		s->output_mode = ROCKCHIP_OUT_MODE_AAAA;

	s->output_type = DRM_MODE_CONNECTOR_LVDS;
	s->bus_flags = info->bus_flags;
	s->tv_state = &conn_state->tv;
	s->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	switch (lvds->pixel_order) {
	case DRM_LVDS_DUAL_LINK_ODD_EVEN_PIXELS:
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_ODD_EVEN_MODE;
		s->output_if |= VOP_OUTPUT_IF_LVDS1 | VOP_OUTPUT_IF_LVDS0;
		break;
	case DRM_LVDS_DUAL_LINK_EVEN_ODD_PIXELS:
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_ODD_EVEN_MODE;
		s->output_flags |= ROCKCHIP_OUTPUT_DATA_SWAP;
		s->output_if |= VOP_OUTPUT_IF_LVDS1 | VOP_OUTPUT_IF_LVDS0;
		break;
/*
 * Fix me: To do it with a GKI compatible version.
 */
#if 0
	case DRM_LVDS_DUAL_LINK_LEFT_RIGHT_PIXELS:
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
		s->output_if |= VOP_OUTPUT_IF_LVDS1 | VOP_OUTPUT_IF_LVDS0;
		break;
	case DRM_LVDS_DUAL_LINK_RIGHT_LEFT_PIXELS:
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
		s->output_flags |= ROCKCHIP_OUTPUT_DATA_SWAP;
		s->output_if |= VOP_OUTPUT_IF_LVDS1 | VOP_OUTPUT_IF_LVDS0;
		break;
#endif
	default:
		if (lvds->id)
			s->output_if |= VOP_OUTPUT_IF_LVDS1;
		else
			s->output_if |= VOP_OUTPUT_IF_LVDS0;
		break;
	}

	return 0;
}

static void rockchip_lvds_enable(struct rockchip_lvds *lvds)
{
	int ret;

	if (lvds->funcs->enable)
		lvds->funcs->enable(lvds);

	ret = phy_set_mode(lvds->phy, PHY_MODE_LVDS);
	if (ret) {
		DRM_DEV_ERROR(lvds->dev, "failed to set phy mode: %d\n", ret);
		return;
	}

	phy_power_on(lvds->phy);

	if (lvds->secondary)
		rockchip_lvds_enable(lvds->secondary);
}

static void rockchip_lvds_disable(struct rockchip_lvds *lvds)
{
	if (lvds->funcs->disable)
		lvds->funcs->disable(lvds);

	phy_power_off(lvds->phy);

	if (lvds->secondary)
		rockchip_lvds_disable(lvds->secondary);
}

static void rockchip_lvds_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->panel)
		drm_panel_prepare(lvds->panel);
	rockchip_lvds_enable(lvds);
	if (lvds->panel)
		drm_panel_enable(lvds->panel);
}

static void rockchip_lvds_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->panel)
		drm_panel_disable(lvds->panel);
	rockchip_lvds_disable(lvds);
	if (lvds->panel)
		drm_panel_unprepare(lvds->panel);
}

static void rockchip_lvds_encoder_loader_protect(struct drm_encoder *encoder,
						 bool on)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->panel)
		panel_simple_loader_protect(lvds->panel);
}

static const
struct drm_encoder_helper_funcs rockchip_lvds_encoder_helper_funcs = {
	.enable = rockchip_lvds_encoder_enable,
	.disable = rockchip_lvds_encoder_disable,
	.atomic_check = rockchip_lvds_encoder_atomic_check,
	.atomic_mode_set = rockchip_lvds_encoder_atomic_mode_set,
};

static const struct drm_encoder_funcs rockchip_lvds_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int rockchip_lvds_match_by_id(struct device *dev, const void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	unsigned int *id = (unsigned int *)data;

	return lvds->id == *id;
}

static struct rockchip_lvds *rockchip_lvds_find_by_id(struct device_driver *drv,
						      unsigned int id)
{
	struct device *dev;

	dev = driver_find_device(drv, NULL, &id, rockchip_lvds_match_by_id);
	if (!dev)
		return NULL;

	return dev_get_drvdata(dev);
}

static int rockchip_lvds_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder = &lvds->encoder;
	struct drm_connector *connector = &lvds->connector;
	int ret;

	/*
	 * dual channel lvds mode only need to register one connector.
	 */
	if (lvds->primary)
		return 0;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &lvds->panel, &lvds->bridge);
	if (ret)
		return ret;

	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_lvds_encoder_funcs,
			       DRM_MODE_ENCODER_LVDS, NULL);
	if (ret < 0) {
		DRM_DEV_ERROR(lvds->dev,
			      "failed to initialize encoder: %d\n", ret);
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_lvds_encoder_helper_funcs);

	if (lvds->panel) {
		struct rockchip_drm_private *private = drm_dev->dev_private;

		ret = drm_connector_init(drm_dev, connector,
					 &rockchip_lvds_connector_funcs,
					 DRM_MODE_CONNECTOR_LVDS);
		if (ret < 0) {
			DRM_DEV_ERROR(drm_dev->dev,
				      "failed to initialize connector: %d\n", ret);
			goto err_free_encoder;
		}

		drm_connector_helper_add(connector,
					 &rockchip_lvds_connector_helper_funcs);

		ret = drm_connector_attach_encoder(connector, encoder);
		if (ret < 0) {
			DRM_DEV_ERROR(lvds->dev,
				      "failed to attach encoder: %d\n", ret);
			goto err_free_connector;
		}

		lvds->sub_dev.connector = &lvds->connector;
		lvds->sub_dev.of_node = lvds->dev->of_node;
		lvds->sub_dev.loader_protect = rockchip_lvds_encoder_loader_protect;
		rockchip_drm_register_sub_dev(&lvds->sub_dev);
		drm_object_attach_property(&connector->base, private->connector_id_prop, 0);
	} else {
		ret = drm_bridge_attach(encoder, lvds->bridge, NULL, 0);
		if (ret) {
			DRM_DEV_ERROR(lvds->dev,
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

static void rockchip_lvds_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);

	if (lvds->sub_dev.connector)
		rockchip_drm_unregister_sub_dev(&lvds->sub_dev);
	if (lvds->panel)
		drm_connector_cleanup(&lvds->connector);

	if (lvds->encoder.dev)
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

	if (!dev->of_node)
		return -ENODEV;

	lvds = devm_kzalloc(dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->id = of_alias_get_id(dev->of_node, "lvds");
	if (lvds->id < 0)
		lvds->id = 0;

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
		DRM_DEV_ERROR(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	lvds->grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(lvds->grf)) {
		ret = PTR_ERR(lvds->grf);
		DRM_DEV_ERROR(dev, "Unable to get grf: %d\n", ret);
		return ret;
	}

	lvds->pixel_order = -1;
	if (lvds->funcs->probe) {
		ret = lvds->funcs->probe(lvds);
		if (ret)
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

static int __maybe_unused rockchip_secondary_lvds_probe(struct rockchip_lvds *lvds)
{
	if (lvds->dual_channel) {
		struct rockchip_lvds *secondary = NULL;
		struct device_node *port0, *port1;
		int pixel_order;

		secondary = rockchip_lvds_find_by_id(lvds->dev->driver, 1);
		if (!secondary)
			return -EPROBE_DEFER;

		port0 = of_graph_get_port_by_id(lvds->dev->of_node, 1);
		port1 = of_graph_get_port_by_id(secondary->dev->of_node, 1);
		pixel_order = drm_of_lvds_get_dual_link_pixel_order(port0, port1);
		of_node_put(port1);
		of_node_put(port0);

		secondary->primary = lvds;
		lvds->secondary = secondary;
		lvds->pixel_order = pixel_order >= 0 ? pixel_order : 0;
	}

	return 0;
}

static void rk3568_lvds_enable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3568_GRF_VO_CON2,
		     RK3568_LVDS0_MODE_EN(1) | RK3568_LVDS0_P2S_EN(1) |
		     RK3568_LVDS0_DCLK_INV_SEL(1));
	regmap_write(lvds->grf, RK3568_GRF_VO_CON0,
		     RK3568_LVDS0_SELECT(lvds->format) | RK3568_LVDS0_MSBSEL(1));
}

static void rk3568_lvds_disable(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3568_GRF_VO_CON2, RK3568_LVDS0_MODE_EN(0));
}

static const struct rockchip_lvds_funcs rk3568_lvds_funcs = {
	.enable = rk3568_lvds_enable,
	.disable = rk3568_lvds_disable,
};

static const struct of_device_id rockchip_lvds_dt_ids[] = {
	{ .compatible = "rockchip,px30-lvds", .data = &px30_lvds_funcs },
	{ .compatible = "rockchip,rk3126-lvds", .data = &rk3126_lvds_funcs },
	{ .compatible = "rockchip,rk3288-lvds", .data = &rk3288_lvds_funcs },
	{ .compatible = "rockchip,rk3368-lvds", .data = &rk3368_lvds_funcs },
	{ .compatible = "rockchip,rk3568-lvds", .data = &rk3568_lvds_funcs },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_lvds_dt_ids);

struct platform_driver rockchip_lvds_driver = {
	.probe = rockchip_lvds_probe,
	.remove = rockchip_lvds_remove,
	.driver = {
		   .name = "rockchip-lvds",
		   .of_match_table = of_match_ptr(rockchip_lvds_dt_ids),
	},
};
