// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 John Crispin <john@phrozen.org>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define RT_SYSC_REG_SYSCFG1		0x014
#define RT_SYSC_REG_CLKCFG1		0x030
#define RT_SYSC_REG_USB_PHY_CFG		0x05c

#define OFS_U2_PHY_AC0			0x800
#define OFS_U2_PHY_AC1			0x804
#define OFS_U2_PHY_AC2			0x808
#define OFS_U2_PHY_ACR0			0x810
#define OFS_U2_PHY_ACR1			0x814
#define OFS_U2_PHY_ACR2			0x818
#define OFS_U2_PHY_ACR3			0x81C
#define OFS_U2_PHY_ACR4			0x820
#define OFS_U2_PHY_AMON0		0x824
#define OFS_U2_PHY_DCR0			0x860
#define OFS_U2_PHY_DCR1			0x864
#define OFS_U2_PHY_DTM0			0x868
#define OFS_U2_PHY_DTM1			0x86C

#define RT_RSTCTRL_UDEV			BIT(25)
#define RT_RSTCTRL_UHST			BIT(22)
#define RT_SYSCFG1_USB0_HOST_MODE	BIT(10)

#define MT7620_CLKCFG1_UPHY0_CLK_EN	BIT(25)
#define MT7620_CLKCFG1_UPHY1_CLK_EN	BIT(22)
#define RT_CLKCFG1_UPHY1_CLK_EN		BIT(20)
#define RT_CLKCFG1_UPHY0_CLK_EN		BIT(18)

#define USB_PHY_UTMI_8B60M		BIT(1)
#define UDEV_WAKEUP			BIT(0)

struct ralink_usb_phy {
	struct reset_control	*rstdev;
	struct reset_control	*rsthost;
	u32			clk;
	struct phy		*phy;
	void __iomem		*base;
	struct regmap		*sysctl;
};

static void u2_phy_w32(struct ralink_usb_phy *phy, u32 val, u32 reg)
{
	writel(val, phy->base + reg);
}

static u32 u2_phy_r32(struct ralink_usb_phy *phy, u32 reg)
{
	return readl(phy->base + reg);
}

static void ralink_usb_phy_init(struct ralink_usb_phy *phy)
{
	u2_phy_r32(phy, OFS_U2_PHY_AC2);
	u2_phy_r32(phy, OFS_U2_PHY_ACR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);

	u2_phy_w32(phy, 0x00ffff02, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x00555502, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x00aaaa02, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x00000402, OFS_U2_PHY_DCR0);
	u2_phy_r32(phy, OFS_U2_PHY_DCR0);
	u2_phy_w32(phy, 0x0048086a, OFS_U2_PHY_AC0);
	u2_phy_w32(phy, 0x4400001c, OFS_U2_PHY_AC1);
	u2_phy_w32(phy, 0xc0200000, OFS_U2_PHY_ACR3);
	u2_phy_w32(phy, 0x02000000, OFS_U2_PHY_DTM0);
}

static int ralink_usb_phy_power_on(struct phy *_phy)
{
	struct ralink_usb_phy *phy = phy_get_drvdata(_phy);
	u32 t;

	/* enable the phy */
	regmap_update_bits(phy->sysctl, RT_SYSC_REG_CLKCFG1,
			   phy->clk, phy->clk);

	/* setup host mode */
	regmap_update_bits(phy->sysctl, RT_SYSC_REG_SYSCFG1,
			   RT_SYSCFG1_USB0_HOST_MODE,
			   RT_SYSCFG1_USB0_HOST_MODE);

	/* deassert the reset lines */
	reset_control_deassert(phy->rsthost);
	reset_control_deassert(phy->rstdev);

	/*
	 * The SDK kernel had a delay of 100ms. however on device
	 * testing showed that 10ms is enough
	 */
	mdelay(10);

	if (phy->base)
		ralink_usb_phy_init(phy);

	/* print some status info */
	regmap_read(phy->sysctl, RT_SYSC_REG_USB_PHY_CFG, &t);
	dev_info(&phy->phy->dev, "remote usb device wakeup %s\n",
		(t & UDEV_WAKEUP) ? ("enabled") : ("disabled"));
	if (t & USB_PHY_UTMI_8B60M)
		dev_info(&phy->phy->dev, "UTMI 8bit 60MHz\n");
	else
		dev_info(&phy->phy->dev, "UTMI 16bit 30MHz\n");

	return 0;
}

