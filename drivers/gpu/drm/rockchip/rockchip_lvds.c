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
#include "rockchip_lvds.h"

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

#define RK3368_GRF_SOC_CON7		0x041c
#define RK3368_LVDS_SELECT(x)		HIWORD_UPDATE(x, 14, 13)
#define RK3368_LVDS_MODE_EN(x)		HIWORD_UPDATE(x, 12, 12)
#define RK3368_LVDS_MSBSEL(x)		HIWORD_UPDATE(x, 11, 11)
#define RK3368_LVDS_P2S_EN(x)		HIWORD_UPDATE(x,  6,  6)

#define DISPLAY_OUTPUT_RGB		0
#define DISPLAY_OUTPUT_LVDS		1
#define DISPLAY_OUTPUT_DUAL_LVDS	2

#define connector_to_lvds(c) \
		container_of(c, struct rockchip_lvds, connector)

#define encoder_to_lvds(c) \
		container_of(c, struct rockchip_lvds, encoder)

enum chip_type {
	PX30,
	RK3126,
	RK3288,
	RK3368,
};

struct rockchip_lvds;

struct rockchip_lvds_soc_data {
	enum chip_type chip_type;
	int (*probe)(struct rockchip_lvds *lvds);
	int (*power_on)(struct rockchip_lvds *lvds);
	void (*power_off)(struct rockchip_lvds *lvds);
};

struct rockchip_lvds {
	struct device *dev;
	struct phy *phy;
	void __iomem *regs;
	struct regmap *grf;
	struct clk *pclk;
	const struct rockchip_lvds_soc_data *soc_data;

	int output;
	int format;

	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct drm_display_mode mode;
};

static inline void lvds_writel(struct rockchip_lvds *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs + offset);
	if ((lvds->output != DISPLAY_OUTPUT_LVDS) &&
	    (lvds->soc_data->chip_type == RK3288))
		writel_relaxed(val,
			       lvds->regs + offset + RK3288_LVDS_CH1_OFFSET);
}

static inline int lvds_name_to_format(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "jeida", 6) == 0)
		return LVDS_FORMAT_JEIDA;
	else if (strncmp(s, "vesa", 5) == 0)
		return LVDS_FORMAT_VESA;

	return -EINVAL;
}

static inline int lvds_name_to_output(const char *s)
{
	if (!s)
		return -EINVAL;

	if (strncmp(s, "rgb", 3) == 0)
		return DISPLAY_OUTPUT_RGB;
	else if (strncmp(s, "lvds", 4) == 0)
		return DISPLAY_OUTPUT_LVDS;
	else if (strncmp(s, "duallvds", 8) == 0)
		return DISPLAY_OUTPUT_DUAL_LVDS;

	return -EINVAL;
}

