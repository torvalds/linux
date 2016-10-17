/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drmP.h>

#include "sti_drv.h"

/* registers offset */
#define VTAC_CONFIG                     0x00
#define VTAC_RX_FIFO_CONFIG             0x04
#define VTAC_FIFO_CONFIG_VAL            0x04

#define VTAC_SYS_CFG8521                0x824
#define VTAC_SYS_CFG8522                0x828

/* Number of phyts per pixel */
#define VTAC_2_5_PPP                    0x0005
#define VTAC_3_PPP                      0x0006
#define VTAC_4_PPP                      0x0008
#define VTAC_5_PPP                      0x000A
#define VTAC_6_PPP                      0x000C
#define VTAC_13_PPP                     0x001A
#define VTAC_14_PPP                     0x001C
#define VTAC_15_PPP                     0x001E
#define VTAC_16_PPP                     0x0020
#define VTAC_17_PPP                     0x0022
#define VTAC_18_PPP                     0x0024

/* enable bits */
#define VTAC_ENABLE                     0x3003

#define VTAC_TX_PHY_ENABLE_CLK_PHY      BIT(0)
#define VTAC_TX_PHY_ENABLE_CLK_DLL      BIT(1)
#define VTAC_TX_PHY_PLL_NOT_OSC_MODE    BIT(3)
#define VTAC_TX_PHY_RST_N_DLL_SWITCH    BIT(4)
#define VTAC_TX_PHY_PROG_N3             BIT(9)


/**
 * VTAC mode structure
 *
 * @vid_in_width: Video Data Resolution
 * @phyts_width: Width of phyt buses(phyt low and phyt high).
 * @phyts_per_pixel: Number of phyts sent per pixel
 */
struct sti_vtac_mode {
	u32 vid_in_width;
	u32 phyts_width;
	u32 phyts_per_pixel;
};

static const struct sti_vtac_mode vtac_mode_main = {
	.vid_in_width = 0x2,
	.phyts_width = 0x2,
	.phyts_per_pixel = VTAC_5_PPP,
};
static const struct sti_vtac_mode vtac_mode_aux = {
	.vid_in_width = 0x1,
	.phyts_width = 0x0,
	.phyts_per_pixel = VTAC_17_PPP,
};

/**
 * VTAC structure
 *
 * @dev: pointer to device structure
 * @regs: ioremapped registers for RX and TX devices
 * @phy_regs: phy registers for TX device
 * @clk: clock
 * @mode: main or auxillary configuration mode
 */
struct sti_vtac {
	struct device *dev;
	void __iomem *regs;
	void __iomem *phy_regs;
	struct clk *clk;
	const struct sti_vtac_mode *mode;
};

static void sti_vtac_rx_set_config(struct sti_vtac *vtac)
{
	u32 config;

	/* Enable VTAC clock */
	if (clk_prepare_enable(vtac->clk))
		DRM_ERROR("Failed to prepare/enable vtac_rx clock.\n");

	writel(VTAC_FIFO_CONFIG_VAL, vtac->regs + VTAC_RX_FIFO_CONFIG);

	config = VTAC_ENABLE;
	config |= vtac->mode->vid_in_width << 4;
	config |= vtac->mode->phyts_width << 16;
	config |= vtac->mode->phyts_per_pixel << 23;
	writel(config, vtac->regs + VTAC_CONFIG);
}

static void sti_vtac_tx_set_config(struct sti_vtac *vtac)
{
	u32 phy_config;
	u32 config;

	/* Enable VTAC clock */
	if (clk_prepare_enable(vtac->clk))
		DRM_ERROR("Failed to prepare/enable vtac_tx clock.\n");

	/* Configure vtac phy */
	phy_config = 0x00000000;
	writel(phy_config, vtac->phy_regs + VTAC_SYS_CFG8522);
	phy_config = VTAC_TX_PHY_ENABLE_CLK_PHY;
	writel(phy_config, vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config = readl(vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config |= VTAC_TX_PHY_PROG_N3;
	writel(phy_config, vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config = readl(vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config |= VTAC_TX_PHY_ENABLE_CLK_DLL;
	writel(phy_config, vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config = readl(vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config |= VTAC_TX_PHY_RST_N_DLL_SWITCH;
	writel(phy_config, vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config = readl(vtac->phy_regs + VTAC_SYS_CFG8521);
	phy_config |= VTAC_TX_PHY_PLL_NOT_OSC_MODE;
	writel(phy_config, vtac->phy_regs + VTAC_SYS_CFG8521);

	/* Configure vtac tx */
	config = VTAC_ENABLE;
	config |= vtac->mode->vid_in_width << 4;
	config |= vtac->mode->phyts_width << 16;
	config |= vtac->mode->phyts_per_pixel << 23;
	writel(config, vtac->regs + VTAC_CONFIG);
}

static const struct of_device_id vtac_of_match[] = {
	{
		.compatible = "st,vtac-main",
		.data = &vtac_mode_main,
	}, {
		.compatible = "st,vtac-aux",
		.data = &vtac_mode_aux,
	}, {
		/* end node */
	}
};
MODULE_DEVICE_TABLE(of, vtac_of_match);

static int sti_vtac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct of_device_id *id;
	struct sti_vtac *vtac;
	struct resource *res;

	vtac = devm_kzalloc(dev, sizeof(*vtac), GFP_KERNEL);
	if (!vtac)
		return -ENOMEM;

	vtac->dev = dev;

	id = of_match_node(vtac_of_match, np);
	if (!id)
		return -ENOMEM;

	vtac->mode = id->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		DRM_ERROR("Invalid resource\n");
		return -ENOMEM;
	}
	vtac->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vtac->regs))
		return PTR_ERR(vtac->regs);


	vtac->clk = devm_clk_get(dev, "vtac");
	if (IS_ERR(vtac->clk)) {
		DRM_ERROR("Cannot get vtac clock\n");
		return PTR_ERR(vtac->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		vtac->phy_regs = devm_ioremap_nocache(dev, res->start,
						 resource_size(res));
		sti_vtac_tx_set_config(vtac);
	} else {

		sti_vtac_rx_set_config(vtac);
	}

	platform_set_drvdata(pdev, vtac);
	DRM_INFO("%s %s\n", __func__, dev_name(vtac->dev));

	return 0;
}

static int sti_vtac_remove(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver sti_vtac_driver = {
	.driver = {
		.name = "sti-vtac",
		.owner = THIS_MODULE,
		.of_match_table = vtac_of_match,
	},
	.probe = sti_vtac_probe,
	.remove = sti_vtac_remove,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
