// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/rk628.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#include <video/of_display_timing.h>
#include <video/videomode.h>

enum interface_type {
	RGB_TX,
	YUV_RX,
	YUV_TX,
	BT1120_RX,
	BT1120_TX,
};

struct rk628_rgb {
	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_display_mode mode;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct device *dev;
	struct regmap *grf;
	struct rk628 *parent;
	struct clk *decclk;
	struct reset_control *rstc;
	bool dual_edge;
	enum interface_type interface_type;
};

static inline struct rk628_rgb *bridge_to_rgb(struct drm_bridge *b)
{
	return container_of(b, struct rk628_rgb, base);
}

static inline struct rk628_rgb *connector_to_rgb(struct drm_connector *c)
{
	return container_of(c, struct rk628_rgb, connector);
}

static enum interface_type rk628_rgb_get_interface_type(struct rk628_rgb *rgb)
{
	const struct device_node *of_node = rgb->dev->of_node;

	if (of_device_is_compatible(of_node, "rockchip,rk628-yuv-rx"))
		return YUV_RX;
	else if (of_device_is_compatible(of_node, "rockchip,rk628-yuv-tx"))
		return YUV_TX;
	else if (of_device_is_compatible(of_node, "rockchip,rk628-bt1120-rx"))
		return BT1120_RX;
	else if (of_device_is_compatible(of_node, "rockchip,rk628-bt1120-tx"))
		return BT1120_TX;
	else
		return RGB_TX;
}

static struct drm_encoder *
rk628_rgb_connector_best_encoder(struct drm_connector *connector)
{
	struct rk628_rgb *rgb = connector_to_rgb(connector);

	return rgb->base.encoder;
}

static int rk628_rgb_connector_get_modes(struct drm_connector *connector)
{
	struct rk628_rgb *rgb = connector_to_rgb(connector);

	return drm_panel_get_modes(rgb->panel);
}

static const struct drm_connector_helper_funcs
rk628_rgb_connector_helper_funcs = {
	.get_modes = rk628_rgb_connector_get_modes,
	.best_encoder = rk628_rgb_connector_best_encoder,
};