static int ralink_usb_phy_power_off(struct phy *_phy)
{
	struct ralink_usb_phy *phy = phy_get_drvdata(_phy);

	/* disable the phy */
	regmap_update_bits(phy->sysctl, RT_SYSC_REG_CLKCFG1,
			   phy->clk, 0);

	/* assert the reset lines */
	reset_control_assert(phy->rstdev);
	reset_control_assert(phy->rsthost);

	return 0;
}

static struct phy_ops ralink_usb_phy_ops = {
	.power_on	= ralink_usb_phy_power_on,
	.power_off	= ralink_usb_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id ralink_usb_phy_of_match[] = {
	{
		.compatible = "ralink,rt3352-usbphy",
		.data = (void *)(uintptr_t)(RT_CLKCFG1_UPHY1_CLK_EN |
					    RT_CLKCFG1_UPHY0_CLK_EN)
	},
	{
		.compatible = "mediatek,mt7620-usbphy",
		.data = (void *)(uintptr_t)(MT7620_CLKCFG1_UPHY1_CLK_EN |
					    MT7620_CLKCFG1_UPHY0_CLK_EN)
	},
	{
		.compatible = "mediatek,mt7628-usbphy",
		.data = (void *)(uintptr_t)(MT7620_CLKCFG1_UPHY1_CLK_EN |
					    MT7620_CLKCFG1_UPHY0_CLK_EN) },
	{ },
};
MODULE_DEVICE_TABLE(of, ralink_usb_phy_of_match);

static int ralink_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct phy_provider *phy_provider;
	const struct of_device_id *match;
	struct ralink_usb_phy *phy;

	match = of_match_device(ralink_usb_phy_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->clk = (uintptr_t)match->data;
	phy->base = NULL;

	phy->sysctl = syscon_regmap_lookup_by_phandle(dev->of_node, "ralink,sysctl");
	if (IS_ERR(phy->sysctl)) {
		dev_err(dev, "failed to get sysctl registers\n");
		return PTR_ERR(phy->sysctl);
	}

	/* The MT7628 and MT7688 require extra setup of PHY registers. */
	if (of_device_is_compatible(dev->of_node, "mediatek,mt7628-usbphy")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		phy->base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(phy->base)) {
			dev_err(dev, "failed to remap register memory\n");
			return PTR_ERR(phy->base);
		}
	}

	phy->rsthost = devm_reset_control_get(&pdev->dev, "host");
	if (IS_ERR(phy->rsthost)) {
		dev_err(dev, "host reset is missing\n");
		return PTR_ERR(phy->rsthost);
	}

	phy->rstdev = devm_reset_control_get(&pdev->dev, "device");
	if (IS_ERR(phy->rstdev)) {
		dev_err(dev, "device reset is missing\n");
		return PTR_ERR(phy->rstdev);
	}

	phy->phy = devm_phy_create(dev, NULL, &ralink_usb_phy_ops);
	if (IS_ERR(phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(phy->phy);
	}
	phy_set_drvdata(phy->phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver ralink_usb_phy_driver = {
	.probe	= ralink_usb_phy_probe,
	.driver = {
		.of_match_table	= ralink_usb_phy_of_match,
		.name  = "ralink-usb-phy",
	}
};
module_platform_driver(ralink_usb_phy_driver);

MODULE_DESCRIPTION("Ralink USB phy driver");
MODULE_AUTHOR("John Crispin <john@phrozen.org>");
MODULE_LICENSE("GPL v2");
