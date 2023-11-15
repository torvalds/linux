// SPDX-License-Identifier: GPL-2.0
/*
 * dwmac-imx.c - DWMAC Specific Glue layer for NXP imx8
 *
 * Copyright 2020 NXP
 *
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define GPR_ENET_QOS_INTF_MODE_MASK	GENMASK(21, 16)
#define GPR_ENET_QOS_INTF_SEL_MII	(0x0 << 16)
#define GPR_ENET_QOS_INTF_SEL_RMII	(0x4 << 16)
#define GPR_ENET_QOS_INTF_SEL_RGMII	(0x1 << 16)
#define GPR_ENET_QOS_CLK_GEN_EN		(0x1 << 19)
#define GPR_ENET_QOS_CLK_TX_CLK_SEL	(0x1 << 20)
#define GPR_ENET_QOS_RGMII_EN		(0x1 << 21)

#define MX93_GPR_ENET_QOS_INTF_MODE_MASK	GENMASK(3, 0)
#define MX93_GPR_ENET_QOS_INTF_MASK		GENMASK(3, 1)
#define MX93_GPR_ENET_QOS_INTF_SEL_MII		(0x0 << 1)
#define MX93_GPR_ENET_QOS_INTF_SEL_RMII		(0x4 << 1)
#define MX93_GPR_ENET_QOS_INTF_SEL_RGMII	(0x1 << 1)
#define MX93_GPR_ENET_QOS_CLK_GEN_EN		(0x1 << 0)

#define DMA_BUS_MODE			0x00001000
#define DMA_BUS_MODE_SFT_RESET		(0x1 << 0)
#define RMII_RESET_SPEED		(0x3 << 14)
#define CTRL_SPEED_MASK			GENMASK(15, 14)

struct imx_dwmac_ops {
	u32 addr_width;
	u32 flags;
	bool mac_rgmii_txclk_auto_adj;

	int (*fix_soc_reset)(void *priv, void __iomem *ioaddr);
	int (*set_intf_mode)(struct plat_stmmacenet_data *plat_dat);
	void (*fix_mac_speed)(void *priv, unsigned int speed, unsigned int mode);
};

struct imx_priv_data {
	struct device *dev;
	struct clk *clk_tx;
	struct clk *clk_mem;
	struct regmap *intf_regmap;
	u32 intf_reg_off;
	bool rmii_refclk_ext;
	void __iomem *base_addr;

	const struct imx_dwmac_ops *ops;
	struct plat_stmmacenet_data *plat_dat;
};

static int imx8mp_set_intf_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct imx_priv_data *dwmac = plat_dat->bsp_priv;
	int val;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		val = GPR_ENET_QOS_INTF_SEL_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = GPR_ENET_QOS_INTF_SEL_RMII;
		val |= (dwmac->rmii_refclk_ext ? 0 : GPR_ENET_QOS_CLK_TX_CLK_SEL);
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = GPR_ENET_QOS_INTF_SEL_RGMII |
		      GPR_ENET_QOS_RGMII_EN;
		break;
	default:
		pr_debug("imx dwmac doesn't support %d interface\n",
			 plat_dat->mac_interface);
		return -EINVAL;
	}

	val |= GPR_ENET_QOS_CLK_GEN_EN;
	return regmap_update_bits(dwmac->intf_regmap, dwmac->intf_reg_off,
				  GPR_ENET_QOS_INTF_MODE_MASK, val);
};

static int
imx8dxl_set_intf_mode(struct plat_stmmacenet_data *plat_dat)
{
	int ret = 0;

	/* TBD: depends on imx8dxl scu interfaces to be upstreamed */
	return ret;
}

static int imx93_set_intf_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct imx_priv_data *dwmac = plat_dat->bsp_priv;
	int val;

	switch (plat_dat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		val = MX93_GPR_ENET_QOS_INTF_SEL_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = MX93_GPR_ENET_QOS_INTF_SEL_RMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = MX93_GPR_ENET_QOS_INTF_SEL_RGMII;
		break;
	default:
		dev_dbg(dwmac->dev, "imx dwmac doesn't support %d interface\n",
			 plat_dat->mac_interface);
		return -EINVAL;
	}

	val |= MX93_GPR_ENET_QOS_CLK_GEN_EN;
	return regmap_update_bits(dwmac->intf_regmap, dwmac->intf_reg_off,
				  MX93_GPR_ENET_QOS_INTF_MODE_MASK, val);
};