static void rk628_rgb_connector_destroy(struct drm_connector *connector)
{
	struct rk628_rgb *rgb = connector_to_rgb(connector);

	drm_panel_detach(rgb->panel);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs rk628_rgb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = rk628_rgb_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void rk628_bt1120_rx_enable(struct rk628_rgb *rgb)
{
	const struct drm_display_mode *mode = &rgb->mode;

	reset_control_assert(rgb->rstc);
	udelay(10);
	reset_control_deassert(rgb->rstc);
	udelay(10);

	clk_set_rate(rgb->decclk, mode->clock * 1000);
	clk_prepare_enable(rgb->decclk);

	if (rgb->dual_edge) {
		regmap_update_bits(rgb->grf, GRF_RGB_DEC_CON0,
				   DEC_DUALEDGE_EN, DEC_DUALEDGE_EN);

		regmap_write(rgb->grf,
			     GRF_BT1120_DCLK_DELAY_CON0, 0x10000000);
		regmap_write(rgb->grf, GRF_BT1120_DCLK_DELAY_CON1, 0);
	} else
		regmap_update_bits(rgb->grf, GRF_RGB_DEC_CON0,
				   DEC_DUALEDGE_EN, 0);

	regmap_update_bits(rgb->grf, GRF_RGB_DEC_CON1,
			   SW_SET_X_MASK, SW_SET_X(mode->hdisplay));
	regmap_update_bits(rgb->grf, GRF_RGB_DEC_CON2,
			   SW_SET_Y_MASK, SW_SET_Y(mode->vdisplay));

	regmap_update_bits(rgb->grf, GRF_SYSTEM_CON0,
			   SW_BT_DATA_OEN_MASK | SW_INPUT_MODE_MASK,
			   SW_BT_DATA_OEN | SW_INPUT_MODE(INPUT_MODE_BT1120));

	regmap_write(rgb->grf, GRF_CSC_CTRL_CON, SW_Y2R_EN(1));

	regmap_update_bits(rgb->grf, GRF_RGB_DEC_CON0,
			   SW_CAP_EN_PSYNC | SW_CAP_EN_ASYNC | SW_PROGRESS_EN,
			   SW_CAP_EN_PSYNC | SW_CAP_EN_ASYNC | SW_PROGRESS_EN);
}

static void rk628_bt1120_tx_enable(struct rk628_rgb *rgb)
{
	u32 val = 0;

	regmap_update_bits(rgb->grf, GRF_SYSTEM_CON0,
			   SW_BT_DATA_OEN_MASK | SW_OUTPUT_MODE_MASK,
			   SW_OUTPUT_MODE(OUTPUT_MODE_BT1120));
	regmap_write(rgb->grf, GRF_CSC_CTRL_CON, SW_R2Y_EN(1));
	regmap_update_bits(rgb->grf, GRF_POST_PROC_CON,
			   SW_DCLK_OUT_INV_EN, SW_DCLK_OUT_INV_EN);

	if (rgb->dual_edge) {
		val |= ENC_DUALEDGE_EN(1);
		regmap_write(rgb->grf, GRF_BT1120_DCLK_DELAY_CON0, 0x10000000);
		regmap_write(rgb->grf, GRF_BT1120_DCLK_DELAY_CON1, 0);
	}

	val |= BT1120_UV_SWAP(1);
	regmap_write(rgb->grf, GRF_RGB_ENC_CON, val);

}

static void rk628_rgb_bridge_enable(struct drm_bridge *bridge)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);

	switch (rgb->interface_type) {
	case YUV_RX:
		regmap_write(rgb->grf, GRF_CSC_CTRL_CON, SW_Y2R_EN(1));
		regmap_update_bits(rgb->grf, GRF_SYSTEM_CON0,
				   SW_BT_DATA_OEN_MASK | SW_INPUT_MODE_MASK,
				   SW_BT_DATA_OEN | SW_INPUT_MODE(INPUT_MODE_YUV));
		break;
	case YUV_TX:
		regmap_write(rgb->grf, GRF_CSC_CTRL_CON, SW_R2Y_EN(1));
		regmap_update_bits(rgb->grf, GRF_POST_PROC_CON,
				   SW_DCLK_OUT_INV_EN, SW_DCLK_OUT_INV_EN);

		regmap_update_bits(rgb->grf, GRF_SYSTEM_CON0,
				   SW_BT_DATA_OEN_MASK | SW_OUTPUT_MODE_MASK,
				   SW_OUTPUT_MODE(OUTPUT_MODE_YUV));
		break;
	case BT1120_RX:
		rk628_bt1120_rx_enable(rgb);
		break;
	case BT1120_TX:
		rk628_bt1120_tx_enable(rgb);
		break;
	case RGB_TX:
	default:
		regmap_update_bits(rgb->grf, GRF_SYSTEM_CON0,
				   SW_BT_DATA_OEN_MASK | SW_OUTPUT_MODE_MASK,
				   SW_OUTPUT_MODE(OUTPUT_MODE_RGB));
		regmap_update_bits(rgb->grf, GRF_POST_PROC_CON,
				   SW_DCLK_OUT_INV_EN, SW_DCLK_OUT_INV_EN);
		break;
	}

	if (rgb->panel) {
		drm_panel_prepare(rgb->panel);
		drm_panel_enable(rgb->panel);
	}
}

static void rk628_rgb_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);

	if (rgb->panel) {
		drm_panel_disable(rgb->panel);
		drm_panel_unprepare(rgb->panel);
	}

	if (rgb->decclk)
		clk_disable_unprepare(rgb->decclk);

	if (rgb->rstc)
		reset_control_assert(rgb->rstc);
}

