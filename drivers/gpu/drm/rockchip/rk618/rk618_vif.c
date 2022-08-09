// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mfd/rk618.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include <drm/drm_of.h>
#include <drm/drm_drv.h>
#include <video/videomode.h>

#define RK618_VIF0_REG0			0x0000
#define VIF_ENABLE			HIWORD_UPDATE(1, 0, 0)
#define VIF_DISABLE			HIWORD_UPDATE(0, 0, 0)
#define RK618_VIF0_REG1			0x0004
#define VIF_FRAME_VST(x)		UPDATE(x, 27, 16)
#define VIF_FRAME_HST(x)		UPDATE(x, 11, 0)
#define RK618_VIF0_REG2			0x0008
#define VIF_HS_END(x)			UPDATE(x, 23, 16)
#define VIF_HTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_VIF0_REG3			0x000c
#define VIF_HACT_END(x)			UPDATE(x, 27, 16)
#define VIF_HACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_VIF0_REG4			0x0010
#define VIF_VS_END(x)			UPDATE(x, 23, 16)
#define VIF_VTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_VIF0_REG5			0x0014
#define VIF_VACT_END(x)			UPDATE(x, 27, 16)
#define VIF_VACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_VIF1_REG0			0x0018
#define RK618_VIF1_REG1			0x001c
#define RK618_VIF1_REG2			0x0020
#define RK618_VIF1_REG3			0x0024
#define RK618_VIF1_REG4			0x0028
#define RK618_VIF1_REG5			0x002c

struct rk618_vif {
	struct drm_bridge base;
	struct drm_bridge *bridge;
	struct drm_display_mode mode;
	struct device *dev;
	struct regmap *regmap;
	struct clk *vif_clk;
	struct clk *vif_pre_clk;
};

static inline struct rk618_vif *bridge_to_vif(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk618_vif, base);
}

static void rk618_vif_enable(struct rk618_vif *vif)
{
	regmap_write(vif->regmap, RK618_VIF0_REG0, VIF_ENABLE);
}

static void rk618_vif_disable(struct rk618_vif *vif)
{
	regmap_write(vif->regmap, RK618_VIF0_REG0, VIF_DISABLE);
}

static void rk618_vif_init(struct rk618_vif *vif,
			   const struct drm_display_mode *mode)
{
	struct videomode vm;
	u32 vif_frame_vst, vif_frame_hst;
	u32 vif_hs_end, vif_htotal, vif_hact_end, vif_hact_st;
	u32 vif_vs_end, vif_vtotal, vif_vact_end, vif_vact_st;

	drm_display_mode_to_videomode(mode, &vm);

	if (!strcmp(mode->name, "1920x1080")) {
		vif_frame_vst = 0x001;
		vif_frame_hst = 0x0cb;
	} else if (!strcmp(mode->name, "1600x900")) {
		vif_frame_vst = 0x001;
		vif_frame_hst = 0x327;
	} else if (!strcmp(mode->name, "1280x720")) {
		vif_frame_vst = 0x001;
		vif_frame_hst = 0x0cf;
	} else {
		vif_frame_vst = 0x001;
		vif_frame_hst = 0x001;
	}

	dev_dbg(vif->dev, "vif_frame_vst=%d, vif_frame_hst=%d\n",
		vif_frame_vst, vif_frame_hst);

	vif_hs_end = vm.hsync_len;
	vif_htotal = vm.hsync_len + vm.hback_porch + vm.hfront_porch +
		     vm.hactive;
	vif_hact_end = vm.hsync_len + vm.hback_porch + vm.hactive;
	vif_hact_st = vm.hsync_len + vm.hback_porch;
	vif_vs_end = vm.vsync_len;
	vif_vtotal = vm.vsync_len + vm.vback_porch + vm.vfront_porch +
		     vm.vactive;
	vif_vact_end = vm.vsync_len + vm.vback_porch + vm.vactive;
	vif_vact_st = vm.vsync_len + vm.vback_porch;

	regmap_write(vif->regmap, RK618_VIF0_REG1,
		     VIF_FRAME_VST(vif_frame_vst) |
		     VIF_FRAME_HST(vif_frame_hst));
	regmap_write(vif->regmap, RK618_VIF0_REG2,
		     VIF_HS_END(vif_hs_end) | VIF_HTOTAL(vif_htotal));
	regmap_write(vif->regmap, RK618_VIF0_REG3,
		     VIF_HACT_END(vif_hact_end) | VIF_HACT_ST(vif_hact_st));
	regmap_write(vif->regmap, RK618_VIF0_REG4,
		     VIF_VS_END(vif_vs_end) | VIF_VTOTAL(vif_vtotal));
	regmap_write(vif->regmap, RK618_VIF0_REG5,
		     VIF_VACT_END(vif_vact_end) | VIF_VACT_ST(vif_vact_st));
}