static int imx_dwmac_clks_config(void *priv, bool enabled)
{
	struct imx_priv_data *dwmac = priv;
	int ret = 0;

	if (enabled) {
		ret = clk_prepare_enable(dwmac->clk_mem);
		if (ret) {
			dev_err(dwmac->dev, "mem clock enable failed\n");
			return ret;
		}

		ret = clk_prepare_enable(dwmac->clk_tx);
		if (ret) {
			dev_err(dwmac->dev, "tx clock enable failed\n");
			clk_disable_unprepare(dwmac->clk_mem);
			return ret;
		}
	} else {
		clk_disable_unprepare(dwmac->clk_tx);
		clk_disable_unprepare(dwmac->clk_mem);
	}

	return ret;
}

static int imx_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct plat_stmmacenet_data *plat_dat;
	struct imx_priv_data *dwmac = priv;
	int ret;

	plat_dat = dwmac->plat_dat;

	if (dwmac->ops->set_intf_mode) {
		ret = dwmac->ops->set_intf_mode(plat_dat);
		if (ret)
			return ret;
	}

	return 0;
}

static void imx_dwmac_exit(struct platform_device *pdev, void *priv)
{
	/* nothing to do now */
}

static void imx_dwmac_fix_speed(void *priv, unsigned int speed, unsigned int mode)
{
	struct plat_stmmacenet_data *plat_dat;
	struct imx_priv_data *dwmac = priv;
	unsigned long rate;
	int err;

	plat_dat = dwmac->plat_dat;

	if (dwmac->ops->mac_rgmii_txclk_auto_adj ||
	    (plat_dat->mac_interface == PHY_INTERFACE_MODE_RMII) ||
	    (plat_dat->mac_interface == PHY_INTERFACE_MODE_MII))
		return;

	switch (speed) {
	case SPEED_1000:
		rate = 125000000;
		break;
	case SPEED_100:
		rate = 25000000;
		break;
	case SPEED_10:
		rate = 2500000;
		break;
	default:
		dev_err(dwmac->dev, "invalid speed %u\n", speed);
		return;
	}

	err = clk_set_rate(dwmac->clk_tx, rate);
	if (err < 0)
		dev_err(dwmac->dev, "failed to set tx rate %lu\n", rate);
}

static void imx93_dwmac_fix_speed(void *priv, unsigned int speed, unsigned int mode)
{
	struct imx_priv_data *dwmac = priv;
	unsigned int iface;
	int ctrl, old_ctrl;

	imx_dwmac_fix_speed(priv, speed, mode);

	if (!dwmac || mode != MLO_AN_FIXED)
		return;

	if (regmap_read(dwmac->intf_regmap, dwmac->intf_reg_off, &iface))
		return;

	iface &= MX93_GPR_ENET_QOS_INTF_MASK;
	if (iface != MX93_GPR_ENET_QOS_INTF_SEL_RGMII)
		return;

	old_ctrl = readl(dwmac->base_addr + MAC_CTRL_REG);
	ctrl = old_ctrl & ~CTRL_SPEED_MASK;
	regmap_update_bits(dwmac->intf_regmap, dwmac->intf_reg_off,
			   MX93_GPR_ENET_QOS_INTF_MODE_MASK, 0);
	writel(ctrl, dwmac->base_addr + MAC_CTRL_REG);

	 /* Ensure the settings for CTRL are applied. */
	readl(dwmac->base_addr + MAC_CTRL_REG);

	usleep_range(10, 20);
	iface |= MX93_GPR_ENET_QOS_CLK_GEN_EN;
	regmap_update_bits(dwmac->intf_regmap, dwmac->intf_reg_off,
			   MX93_GPR_ENET_QOS_INTF_MODE_MASK, iface);

	writel(old_ctrl, dwmac->base_addr + MAC_CTRL_REG);
}

static int imx_dwmac_mx93_reset(void *priv, void __iomem *ioaddr)
{
	struct plat_stmmacenet_data *plat_dat = priv;
	u32 value = readl(ioaddr + DMA_BUS_MODE);

	/* DMA SW reset */
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);

	if (plat_dat->mac_interface == PHY_INTERFACE_MODE_RMII) {
		usleep_range(100, 200);
		writel(RMII_RESET_SPEED, ioaddr + MAC_CTRL_REG);
	}

	return readl_poll_timeout(ioaddr + DMA_BUS_MODE, value,
				 !(value & DMA_BUS_MODE_SFT_RESET),
				 10000, 1000000);
}

