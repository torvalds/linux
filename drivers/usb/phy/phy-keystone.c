// SPDX-License-Identifier: GPL-2.0+
/*
 * phy-keystone - USB PHY, talking to dwc3 controller in Keystone.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: WingMan Kwok <w-kwok2@ti.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/io.h>
#include <linux/of.h>

#include "phy-generic.h"

/* USB PHY control register offsets */
#define USB_PHY_CTL_UTMI		0x0000
#define USB_PHY_CTL_PIPE		0x0004
#define USB_PHY_CTL_PARAM_1		0x0008
#define USB_PHY_CTL_PARAM_2		0x000c
#define USB_PHY_CTL_CLOCK		0x0010
#define USB_PHY_CTL_PLL			0x0014

#define PHY_REF_SSP_EN			BIT(29)

struct keystone_usbphy {
	struct usb_phy_generic	usb_phy_gen;
	void __iomem			*phy_ctrl;
};

static inline u32 keystone_usbphy_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void keystone_usbphy_writel(void __iomem *base,
					  u32 offset, u32 value)
{
	writel(value, base + offset);
}

static int keystone_usbphy_init(struct usb_phy *phy)
{
	struct keystone_usbphy *k_phy = dev_get_drvdata(phy->dev);
	u32 val;

	val  = keystone_usbphy_readl(k_phy->phy_ctrl, USB_PHY_CTL_CLOCK);
	keystone_usbphy_writel(k_phy->phy_ctrl, USB_PHY_CTL_CLOCK,
				val | PHY_REF_SSP_EN);
	return 0;
}

static void keystone_usbphy_shutdown(struct usb_phy *phy)
{
	struct keystone_usbphy *k_phy = dev_get_drvdata(phy->dev);
	u32 val;

	val  = keystone_usbphy_readl(k_phy->phy_ctrl, USB_PHY_CTL_CLOCK);
	keystone_usbphy_writel(k_phy->phy_ctrl, USB_PHY_CTL_CLOCK,
				val &= ~PHY_REF_SSP_EN);
}

static int keystone_usbphy_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct keystone_usbphy	*k_phy;
	int ret;

	k_phy = devm_kzalloc(dev, sizeof(*k_phy), GFP_KERNEL);
	if (!k_phy)
		return -ENOMEM;

	k_phy->phy_ctrl = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(k_phy->phy_ctrl))
		return PTR_ERR(k_phy->phy_ctrl);

	ret = usb_phy_gen_create_phy(dev, &k_phy->usb_phy_gen);
	if (ret)
		return ret;

	k_phy->usb_phy_gen.phy.init = keystone_usbphy_init;
	k_phy->usb_phy_gen.phy.shutdown = keystone_usbphy_shutdown;

	platform_set_drvdata(pdev, k_phy);

	return usb_add_phy_dev(&k_phy->usb_phy_gen.phy);
}

static int keystone_usbphy_remove(struct platform_device *pdev)
{
	struct keystone_usbphy *k_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&k_phy->usb_phy_gen.phy);

	return 0;
}

static const struct of_device_id keystone_usbphy_ids[] = {
	{ .compatible = "ti,keystone-usbphy" },
	{ }
};
MODULE_DEVICE_TABLE(of, keystone_usbphy_ids);

static struct platform_driver keystone_usbphy_driver = {
	.probe          = keystone_usbphy_probe,
	.remove         = keystone_usbphy_remove,
	.driver         = {
		.name   = "keystone-usbphy",
		.of_match_table = keystone_usbphy_ids,
	},
};

module_platform_driver(keystone_usbphy_driver);

MODULE_ALIAS("platform:keystone-usbphy");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("Keystone USB phy driver");
MODULE_LICENSE("GPL v2");