static int innov2_lvds_power_on(struct rockchip_lvds *lvds)
{
	u32 status;
	int ret;

	if (lvds->output == DISPLAY_OUTPUT_RGB) {
		lvds_writel(lvds, RK3288_LVDS_CH0_REG0,
			    RK3288_LVDS_CH0_REG0_TTL_EN |
			    RK3288_LVDS_CH0_REG0_LANECK_EN |
			    RK3288_LVDS_CH0_REG0_LANE4_EN |
			    RK3288_LVDS_CH0_REG0_LANE3_EN |
			    RK3288_LVDS_CH0_REG0_LANE2_EN |
			    RK3288_LVDS_CH0_REG0_LANE1_EN |
			    RK3288_LVDS_CH0_REG0_LANE0_EN);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG2,
			    RK3288_LVDS_PLL_FBDIV_REG2(0x46));

		lvds_writel(lvds, RK3288_LVDS_CH0_REG3,
			    RK3288_LVDS_PLL_FBDIV_REG3(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG4,
			    RK3288_LVDS_CH0_REG4_LANECK_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE4_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE3_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE2_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE1_TTL_MODE |
			    RK3288_LVDS_CH0_REG4_LANE0_TTL_MODE);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG5,
			    RK3288_LVDS_CH0_REG5_LANECK_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE4_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE3_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE2_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE1_TTL_DATA |
			    RK3288_LVDS_CH0_REG5_LANE0_TTL_DATA);
		lvds_writel(lvds, RK3288_LVDS_CH0_REGD,
			    RK3288_LVDS_PLL_PREDIV_REGD(0x0a));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG20,
			    RK3288_LVDS_CH0_REG20_LSB);
	} else {
		lvds_writel(lvds, RK3288_LVDS_CH0_REG0,
			    RK3288_LVDS_CH0_REG0_LVDS_EN |
			    RK3288_LVDS_CH0_REG0_LANECK_EN |
			    RK3288_LVDS_CH0_REG0_LANE4_EN |
			    RK3288_LVDS_CH0_REG0_LANE3_EN |
			    RK3288_LVDS_CH0_REG0_LANE2_EN |
			    RK3288_LVDS_CH0_REG0_LANE1_EN |
			    RK3288_LVDS_CH0_REG0_LANE0_EN);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG1,
			    RK3288_LVDS_CH0_REG1_LANECK_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE4_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE3_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE2_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE1_BIAS |
			    RK3288_LVDS_CH0_REG1_LANE0_BIAS);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG2,
			    RK3288_LVDS_CH0_REG2_RESERVE_ON |
			    RK3288_LVDS_CH0_REG2_LANECK_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE4_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE3_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE2_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE1_LVDS_MODE |
			    RK3288_LVDS_CH0_REG2_LANE0_LVDS_MODE |
			    RK3288_LVDS_PLL_FBDIV_REG2(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG3,
			    RK3288_LVDS_PLL_FBDIV_REG3(0x46));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG4, 0x00);
		lvds_writel(lvds, RK3288_LVDS_CH0_REG5, 0x00);
		lvds_writel(lvds, RK3288_LVDS_CH0_REGD,
			    RK3288_LVDS_PLL_PREDIV_REGD(0x0a));
		lvds_writel(lvds, RK3288_LVDS_CH0_REG20,
			    RK3288_LVDS_CH0_REG20_LSB);
	}

	writel(RK3288_LVDS_CFG_REGC_PLL_ENABLE,
	       lvds->regs + RK3288_LVDS_CFG_REGC);
	ret = readl_poll_timeout(lvds->regs + RK3288_LVDS_CH0_REGF, status,
				 status & RK3288_LVDS_CH0_PLL_LOCK, 500, 10000);
	if (ret) {
		dev_err(lvds->dev, "PLL is not lock\n");
		return ret;
	}

	writel(RK3288_LVDS_CFG_REG21_TX_ENABLE,
	       lvds->regs + RK3288_LVDS_CFG_REG21);

	return 0;
}

static void innov2_lvds_power_off(struct rockchip_lvds *lvds)
{
	writel(RK3288_LVDS_CFG_REG21_TX_DISABLE,
	       lvds->regs + RK3288_LVDS_CFG_REG21);
	writel(RK3288_LVDS_CFG_REGC_PLL_DISABLE,
	       lvds->regs + RK3288_LVDS_CFG_REGC);
}

static enum drm_connector_status
rockchip_lvds_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void rockchip_lvds_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rockchip_lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = rockchip_lvds_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rockchip_lvds_connector_destroy,
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

	if (lvds->panel)
		drm_panel_loader_protect(lvds->panel, on);

	return 0;
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
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->output == DISPLAY_OUTPUT_RGB)
		s->output_type = DRM_MODE_CONNECTOR_DPI;
	else
		s->output_type = DRM_MODE_CONNECTOR_LVDS;

	if (info->num_bus_formats)
		s->bus_format = info->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	if ((s->bus_format == MEDIA_BUS_FMT_RGB666_1X18) &&
	    (lvds->output == DISPLAY_OUTPUT_RGB))
		s->output_mode = ROCKCHIP_OUT_MODE_P666;
	else if ((s->bus_format == MEDIA_BUS_FMT_RGB565_1X16) &&
		 (lvds->output == DISPLAY_OUTPUT_RGB))
		s->output_mode = ROCKCHIP_OUT_MODE_P565;
	else
		s->output_mode = ROCKCHIP_OUT_MODE_P888;
	s->tv_state = &conn_state->tv;
	s->eotf = TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;

	return 0;
}

static void rockchip_lvds_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);
	int ret;

	clk_prepare_enable(lvds->pclk);
	pm_runtime_get_sync(lvds->dev);

	if (lvds->panel)
		drm_panel_prepare(lvds->panel);

	if (lvds->soc_data->power_on)
		lvds->soc_data->power_on(lvds);

	if (lvds->phy) {
		ret = phy_set_mode(lvds->phy, PHY_MODE_VIDEO_LVDS);
		if (ret) {
			dev_err(lvds->dev, "failed to set phy mode: %d\n", ret);
			return;
		}

		phy_power_on(lvds->phy);
	}

	if (lvds->panel)
		drm_panel_enable(lvds->panel);
}