static void rk618_vif_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_vif *vif = bridge_to_vif(bridge);
	const struct drm_display_mode *mode = &vif->mode;
	long rate;

	clk_set_parent(vif->vif_clk, vif->vif_pre_clk);

	rate = clk_round_rate(vif->vif_clk, mode->clock * 1000);
	clk_set_rate(vif->vif_clk, rate);
	clk_prepare_enable(vif->vif_clk);

	rk618_vif_disable(vif);
	rk618_vif_init(vif, mode);
	rk618_vif_enable(vif);
}

static void rk618_vif_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_vif *vif = bridge_to_vif(bridge);

	rk618_vif_disable(vif);
	clk_disable_unprepare(vif->vif_clk);
}

static void rk618_vif_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adjusted)
{
	struct rk618_vif *vif = bridge_to_vif(bridge);

	drm_mode_copy(&vif->mode, adjusted);
}

static int rk618_vif_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct rk618_vif *vif = bridge_to_vif(bridge);
	struct device *dev = vif->dev;
	struct device_node *endpoint;
	int ret;

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 1, -1);
	if (endpoint && of_device_is_available(endpoint)) {
		struct device_node *remote;

		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!remote || !of_device_is_available(remote))
			return -ENODEV;

		vif->bridge = of_drm_find_bridge(remote);
		of_node_put(remote);
		if (!vif->bridge)
			return -EPROBE_DEFER;

		ret = drm_bridge_attach(bridge->encoder, vif->bridge, bridge, 0);
		if (ret) {
			dev_err(dev, "failed to attach bridge\n");
			return ret;
		}
	}

	return 0;
}

static const struct drm_bridge_funcs rk618_vif_bridge_funcs = {
	.enable = rk618_vif_bridge_enable,
	.disable = rk618_vif_bridge_disable,
	.mode_set = rk618_vif_bridge_mode_set,
	.attach = rk618_vif_bridge_attach,
};

static int rk618_vif_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_vif *vif;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	vif = devm_kzalloc(dev, sizeof(*vif), GFP_KERNEL);
	if (!vif)
		return -ENOMEM;

	vif->dev = dev;
	vif->regmap = rk618->regmap;
	platform_set_drvdata(pdev, vif);

	vif->vif_clk = devm_clk_get(dev, "vif");
	if (IS_ERR(vif->vif_clk)) {
		ret = PTR_ERR(vif->vif_clk);
		dev_err(dev, "failed to get vif clock: %d\n", ret);
		return ret;
	}

	vif->vif_pre_clk = devm_clk_get(dev, "vif_pre");
	if (IS_ERR(vif->vif_pre_clk)) {
		ret = PTR_ERR(vif->vif_pre_clk);
		dev_err(dev, "failed to get vif pre clock: %d\n", ret);
		return ret;
	}

	vif->base.funcs = &rk618_vif_bridge_funcs;
	vif->base.of_node = dev->of_node;
	drm_bridge_add(&vif->base);

	return 0;
}

static int rk618_vif_remove(struct platform_device *pdev)
{
	struct rk618_vif *vif = platform_get_drvdata(pdev);

	drm_bridge_remove(&vif->base);

	return 0;
}

static const struct of_device_id rk618_vif_of_match[] = {
	{ .compatible = "rockchip,rk618-vif", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_vif_of_match);

static struct platform_driver rk618_vif_driver = {
	.driver = {
		.name = "rk618-vif",
		.of_match_table = of_match_ptr(rk618_vif_of_match),
	},
	.probe = rk618_vif_probe,
	.remove = rk618_vif_remove,
};
module_platform_driver(rk618_vif_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 VIF driver");
MODULE_LICENSE("GPL v2");
