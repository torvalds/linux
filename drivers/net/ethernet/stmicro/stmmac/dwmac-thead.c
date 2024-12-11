// SPDX-License-Identifier: GPL-2.0
/*
 * T-HEAD DWMAC platform driver
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 *
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>

#include "stmmac_platform.h"

#define GMAC_CLK_EN			0x00
#define  GMAC_TX_CLK_EN			BIT(1)
#define  GMAC_TX_CLK_N_EN		BIT(2)
#define  GMAC_TX_CLK_OUT_EN		BIT(3)
#define  GMAC_RX_CLK_EN			BIT(4)
#define  GMAC_RX_CLK_N_EN		BIT(5)
#define  GMAC_EPHY_REF_CLK_EN		BIT(6)
#define GMAC_RXCLK_DELAY_CTRL		0x04
#define  GMAC_RXCLK_BYPASS		BIT(15)
#define  GMAC_RXCLK_INVERT		BIT(14)
#define  GMAC_RXCLK_DELAY		GENMASK(4, 0)
#define GMAC_TXCLK_DELAY_CTRL		0x08
#define  GMAC_TXCLK_BYPASS		BIT(15)
#define  GMAC_TXCLK_INVERT		BIT(14)
#define  GMAC_TXCLK_DELAY		GENMASK(4, 0)
#define GMAC_PLLCLK_DIV			0x0c
#define  GMAC_PLLCLK_DIV_EN		BIT(31)
#define  GMAC_PLLCLK_DIV_NUM		GENMASK(7, 0)
#define GMAC_GTXCLK_SEL			0x18
#define  GMAC_GTXCLK_SEL_PLL		BIT(0)
#define GMAC_INTF_CTRL			0x1c
#define  PHY_INTF_MASK			BIT(0)
#define  PHY_INTF_RGMII			FIELD_PREP(PHY_INTF_MASK, 1)
#define  PHY_INTF_MII_GMII		FIELD_PREP(PHY_INTF_MASK, 0)
#define GMAC_TXCLK_OEN			0x20
#define  TXCLK_DIR_MASK			BIT(0)
#define  TXCLK_DIR_OUTPUT		FIELD_PREP(TXCLK_DIR_MASK, 0)
#define  TXCLK_DIR_INPUT		FIELD_PREP(TXCLK_DIR_MASK, 1)

#define GMAC_GMII_RGMII_RATE	125000000
#define GMAC_MII_RATE		25000000

struct thead_dwmac {
	struct plat_stmmacenet_data *plat;
	void __iomem *apb_base;
	struct device *dev;
};

static int thead_dwmac_set_phy_if(struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	u32 phyif;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		phyif = PHY_INTF_MII_GMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		phyif = PHY_INTF_RGMII;
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return -EINVAL;
	}

	writel(phyif, dwmac->apb_base + GMAC_INTF_CTRL);
	return 0;
}

static int thead_dwmac_set_txclk_dir(struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	u32 txclk_dir;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		txclk_dir = TXCLK_DIR_INPUT;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		txclk_dir = TXCLK_DIR_OUTPUT;
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return -EINVAL;
	}

	writel(txclk_dir, dwmac->apb_base + GMAC_TXCLK_OEN);
	return 0;
}

static void thead_dwmac_fix_speed(void *priv, unsigned int speed, unsigned int mode)
{
	struct plat_stmmacenet_data *plat;
	struct thead_dwmac *dwmac = priv;
	unsigned long rate;
	u32 div, reg;

	plat = dwmac->plat;

	switch (plat->mac_interface) {
	/* For MII, rxc/txc is provided by phy */
	case PHY_INTERFACE_MODE_MII:
		return;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rate = clk_get_rate(plat->stmmac_clk);
		if (!rate || rate % GMAC_GMII_RGMII_RATE != 0 ||
		    rate % GMAC_MII_RATE != 0) {
			dev_err(dwmac->dev, "invalid gmac rate %ld\n", rate);
			return;
		}

		writel(0, dwmac->apb_base + GMAC_PLLCLK_DIV);

		switch (speed) {
		case SPEED_1000:
			div = rate / GMAC_GMII_RGMII_RATE;
			break;
		case SPEED_100:
			div = rate / GMAC_MII_RATE;
			break;
		case SPEED_10:
			div = rate * 10 / GMAC_MII_RATE;
			break;
		default:
			dev_err(dwmac->dev, "invalid speed %u\n", speed);
			return;
		}

		reg = FIELD_PREP(GMAC_PLLCLK_DIV_EN, 1) |
		      FIELD_PREP(GMAC_PLLCLK_DIV_NUM, div);
		writel(reg, dwmac->apb_base + GMAC_PLLCLK_DIV);
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return;
	}
}