static void rockchip_lvds_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (lvds->panel)
		drm_panel_disable(lvds->panel);

	if (lvds->phy)
		phy_power_off(lvds->phy);

	if (lvds->soc_data->power_off)
		lvds->soc_data->power_off(lvds);

	if (lvds->panel)
		drm_panel_unprepare(lvds->panel);

	pm_runtime_put(lvds->dev);
	clk_disable_unprepare(lvds->pclk);
}

static int rockchip_lvds_encoder_loader_protect(struct drm_encoder *encoder,
						bool on)
{
	struct rockchip_lvds *lvds = encoder_to_lvds(encoder);

	if (on)
		pm_runtime_get_sync(lvds->dev);
	else
		pm_runtime_put(lvds->dev);

	return 0;
}

static const
struct drm_encoder_helper_funcs rockchip_lvds_encoder_helper_funcs = {
	.mode_set = rockchip_lvds_encoder_mode_set,
	.enable = rockchip_lvds_encoder_enable,
	.disable = rockchip_lvds_encoder_disable,
	.atomic_check = rockchip_lvds_encoder_atomic_check,
	.loader_protect = rockchip_lvds_encoder_loader_protect,
};

static void rockchip_lvds_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs rockchip_lvds_encoder_funcs = {
	.destroy = rockchip_lvds_encoder_destroy,
};

static int rockchip_lvds_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct device_node *remote = NULL;
	struct device_node  *port, *endpoint;
	int ret, i;
	const char *name;

	port = of_graph_get_port_by_id(dev->of_node, 1);
	if (!port) {
		dev_err(dev, "can't found port point, please init lvds panel port!\n");
		return -EINVAL;
	}

	for_each_child_of_node(port, endpoint) {
		remote = of_graph_get_remote_port_parent(endpoint);
		if (!remote) {
			dev_err(dev, "can't found panel node, please init!\n");
			ret = -EINVAL;
			goto err_put_port;
		}
		if (!of_device_is_available(remote)) {
			of_node_put(remote);
			remote = NULL;
			continue;
		}
		break;
	}
	if (!remote) {
		dev_err(dev, "can't found remote node, please init!\n");
		ret = -EINVAL;
		goto err_put_port;
	}

	lvds->panel = of_drm_find_panel(remote);
	if (!lvds->panel)
		lvds->bridge = of_drm_find_bridge(remote);

	if (!lvds->panel && !lvds->bridge) {
		DRM_ERROR("failed to find panel and bridge node\n");
		ret  = -EPROBE_DEFER;
		goto err_put_remote;
	}

	if (of_property_read_string(remote, "rockchip,output", &name))
		/* default set it as output rgb */
		lvds->output = DISPLAY_OUTPUT_RGB;
	else
		lvds->output = lvds_name_to_output(name);

	if (lvds->output < 0) {
		dev_err(dev, "invalid output type [%s]\n", name);
		ret = lvds->output;
		goto err_put_remote;
	}

	if (of_property_read_string(remote, "rockchip,data-mapping",
				    &name))
		/* default set it as format jeida */
		lvds->format = LVDS_FORMAT_JEIDA;
	else
		lvds->format = lvds_name_to_format(name);

	if (lvds->format < 0) {
		dev_err(dev, "invalid data-mapping format [%s]\n", name);
		ret = lvds->format;
		goto err_put_remote;
	}

	if (of_property_read_u32(remote, "rockchip,data-width", &i)) {
		lvds->format |= LVDS_24BIT;
	} else {
		if (i == 24) {
			lvds->format |= LVDS_24BIT;
		} else if (i == 18) {
			lvds->format |= LVDS_18BIT;
		} else {
			dev_err(dev,
				"rockchip-lvds unsupport data-width[%d]\n", i);
			ret = -EINVAL;
			goto err_put_remote;
		}
	}

	encoder = &lvds->encoder;
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev,
							     dev->of_node);

	ret = drm_encoder_init(drm_dev, encoder, &rockchip_lvds_encoder_funcs,
			       (lvds->output == DISPLAY_OUTPUT_RGB) ?
			       DRM_MODE_ENCODER_DPI : DRM_MODE_ENCODER_LVDS,
			       NULL);
	if (ret < 0) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		goto err_put_remote;
	}

	drm_encoder_helper_add(encoder, &rockchip_lvds_encoder_helper_funcs);
	encoder->port = dev->of_node;

	if (lvds->panel) {
		connector = &lvds->connector;
		ret = drm_connector_init(drm_dev, connector,
					 &rockchip_lvds_connector_funcs,
					 (lvds->output == DISPLAY_OUTPUT_RGB) ?
					 DRM_MODE_CONNECTOR_DPI :
					 DRM_MODE_CONNECTOR_LVDS);
		if (ret < 0) {
			DRM_ERROR("failed to initialize connector with drm\n");
			goto err_free_encoder;
		}

		drm_connector_helper_add(connector,
					 &rockchip_lvds_connector_helper_funcs);

		ret = drm_mode_connector_attach_encoder(connector, encoder);
		if (ret < 0) {
			DRM_ERROR("failed to attach connector and encoder\n");
			goto err_free_connector;
		}

		ret = drm_panel_attach(lvds->panel, connector);
		if (ret < 0) {
			DRM_ERROR("failed to attach connector and encoder\n");
			goto err_free_connector;
		}
		lvds->connector.port = dev->of_node;
	} else {
		lvds->bridge->encoder = encoder;
		ret = drm_bridge_attach(drm_dev, lvds->bridge);
		if (ret) {
			DRM_ERROR("Failed to attach bridge to drm\n");
			goto err_free_encoder;
		}
		encoder->bridge = lvds->bridge;
	}

	pm_runtime_enable(dev);
	of_node_put(remote);
	of_node_put(port);

	return 0;