static int
imx_dwmac_parse_dt(struct imx_priv_data *dwmac, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int err = 0;

	dwmac->rmii_refclk_ext = of_property_read_bool(np, "snps,rmii_refclk_ext");

	dwmac->clk_tx = devm_clk_get(dev, "tx");
	if (IS_ERR(dwmac->clk_tx)) {
		dev_err(dev, "failed to get tx clock\n");
		return PTR_ERR(dwmac->clk_tx);
	}

	dwmac->clk_mem = NULL;

	if (of_machine_is_compatible("fsl,imx8dxl") ||
	    of_machine_is_compatible("fsl,imx93")) {
		dwmac->clk_mem = devm_clk_get(dev, "mem");
		if (IS_ERR(dwmac->clk_mem)) {
			dev_err(dev, "failed to get mem clock\n");
			return PTR_ERR(dwmac->clk_mem);
		}
	}

	if (of_machine_is_compatible("fsl,imx8mp") ||
	    of_machine_is_compatible("fsl,imx93")) {
		/* Binding doc describes the propety:
		 * is required by i.MX8MP, i.MX93.
		 * is optinoal for i.MX8DXL.
		 */
		dwmac->intf_regmap = syscon_regmap_lookup_by_phandle(np, "intf_mode");
		if (IS_ERR(dwmac->intf_regmap))
			return PTR_ERR(dwmac->intf_regmap);

		err = of_property_read_u32_index(np, "intf_mode", 1, &dwmac->intf_reg_off);
		if (err) {
			dev_err(dev, "Can't get intf mode reg offset (%d)\n", err);
			return err;
		}
	}

	return err;
}

static int imx_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct imx_priv_data *dwmac;
	const struct imx_dwmac_ops *data;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "failed to get match data\n");
		return -EINVAL;
	}

	dwmac->ops = data;
	dwmac->dev = &pdev->dev;

	ret = imx_dwmac_parse_dt(dwmac, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse OF data\n");
		return ret;
	}

	if (data->flags & STMMAC_FLAG_HWTSTAMP_CORRECT_LATENCY)
		plat_dat->flags |= STMMAC_FLAG_HWTSTAMP_CORRECT_LATENCY;

	plat_dat->host_dma_width = dwmac->ops->addr_width;
	plat_dat->init = imx_dwmac_init;
	plat_dat->exit = imx_dwmac_exit;
	plat_dat->clks_config = imx_dwmac_clks_config;
	plat_dat->fix_mac_speed = imx_dwmac_fix_speed;
	plat_dat->bsp_priv = dwmac;
	dwmac->plat_dat = plat_dat;
	dwmac->base_addr = stmmac_res.addr;

	ret = imx_dwmac_clks_config(dwmac, true);
	if (ret)
		return ret;

	ret = imx_dwmac_init(pdev, dwmac);
	if (ret)
		goto err_dwmac_init;

	if (dwmac->ops->fix_mac_speed)
		plat_dat->fix_mac_speed = dwmac->ops->fix_mac_speed;
	dwmac->plat_dat->fix_soc_reset = dwmac->ops->fix_soc_reset;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_drv_probe;

	return 0;

err_drv_probe:
	imx_dwmac_exit(pdev, plat_dat->bsp_priv);
err_dwmac_init:
	imx_dwmac_clks_config(dwmac, false);
	return ret;
}

static struct imx_dwmac_ops imx8mp_dwmac_data = {
	.addr_width = 34,
	.mac_rgmii_txclk_auto_adj = false,
	.set_intf_mode = imx8mp_set_intf_mode,
	.flags = STMMAC_FLAG_HWTSTAMP_CORRECT_LATENCY,
};

static struct imx_dwmac_ops imx8dxl_dwmac_data = {
	.addr_width = 32,
	.mac_rgmii_txclk_auto_adj = true,
	.set_intf_mode = imx8dxl_set_intf_mode,
};

static struct imx_dwmac_ops imx93_dwmac_data = {
	.addr_width = 32,
	.mac_rgmii_txclk_auto_adj = true,
	.set_intf_mode = imx93_set_intf_mode,
	.fix_soc_reset = imx_dwmac_mx93_reset,
	.fix_mac_speed = imx93_dwmac_fix_speed,
};

static const struct of_device_id imx_dwmac_match[] = {
	{ .compatible = "nxp,imx8mp-dwmac-eqos", .data = &imx8mp_dwmac_data },
	{ .compatible = "nxp,imx8dxl-dwmac-eqos", .data = &imx8dxl_dwmac_data },
	{ .compatible = "nxp,imx93-dwmac-eqos", .data = &imx93_dwmac_data },
	{ }
};
MODULE_DEVICE_TABLE(of, imx_dwmac_match);

static struct platform_driver imx_dwmac_driver = {
	.probe  = imx_dwmac_probe,
	.remove_new = stmmac_pltfr_remove,
	.driver = {
		.name           = "imx-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = imx_dwmac_match,
	},
};
module_platform_driver(imx_dwmac_driver);

MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("NXP imx8 DWMAC Specific Glue layer");
MODULE_LICENSE("GPL v2");