static int rk628_rgb_bridge_attach(struct drm_bridge *bridge)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);
	struct drm_connector *connector = &rgb->connector;
	struct drm_device *drm = bridge->dev;
	struct device *dev = rgb->dev;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  &rgb->panel, &rgb->bridge);
	if (ret)
		return ret;

	if (rgb->interface_type == YUV_RX || rgb->interface_type == BT1120_RX) {
		if (!rgb->bridge) {
			dev_err(dev, "decoder failed to find bridge\n");
			return -EPROBE_DEFER;
		}

		rgb->bridge->encoder = bridge->encoder;
		ret = drm_bridge_attach(bridge->encoder, rgb->bridge, bridge);
		if (ret) {
			dev_err(dev, "failed to attach bridge\n");
			return ret;
		}

		bridge->next = rgb->bridge;
	} else {
		if (rgb->bridge) {
			rgb->bridge->encoder = bridge->encoder;
			ret = drm_bridge_attach(bridge->encoder, rgb->bridge, bridge);
			if (ret) {
				dev_err(dev, "failed to attach bridge\n");
				return ret;
			}

			bridge->next = rgb->bridge;
		}

		if (rgb->panel) {
			ret = drm_connector_init(drm, connector,
						 &rk628_rgb_connector_funcs,
						 DRM_MODE_CONNECTOR_DPI);
			if (ret) {
				dev_err(dev,
					"Failed to initialize connector with drm\n");
				return ret;
			}

			drm_connector_helper_add(connector,
						 &rk628_rgb_connector_helper_funcs);
			drm_connector_attach_encoder(connector,
							  bridge->encoder);
			ret = drm_panel_attach(rgb->panel, connector);
			if (ret) {
				dev_err(dev, "Failed to attach panel\n");
				return ret;
			}
		}
	}

	return 0;
}

static void rk628_rgb_bridge_mode_set(struct drm_bridge *bridge,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adj)
{
	struct rk628_rgb *rgb = bridge_to_rgb(bridge);

	drm_mode_copy(&rgb->mode, adj);
}

static const struct drm_bridge_funcs rk628_rgb_bridge_funcs = {
	.attach = rk628_rgb_bridge_attach,
	.enable = rk628_rgb_bridge_enable,
	.disable = rk628_rgb_bridge_disable,
	.mode_set = rk628_rgb_bridge_mode_set,
};

static int rk628_rgb_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk628_rgb *rgb;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	rgb = devm_kzalloc(dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->dev = dev;
	rgb->parent = rk628;
	rgb->grf = rk628->grf;
	rgb->interface_type = rk628_rgb_get_interface_type(rgb);
	rgb->dual_edge = of_property_read_bool(dev->of_node, "dual-edge");
	platform_set_drvdata(pdev, rgb);

	if (rgb->interface_type == BT1120_RX) {
		rgb->decclk = devm_clk_get(dev, "bt1120dec");
		if (IS_ERR(rgb->decclk)) {
			ret = PTR_ERR(rgb->decclk);
			dev_err(dev, "failed to get dec clk: %d\n", ret);
			return ret;
		}

		rgb->rstc = of_reset_control_get(dev->of_node, NULL);
		if (IS_ERR(rgb->rstc)) {
			ret = PTR_ERR(rgb->rstc);
			dev_err(dev, "failed to get reset control: %d\n", ret);
			return ret;
		}
	}

	rgb->base.funcs = &rk628_rgb_bridge_funcs;
	rgb->base.of_node = dev->of_node;
	drm_bridge_add(&rgb->base);

	return 0;
}

static int rk628_rgb_remove(struct platform_device *pdev)
{
	struct rk628_rgb *rgb = platform_get_drvdata(pdev);

	drm_bridge_remove(&rgb->base);

	return 0;
}

static const struct of_device_id rk628_rgb_of_match[] = {
	{ .compatible = "rockchip,rk628-rgb-tx", },
	{ .compatible = "rockchip,rk628-yuv-rx", },
	{ .compatible = "rockchip,rk628-yuv-tx", },
	{ .compatible = "rockchip,rk628-bt1120-rx", },
	{ .compatible = "rockchip,rk628-bt1120-tx", },
	{},
};
MODULE_DEVICE_TABLE(of, rk628_rgb_of_match);

static struct platform_driver rk628_rgb_driver = {
	.driver = {
		.name = "rk628-rgb",
		.of_match_table = of_match_ptr(rk628_rgb_of_match),
	},
	.probe = rk628_rgb_probe,
	.remove = rk628_rgb_remove,
};
module_platform_driver(rk628_rgb_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 RGB driver");
MODULE_LICENSE("GPL v2");