err_free_connector:
	drm_connector_cleanup(connector);
err_free_encoder:
	drm_encoder_cleanup(encoder);
err_put_remote:
	of_node_put(remote);
err_put_port:
	of_node_put(port);

	return ret;
}

static void rockchip_lvds_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct rockchip_lvds *lvds = dev_get_drvdata(dev);

	drm_panel_detach(lvds->panel);

	drm_connector_cleanup(&lvds->connector);
	drm_encoder_cleanup(&lvds->encoder);

	pm_runtime_disable(dev);
}

static const struct component_ops rockchip_lvds_component_ops = {
	.bind = rockchip_lvds_bind,
	.unbind = rockchip_lvds_unbind,
};

static int rockchip_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_lvds *lvds;
	struct resource *res;
	int ret;

	lvds = devm_kzalloc(dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;
	lvds->soc_data = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, lvds);

	lvds->phy = devm_phy_optional_get(dev, "phy");
	if (IS_ERR(lvds->phy)) {
		ret = PTR_ERR(lvds->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	if (lvds->phy) {
		lvds->grf = syscon_node_to_regmap(dev->parent->of_node);
		if (IS_ERR(lvds->grf)) {
			ret = PTR_ERR(lvds->grf);
			dev_err(dev, "Unable to get grf: %d\n", ret);
			return ret;
		}
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		lvds->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(lvds->regs))
			return PTR_ERR(lvds->regs);

		lvds->pclk = devm_clk_get(dev, "pclk_lvds");
		if (IS_ERR(lvds->pclk)) {
			dev_err(dev, "could not get pclk_lvds\n");
			return PTR_ERR(lvds->pclk);
		}

		lvds->grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							    "rockchip,grf");
		if (IS_ERR(lvds->grf)) {
			dev_err(dev, "missing rockchip,grf property\n");
			return PTR_ERR(lvds->grf);
		}

		if (lvds->soc_data->probe) {
			ret = lvds->soc_data->probe(lvds);
			if (ret)
				return ret;
		}
	}

	return component_add(dev, &rockchip_lvds_component_ops);
}

static int rockchip_lvds_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &rockchip_lvds_component_ops);

	return 0;
}

static int px30_lvds_power_on(struct rockchip_lvds *lvds)
{
	int pipe = drm_of_encoder_active_endpoint_id(lvds->dev->of_node,
						     &lvds->encoder);

	regmap_write(lvds->grf, PX30_GRF_PD_VO_CON1,
		     PX30_LVDS_SELECT(lvds->format) |
		     PX30_LVDS_MODE_EN(1) | PX30_LVDS_MSBSEL(1) |
		     PX30_LVDS_P2S_EN(1) | PX30_LVDS_VOP_SEL(pipe));

	return 0;
}

static void px30_lvds_power_off(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, PX30_GRF_PD_VO_CON1,
		     PX30_LVDS_MODE_EN(0) | PX30_LVDS_P2S_EN(0));
}

