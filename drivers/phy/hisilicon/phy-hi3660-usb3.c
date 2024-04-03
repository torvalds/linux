// SPDX-License-Identifier: GPL-2.0
/*
 * Phy provider for USB 3.0 controller on HiSilicon 3660 platform
 *
 * Copyright (C) 2017-2018 Hilisicon Electronics Co., Ltd.
 *		http://www.huawei.com
 *
 * Authors: Yu Chen <chenyu56@huawei.com>
 */

#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define PERI_CRG_CLK_EN4			0x40
#define PERI_CRG_CLK_DIS4			0x44
#define GT_CLK_USB3OTG_REF			BIT(0)
#define GT_ACLK_USB3OTG				BIT(1)

#define PERI_CRG_RSTEN4				0x90
#define PERI_CRG_RSTDIS4			0x94
#define IP_RST_USB3OTGPHY_POR			BIT(3)
#define IP_RST_USB3OTG				BIT(5)

#define PERI_CRG_ISODIS				0x148
#define USB_REFCLK_ISO_EN			BIT(25)

#define PCTRL_PERI_CTRL3			0x10
#define PCTRL_PERI_CTRL3_MSK_START		16
#define USB_TCXO_EN				BIT(1)

#define PCTRL_PERI_CTRL24			0x64
#define SC_CLK_USB3PHY_3MUX1_SEL		BIT(25)

#define USBOTG3_CTRL0				0x00
#define SC_USB3PHY_ABB_GT_EN			BIT(15)

#define USBOTG3_CTRL2				0x08
#define USBOTG3CTRL2_POWERDOWN_HSP		BIT(0)
#define USBOTG3CTRL2_POWERDOWN_SSP		BIT(1)

#define USBOTG3_CTRL3				0x0C
#define USBOTG3_CTRL3_VBUSVLDEXT		BIT(6)
#define USBOTG3_CTRL3_VBUSVLDEXTSEL		BIT(5)

#define USBOTG3_CTRL4				0x10

#define USBOTG3_CTRL7				0x1c
#define REF_SSP_EN				BIT(16)

/* This value config the default txtune parameter of the usb 2.0 phy */
#define HI3660_USB_DEFAULT_PHY_PARAM		0x1c466e3

struct hi3660_priv {
	struct device *dev;
	struct regmap *peri_crg;
	struct regmap *pctrl;
	struct regmap *otg_bc;
	u32 eye_diagram_param;
};

static int hi3660_phy_init(struct phy *phy)
{
	struct hi3660_priv *priv = phy_get_drvdata(phy);
	u32 val, mask;
	int ret;

	/* usb refclk iso disable */
	ret = regmap_write(priv->peri_crg, PERI_CRG_ISODIS, USB_REFCLK_ISO_EN);
	if (ret)
		goto out;

	/* enable usb_tcxo_en */
	val = USB_TCXO_EN | (USB_TCXO_EN << PCTRL_PERI_CTRL3_MSK_START);
	ret = regmap_write(priv->pctrl, PCTRL_PERI_CTRL3, val);
	if (ret)
		goto out;

	/* assert phy */
	val = IP_RST_USB3OTGPHY_POR | IP_RST_USB3OTG;
	ret = regmap_write(priv->peri_crg, PERI_CRG_RSTEN4, val);
	if (ret)
		goto out;

	/* enable phy ref clk */
	val = SC_USB3PHY_ABB_GT_EN;
	mask = val;
	ret = regmap_update_bits(priv->otg_bc, USBOTG3_CTRL0, mask, val);
	if (ret)
		goto out;

	val = REF_SSP_EN;
	mask = val;
	ret = regmap_update_bits(priv->otg_bc, USBOTG3_CTRL7, mask, val);
	if (ret)
		goto out;

	/* exit from IDDQ mode */
	mask = USBOTG3CTRL2_POWERDOWN_HSP | USBOTG3CTRL2_POWERDOWN_SSP;
	ret = regmap_update_bits(priv->otg_bc, USBOTG3_CTRL2, mask, 0);
	if (ret)
		goto out;

	/* delay for exit from IDDQ mode */
	usleep_range(100, 120);

	/* deassert phy */
	val = IP_RST_USB3OTGPHY_POR | IP_RST_USB3OTG;
	ret = regmap_write(priv->peri_crg, PERI_CRG_RSTDIS4, val);
	if (ret)
		goto out;

	/* delay for phy deasserted */
	usleep_range(10000, 15000);

	/* fake vbus valid signal */
	val = USBOTG3_CTRL3_VBUSVLDEXT | USBOTG3_CTRL3_VBUSVLDEXTSEL;
	mask = val;
	ret = regmap_update_bits(priv->otg_bc, USBOTG3_CTRL3, mask, val);
	if (ret)
		goto out;

	/* delay for vbus valid */
	usleep_range(100, 120);

	ret = regmap_write(priv->otg_bc, USBOTG3_CTRL4,
			priv->eye_diagram_param);
	if (ret)
		goto out;

	return 0;
out:
	dev_err(priv->dev, "failed to init phy ret: %d\n", ret);
	return ret;
}