static int thead_dwmac_enable_clk(struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	u32 reg;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		reg = GMAC_RX_CLK_EN | GMAC_TX_CLK_EN;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* use pll */
		writel(GMAC_GTXCLK_SEL_PLL, dwmac->apb_base + GMAC_GTXCLK_SEL);
		reg = GMAC_TX_CLK_EN | GMAC_TX_CLK_N_EN | GMAC_TX_CLK_OUT_EN |
		      GMAC_RX_CLK_EN | GMAC_RX_CLK_N_EN;
		break;

	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return -EINVAL;
	}

	writel(reg, dwmac->apb_base + GMAC_CLK_EN);
	return 0;
}

static int thead_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct thead_dwmac *dwmac = priv;
	unsigned int reg;
	int ret;

	ret = thead_dwmac_set_phy_if(dwmac->plat);
	if (ret)
		return ret;

	ret = thead_dwmac_set_txclk_dir(dwmac->plat);
	if (ret)
		return ret;

	reg = readl(dwmac->apb_base + GMAC_RXCLK_DELAY_CTRL);
	reg &= ~(GMAC_RXCLK_DELAY);
	reg |= FIELD_PREP(GMAC_RXCLK_DELAY, 0);
	writel(reg, dwmac->apb_base + GMAC_RXCLK_DELAY_CTRL);

	reg = readl(dwmac->apb_base + GMAC_TXCLK_DELAY_CTRL);
	reg &= ~(GMAC_TXCLK_DELAY);
	reg |= FIELD_PREP(GMAC_TXCLK_DELAY, 0);
	writel(reg, dwmac->apb_base + GMAC_TXCLK_DELAY_CTRL);

	return thead_dwmac_enable_clk(dwmac->plat);
}

static int thead_dwmac_probe(struct platform_device *pdev)
{
	struct stmmac_resources stmmac_res;
	struct plat_stmmacenet_data *plat;
	struct thead_dwmac *dwmac;
	void __iomem *apb;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get resources\n");

	plat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat),
				     "dt configuration failed\n");

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	apb = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(apb))
		return dev_err_probe(&pdev->dev, PTR_ERR(apb),
				     "failed to remap gmac apb registers\n");

	dwmac->dev = &pdev->dev;
	dwmac->plat = plat;
	dwmac->apb_base = apb;
	plat->bsp_priv = dwmac;
	plat->fix_mac_speed = thead_dwmac_fix_speed;
	plat->init = thead_dwmac_init;

	return devm_stmmac_pltfr_probe(pdev, plat, &stmmac_res);
}

static const struct of_device_id thead_dwmac_match[] = {
	{ .compatible = "thead,th1520-gmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, thead_dwmac_match);

static struct platform_driver thead_dwmac_driver = {
	.probe = thead_dwmac_probe,
	.driver = {
		.name = "thead-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = thead_dwmac_match,
	},
};
module_platform_driver(thead_dwmac_driver);

MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_AUTHOR("Drew Fustini <drew@pdp7.com>");
MODULE_DESCRIPTION("T-HEAD DWMAC platform driver");
MODULE_LICENSE("GPL");