static const struct rockchip_lvds_soc_data px30_lvds_soc_data = {
	.chip_type = PX30,
	.power_on = px30_lvds_power_on,
	.power_off = px30_lvds_power_off,
};

static int rk3126_lvds_power_on(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3126_GRF_LVDS_CON0,
		     RK3126_LVDS_P2S_EN(1) | RK3126_LVDS_MODE_EN(1) |
		     RK3126_LVDS_MSBSEL(1) | RK3126_LVDS_SELECT(lvds->format));

	return 0;
}

static void rk3126_lvds_power_off(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3126_GRF_LVDS_CON0,
		     RK3126_LVDS_P2S_EN(0) | RK3126_LVDS_MODE_EN(0));
}

static const struct rockchip_lvds_soc_data rk3126_lvds_soc_data = {
	.chip_type = RK3126,
	.power_on = rk3126_lvds_power_on,
	.power_off = rk3126_lvds_power_off,
};

static int rk3288_lvds_power_on(struct rockchip_lvds *lvds)
{
	struct drm_display_mode *mode = &lvds->mode;
	u32 h_bp = mode->htotal - mode->hsync_start;
	u8 pin_hsync = (mode->flags & DRM_MODE_FLAG_PHSYNC) ? 1 : 0;
	u8 pin_dclk = (mode->flags & DRM_MODE_FLAG_PCSYNC) ? 1 : 0;
	u32 val;
	int pipe;

	pipe = drm_of_encoder_active_endpoint_id(lvds->dev->of_node,
						 &lvds->encoder);
	if (pipe)
		val = RK3288_LVDS_SOC_CON6_SEL_VOP_LIT |
		      (RK3288_LVDS_SOC_CON6_SEL_VOP_LIT << 16);
	else
		val = RK3288_LVDS_SOC_CON6_SEL_VOP_LIT << 16;
	regmap_write(lvds->grf, RK3288_GRF_SOC_CON6, val);

	val = lvds->format;
	if (lvds->output == DISPLAY_OUTPUT_DUAL_LVDS)
		val |= LVDS_DUAL | LVDS_CH0_EN | LVDS_CH1_EN;
	else if (lvds->output == DISPLAY_OUTPUT_LVDS)
		val |= LVDS_CH0_EN;
	else if (lvds->output == DISPLAY_OUTPUT_RGB)
		val |= LVDS_TTL_EN | LVDS_CH0_EN | LVDS_CH1_EN;

	if (h_bp & 0x01)
		val |= LVDS_START_PHASE_RST_1;

	val |= (pin_dclk << 8) | (pin_hsync << 9);
	val |= (0xffff << 16);
	regmap_write(lvds->grf, RK3288_GRF_SOC_CON7, val);

	return innov2_lvds_power_on(lvds);
}

static void rk3288_lvds_power_off(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3288_GRF_SOC_CON7, 0xffff8000);

	innov2_lvds_power_off(lvds);
}

static const struct rockchip_lvds_soc_data rk3288_lvds_soc_data = {
	.chip_type = RK3288,
	.power_on = rk3288_lvds_power_on,
	.power_off = rk3288_lvds_power_off,
};

static int rk3368_lvds_power_on(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3368_GRF_SOC_CON7,
		     RK3368_LVDS_SELECT(lvds->format) |
		     RK3368_LVDS_MODE_EN(1) | RK3368_LVDS_MSBSEL(1) |
		     RK3368_LVDS_P2S_EN(1));

	return 0;
}

static void rk3368_lvds_power_off(struct rockchip_lvds *lvds)
{
	regmap_write(lvds->grf, RK3368_GRF_SOC_CON7,
		     RK3368_LVDS_MODE_EN(0) | RK3368_LVDS_P2S_EN(0));
}

static const struct rockchip_lvds_soc_data rk3368_lvds_soc_data = {
	.chip_type = RK3368,
	.power_on = rk3368_lvds_power_on,
	.power_off = rk3368_lvds_power_off,
};

static const struct of_device_id rockchip_lvds_dt_ids[] = {
	{ .compatible = "rockchip,px30-lvds", .data = &px30_lvds_soc_data },
	{ .compatible = "rockchip,rk3126-lvds", .data = &rk3126_lvds_soc_data },
	{ .compatible = "rockchip,rk3288-lvds", .data = &rk3288_lvds_soc_data },
	{ .compatible = "rockchip,rk3368-lvds", .data = &rk3368_lvds_soc_data },
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
