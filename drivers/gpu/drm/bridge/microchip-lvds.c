// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Manikandan Muralidharan <manikandan.m@microchip.com>
 * Author: Dharma Balasubiramani <dharma.b@microchip.com>
 *
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mfd/syscon.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/devinfo.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define LVDS_POLL_TIMEOUT_MS 1000

/* LVDSC register offsets */
#define LVDSC_CR	0x00
#define LVDSC_CFGR	0x04
#define LVDSC_SR	0x0C
#define LVDSC_WPMR	0xE4

/* Bitfields in LVDSC_CR (Control Register) */
#define LVDSC_CR_SER_EN	BIT(0)

/* Bitfields in LVDSC_CFGR (Configuration Register) */
#define LVDSC_CFGR_PIXSIZE_24BITS	0
#define LVDSC_CFGR_DEN_POL_HIGH		0
#define LVDSC_CFGR_DC_UNBALANCED	0
#define LVDSC_CFGR_MAPPING_JEIDA	BIT(6)

/*Bitfields in LVDSC_SR */
#define LVDSC_SR_CS	BIT(0)

/* Bitfields in LVDSC_WPMR (Write Protection Mode Register) */
#define LVDSC_WPMR_WPKEY_MASK	GENMASK(31, 8)
#define LVDSC_WPMR_WPKEY_PSSWD	0x4C5644

struct mchp_lvds {
	struct device *dev;
	void __iomem *regs;
	struct clk *pclk;
	struct drm_panel *panel;
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
};

static inline struct mchp_lvds *bridge_to_lvds(struct drm_bridge *bridge)
{
	return container_of(bridge, struct mchp_lvds, bridge);
}

static inline u32 lvds_readl(struct mchp_lvds *lvds, u32 offset)
{
	return readl_relaxed(lvds->regs + offset);
}

static inline void lvds_writel(struct mchp_lvds *lvds, u32 offset, u32 val)
{
	writel_relaxed(val, lvds->regs + offset);
}

static void lvds_serialiser_on(struct mchp_lvds *lvds)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(LVDS_POLL_TIMEOUT_MS);

	/* The LVDSC registers can only be written if WPEN is cleared */
	lvds_writel(lvds, LVDSC_WPMR, (LVDSC_WPMR_WPKEY_PSSWD &
				LVDSC_WPMR_WPKEY_MASK));

	/* Wait for the status of configuration registers to be changed */
	while (lvds_readl(lvds, LVDSC_SR) & LVDSC_SR_CS) {
		if (time_after(jiffies, timeout)) {
			dev_err(lvds->dev, "%s: timeout error\n", __func__);
			return;
		}
		usleep_range(1000, 2000);
	}

	/* Configure the LVDSC */
	lvds_writel(lvds, LVDSC_CFGR, (LVDSC_CFGR_MAPPING_JEIDA |
				LVDSC_CFGR_DC_UNBALANCED |
				LVDSC_CFGR_DEN_POL_HIGH |
				LVDSC_CFGR_PIXSIZE_24BITS));

	/* Enable the LVDS serializer */
	lvds_writel(lvds, LVDSC_CR, LVDSC_CR_SER_EN);
}

static int mchp_lvds_attach(struct drm_bridge *bridge,
			    enum drm_bridge_attach_flags flags)
{
	struct mchp_lvds *lvds = bridge_to_lvds(bridge);

	return drm_bridge_attach(bridge->encoder, lvds->panel_bridge,
				 bridge, flags);
}

static void mchp_lvds_enable(struct drm_bridge *bridge)
{
	struct mchp_lvds *lvds = bridge_to_lvds(bridge);
	int ret;

	ret = clk_prepare_enable(lvds->pclk);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to enable lvds pclk %d\n", ret);
		return;
	}

	ret = pm_runtime_get_sync(lvds->dev);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to get pm runtime: %d\n", ret);
		return;
	}

	lvds_serialiser_on(lvds);
}

static void mchp_lvds_disable(struct drm_bridge *bridge)
{
	struct mchp_lvds *lvds = bridge_to_lvds(bridge);

	pm_runtime_put(lvds->dev);
	clk_disable_unprepare(lvds->pclk);
}

static const struct drm_bridge_funcs mchp_lvds_bridge_funcs = {
	.attach = mchp_lvds_attach,
	.enable = mchp_lvds_enable,
	.disable = mchp_lvds_disable,
};

static int mchp_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mchp_lvds *lvds;
	struct device_node *port;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = dev;

	lvds->regs = devm_ioremap_resource(lvds->dev,
			platform_get_resource(pdev, IORESOURCE_MEM, 0));
	if (IS_ERR(lvds->regs))
		return PTR_ERR(lvds->regs);

	lvds->pclk = devm_clk_get(lvds->dev, "pclk");
	if (IS_ERR(lvds->pclk))
		return dev_err_probe(lvds->dev, PTR_ERR(lvds->pclk),
				"could not get pclk_lvds\n");

	port = of_graph_get_remote_node(dev->of_node, 1, 0);
	if (!port) {
		dev_err(dev,
			"can't find port point, please init lvds panel port!\n");
		return -ENODEV;
	}

	lvds->panel = of_drm_find_panel(port);
	of_node_put(port);

	if (IS_ERR(lvds->panel))
		return -EPROBE_DEFER;

	lvds->panel_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);

	if (IS_ERR(lvds->panel_bridge))
		return PTR_ERR(lvds->panel_bridge);

	lvds->bridge.of_node = dev->of_node;
	lvds->bridge.type = DRM_MODE_CONNECTOR_LVDS;
	lvds->bridge.funcs = &mchp_lvds_bridge_funcs;

	dev_set_drvdata(dev, lvds);
	ret = devm_pm_runtime_enable(dev);
	if (ret < 0) {
		dev_err(lvds->dev, "failed to enable pm runtime: %d\n", ret);
		return ret;
	}

	drm_bridge_add(&lvds->bridge);

	return 0;
}

static const struct of_device_id mchp_lvds_dt_ids[] = {
	{
		.compatible = "microchip,sam9x75-lvds",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mchp_lvds_dt_ids);

static struct platform_driver mchp_lvds_driver = {
	.probe = mchp_lvds_probe,
	.driver = {
		   .name = "microchip-lvds",
		   .of_match_table = mchp_lvds_dt_ids,
	},
};
module_platform_driver(mchp_lvds_driver);

MODULE_AUTHOR("Manikandan Muralidharan <manikandan.m@microchip.com>");
MODULE_AUTHOR("Dharma Balasubiramani <dharma.b@microchip.com>");
MODULE_DESCRIPTION("Low Voltage Differential Signaling Controller Driver");
MODULE_LICENSE("GPL");
