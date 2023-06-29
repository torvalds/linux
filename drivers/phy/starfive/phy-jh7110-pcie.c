// SPDX-License-Identifier: GPL-2.0+
/*
 * StarFive JH7110 PCIe 2.0 PHY driver
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 * Author: Minda Chen <minda.chen@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define PCIE_KVCO_LEVEL_OFF		0x28
#define PCIE_USB3_PHY_PLL_CTL_OFF	0x7c
#define PCIE_KVCO_TUNE_SIGNAL_OFF	0x80
#define PCIE_USB3_PHY_ENABLE		BIT(4)
#define PHY_KVCO_FINE_TUNE_LEVEL	0x91
#define PHY_KVCO_FINE_TUNE_SIGNALS	0xc

#define USB_PDRSTN_SPLIT		BIT(17)

#define PCIE_PHY_MODE			BIT(20)
#define PCIE_PHY_MODE_MASK		GENMASK(21, 20)
#define PCIE_USB3_BUS_WIDTH_MASK	GENMASK(3, 2)
#define PCIE_USB3_BUS_WIDTH		BIT(3)
#define PCIE_USB3_RATE_MASK		GENMASK(6, 5)
#define PCIE_USB3_RX_STANDBY_MASK	BIT(7)
#define PCIE_USB3_PHY_ENABLE		BIT(4)

struct jh7110_pcie_phy {
	struct phy *phy;
	struct regmap *stg_syscon;
	struct regmap *sys_syscon;
	void __iomem *regs;
	u32 sys_phy_connect;
	u32 stg_pcie_mode;
	u32 stg_pcie_usb;
	enum phy_mode mode;
};

static int phy_usb3_mode_set(struct jh7110_pcie_phy *data)
{
	if (!data->stg_syscon || !data->sys_syscon) {
		dev_err(&data->phy->dev, "doesn't support usb3 mode\n");
		return -EINVAL;
	}

	regmap_update_bits(data->stg_syscon, data->stg_pcie_mode,
			   PCIE_PHY_MODE_MASK, PCIE_PHY_MODE);
	regmap_update_bits(data->stg_syscon, data->stg_pcie_usb,
			   PCIE_USB3_BUS_WIDTH_MASK, 0);
	regmap_update_bits(data->stg_syscon, data->stg_pcie_usb,
			   PCIE_USB3_PHY_ENABLE, PCIE_USB3_PHY_ENABLE);

	/* Connect usb 3.0 phy mode */
	regmap_update_bits(data->sys_syscon, data->sys_phy_connect,
			   USB_PDRSTN_SPLIT, 0);

	/* Configuare spread-spectrum mode: down-spread-spectrum */
	writel(PCIE_USB3_PHY_ENABLE, data->regs + PCIE_USB3_PHY_PLL_CTL_OFF);

	return 0;
}

static void phy_pcie_mode_set(struct jh7110_pcie_phy *data)
{
	u32 val;

	/* default is PCIe mode */
	if (!data->stg_syscon || !data->sys_syscon)
		return;

	regmap_update_bits(data->stg_syscon, data->stg_pcie_mode,
			   PCIE_PHY_MODE_MASK, 0);
	regmap_update_bits(data->stg_syscon, data->stg_pcie_usb,
			   PCIE_USB3_BUS_WIDTH_MASK,
			   PCIE_USB3_BUS_WIDTH);
	regmap_update_bits(data->stg_syscon, data->stg_pcie_usb,
			   PCIE_USB3_PHY_ENABLE, 0);

	regmap_update_bits(data->sys_syscon, data->sys_phy_connect,
			   USB_PDRSTN_SPLIT, 0);

	val = readl(data->regs + PCIE_USB3_PHY_PLL_CTL_OFF);
	val &= ~PCIE_USB3_PHY_ENABLE;
	writel(val, data->regs + PCIE_USB3_PHY_PLL_CTL_OFF);
}

static void phy_kvco_gain_set(struct jh7110_pcie_phy *phy)
{
	/* PCIe Multi-PHY PLL KVCO Gain fine tune settings: */
	writel(PHY_KVCO_FINE_TUNE_LEVEL, phy->regs + PCIE_KVCO_LEVEL_OFF);
	writel(PHY_KVCO_FINE_TUNE_SIGNALS, phy->regs + PCIE_KVCO_TUNE_SIGNAL_OFF);
}

static int jh7110_pcie_phy_set_mode(struct phy *_phy,
				    enum phy_mode mode, int submode)
{
	struct jh7110_pcie_phy *phy = phy_get_drvdata(_phy);
	int ret;

	if (mode == phy->mode)
		return 0;

	switch (mode) {
	case PHY_MODE_USB_HOST:
	case PHY_MODE_USB_DEVICE:
	case PHY_MODE_USB_OTG:
		ret = phy_usb3_mode_set(phy);
		if (ret)
			return ret;
		break;
	case PHY_MODE_PCIE:
		phy_pcie_mode_set(phy);
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(&_phy->dev, "Changing phy mode to %d\n", mode);
	phy->mode = mode;

	return 0;
}

static const struct phy_ops jh7110_pcie_phy_ops = {
	.set_mode	= jh7110_pcie_phy_set_mode,
	.owner		= THIS_MODULE,
};

static int jh7110_pcie_phy_probe(struct platform_device *pdev)
{
	struct jh7110_pcie_phy *phy;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	u32 args[2];

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->regs))
		return PTR_ERR(phy->regs);

	phy->phy = devm_phy_create(dev, NULL, &jh7110_pcie_phy_ops);
	if (IS_ERR(phy->phy))
		return dev_err_probe(dev, PTR_ERR(phy->regs),
				     "Failed to map phy base\n");

	phy->sys_syscon =
		syscon_regmap_lookup_by_phandle_args(pdev->dev.of_node,
						     "starfive,sys-syscon",
						     1, args);

	if (!IS_ERR_OR_NULL(phy->sys_syscon))
		phy->sys_phy_connect = args[0];
	else
		phy->sys_syscon = NULL;

	phy->stg_syscon =
		syscon_regmap_lookup_by_phandle_args(pdev->dev.of_node,
						     "starfive,stg-syscon",
						     2, args);

	if (!IS_ERR_OR_NULL(phy->stg_syscon)) {
		phy->stg_pcie_mode = args[0];
		phy->stg_pcie_usb = args[1];
	} else {
		phy->stg_syscon = NULL;
	}

	phy_kvco_gain_set(phy);

	phy_set_drvdata(phy->phy, phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id jh7110_pcie_phy_of_match[] = {
	{ .compatible = "starfive,jh7110-pcie-phy" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, jh7110_pcie_phy_of_match);

static struct platform_driver jh7110_pcie_phy_driver = {
	.probe	= jh7110_pcie_phy_probe,
	.driver = {
		.of_match_table	= jh7110_pcie_phy_of_match,
		.name  = "jh7110-pcie-phy",
	}
};
module_platform_driver(jh7110_pcie_phy_driver);

MODULE_DESCRIPTION("StarFive JH7110 PCIe 2.0 PHY driver");
MODULE_AUTHOR("Minda Chen <minda.chen@starfivetech.com>");
MODULE_LICENSE("GPL");
