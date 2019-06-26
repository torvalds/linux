// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2017 NXP. */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define PHY_CTRL0			0x0
#define PHY_CTRL0_REF_SSP_EN		BIT(2)

#define PHY_CTRL1			0x4
#define PHY_CTRL1_RESET			BIT(0)
#define PHY_CTRL1_COMMONONN		BIT(1)
#define PHY_CTRL1_ATERESET		BIT(3)
#define PHY_CTRL1_VDATSRCENB0		BIT(19)
#define PHY_CTRL1_VDATDETENB0		BIT(20)

#define PHY_CTRL2			0x8
#define PHY_CTRL2_TXENABLEN0		BIT(8)

struct imx8mq_usb_phy {
	struct phy *phy;
	struct clk *clk;
	void __iomem *base;
	struct regulator *vbus;
};

static int imx8mq_usb_phy_init(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);
	u32 value;

	value = readl(imx_phy->base + PHY_CTRL1);
	value &= ~(PHY_CTRL1_VDATSRCENB0 | PHY_CTRL1_VDATDETENB0 |
		   PHY_CTRL1_COMMONONN);
	value |= PHY_CTRL1_RESET | PHY_CTRL1_ATERESET;
	writel(value, imx_phy->base + PHY_CTRL1);

	value = readl(imx_phy->base + PHY_CTRL0);
	value |= PHY_CTRL0_REF_SSP_EN;
	writel(value, imx_phy->base + PHY_CTRL0);

	value = readl(imx_phy->base + PHY_CTRL2);
	value |= PHY_CTRL2_TXENABLEN0;
	writel(value, imx_phy->base + PHY_CTRL2);

	value = readl(imx_phy->base + PHY_CTRL1);
	value &= ~(PHY_CTRL1_RESET | PHY_CTRL1_ATERESET);
	writel(value, imx_phy->base + PHY_CTRL1);

	return 0;
}

static int imx8mq_phy_power_on(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);
	int ret;

	ret = regulator_enable(imx_phy->vbus);
	if (ret)
		return ret;

	return clk_prepare_enable(imx_phy->clk);
}

static int imx8mq_phy_power_off(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(imx_phy->clk);
	regulator_disable(imx_phy->vbus);

	return 0;
}

static struct phy_ops imx8mq_usb_phy_ops = {
	.init		= imx8mq_usb_phy_init,
	.power_on	= imx8mq_phy_power_on,
	.power_off	= imx8mq_phy_power_off,
	.owner		= THIS_MODULE,
};

static int imx8mq_usb_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct imx8mq_usb_phy *imx_phy;
	struct resource *res;

	imx_phy = devm_kzalloc(dev, sizeof(*imx_phy), GFP_KERNEL);
	if (!imx_phy)
		return -ENOMEM;

	imx_phy->clk = devm_clk_get(dev, "phy");
	if (IS_ERR(imx_phy->clk)) {
		dev_err(dev, "failed to get imx8mq usb phy clock\n");
		return PTR_ERR(imx_phy->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	imx_phy->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(imx_phy->base))
		return PTR_ERR(imx_phy->base);

	imx_phy->phy = devm_phy_create(dev, NULL, &imx8mq_usb_phy_ops);
	if (IS_ERR(imx_phy->phy))
		return PTR_ERR(imx_phy->phy);

	imx_phy->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(imx_phy->vbus))
		return PTR_ERR(imx_phy->vbus);

	phy_set_drvdata(imx_phy->phy, imx_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id imx8mq_usb_phy_of_match[] = {
	{.compatible = "fsl,imx8mq-usb-phy",},
	{ },
};
MODULE_DEVICE_TABLE(of, imx8mq_usb_phy_of_match);

static struct platform_driver imx8mq_usb_phy_driver = {
	.probe	= imx8mq_usb_phy_probe,
	.driver = {
		.name	= "imx8mq-usb-phy",
		.of_match_table	= imx8mq_usb_phy_of_match,
	}
};
module_platform_driver(imx8mq_usb_phy_driver);

MODULE_DESCRIPTION("FSL IMX8MQ USB PHY driver");
MODULE_LICENSE("GPL");