static int hi3660_phy_exit(struct phy *phy)
{
	struct hi3660_priv *priv = phy_get_drvdata(phy);
	u32 val;
	int ret;

	/* assert phy */
	val = IP_RST_USB3OTGPHY_POR;
	ret = regmap_write(priv->peri_crg, PERI_CRG_RSTEN4, val);
	if (ret)
		goto out;

	/* disable usb_tcxo_en */
	val = USB_TCXO_EN << PCTRL_PERI_CTRL3_MSK_START;
	ret = regmap_write(priv->pctrl, PCTRL_PERI_CTRL3, val);
	if (ret)
		goto out;

	return 0;
out:
	dev_err(priv->dev, "failed to exit phy ret: %d\n", ret);
	return ret;
}

static const struct phy_ops hi3660_phy_ops = {
	.init		= hi3660_phy_init,
	.exit		= hi3660_phy_exit,
	.owner		= THIS_MODULE,
};

static int hi3660_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct phy *phy;
	struct hi3660_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->peri_crg = syscon_regmap_lookup_by_phandle(dev->of_node,
					"hisilicon,pericrg-syscon");
	if (IS_ERR(priv->peri_crg)) {
		dev_err(dev, "no hisilicon,pericrg-syscon\n");
		return PTR_ERR(priv->peri_crg);
	}

	priv->pctrl = syscon_regmap_lookup_by_phandle(dev->of_node,
					"hisilicon,pctrl-syscon");
	if (IS_ERR(priv->pctrl)) {
		dev_err(dev, "no hisilicon,pctrl-syscon\n");
		return PTR_ERR(priv->pctrl);
	}

	/* node of hi3660 phy is a sub-node of usb3_otg_bc */
	priv->otg_bc = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(priv->otg_bc)) {
		dev_err(dev, "no hisilicon,usb3-otg-bc-syscon\n");
		return PTR_ERR(priv->otg_bc);
	}

	if (of_property_read_u32(dev->of_node, "hisilicon,eye-diagram-param",
		&(priv->eye_diagram_param)))
		priv->eye_diagram_param = HI3660_USB_DEFAULT_PHY_PARAM;

	phy = devm_phy_create(dev, NULL, &hi3660_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id hi3660_phy_of_match[] = {
	{.compatible = "hisilicon,hi3660-usb-phy",},
	{ }
};
MODULE_DEVICE_TABLE(of, hi3660_phy_of_match);

static struct platform_driver hi3660_phy_driver = {
	.probe	= hi3660_phy_probe,
	.driver = {
		.name	= "hi3660-usb-phy",
		.of_match_table	= hi3660_phy_of_match,
	}
};
module_platform_driver(hi3660_phy_driver);

MODULE_AUTHOR("Yu Chen <chenyu56@huawei.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Hilisicon Hi3660 USB3 PHY Driver");
