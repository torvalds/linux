// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Marvell
 *
 * Authors:
 *   Igal Liberman <igall@marvell.com>
 *   Miquèl Raynal <miquel.raynal@bootlin.com>
 *
 * Marvell A3700 UTMI PHY driver
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Armada 3700 UTMI PHY registers */
#define USB2_PHY_PLL_CTRL_REG0			0x0
#define   PLL_REF_DIV_OFF			0
#define   PLL_REF_DIV_MASK			GENMASK(6, 0)
#define   PLL_REF_DIV_5				5
#define   PLL_FB_DIV_OFF			16
#define   PLL_FB_DIV_MASK			GENMASK(24, 16)
#define   PLL_FB_DIV_96				96
#define   PLL_SEL_LPFR_OFF			28
#define   PLL_SEL_LPFR_MASK			GENMASK(29, 28)
#define   PLL_READY				BIT(31)
#define USB2_PHY_CAL_CTRL			0x8
#define   PHY_PLLCAL_DONE			BIT(31)
#define   PHY_IMPCAL_DONE			BIT(23)
#define USB2_RX_CHAN_CTRL1			0x18
#define   USB2PHY_SQCAL_DONE			BIT(31)
#define USB2_PHY_OTG_CTRL			0x34
#define   PHY_PU_OTG				BIT(4)
#define USB2_PHY_CHRGR_DETECT			0x38
#define   PHY_CDP_EN				BIT(2)
#define   PHY_DCP_EN				BIT(3)
#define   PHY_PD_EN				BIT(4)
#define   PHY_PU_CHRG_DTC			BIT(5)
#define   PHY_CDP_DM_AUTO			BIT(7)
#define   PHY_ENSWITCH_DP			BIT(12)
#define   PHY_ENSWITCH_DM			BIT(13)

/* Armada 3700 USB miscellaneous registers */
#define USB2_PHY_CTRL(usb32)			(usb32 ? 0x20 : 0x4)
#define   RB_USB2PHY_PU				BIT(0)
#define   USB2_DP_PULLDN_DEV_MODE		BIT(5)
#define   USB2_DM_PULLDN_DEV_MODE		BIT(6)
#define   RB_USB2PHY_SUSPM(usb32)		(usb32 ? BIT(14) : BIT(7))

#define PLL_LOCK_DELAY_US			10000
#define PLL_LOCK_TIMEOUT_US			1000000

/**
 * struct mvebu_a3700_utmi_caps - PHY capabilities
 *
 * @usb32: Flag indicating which PHY is in use (impacts the register map):
 *           - The UTMI PHY wired to the USB3/USB2 controller (otg)
 *           - The UTMI PHY wired to the USB2 controller (host only)
 * @ops: PHY operations
 */
struct mvebu_a3700_utmi_caps {
	int usb32;
	const struct phy_ops *ops;
};

/**
 * struct mvebu_a3700_utmi - PHY driver data
 *
 * @regs: PHY registers
 * @usb_mis: Regmap with USB miscellaneous registers including PHY ones
 * @caps: PHY capabilities
 * @phy: PHY handle
 */
struct mvebu_a3700_utmi {
	void __iomem *regs;
	struct regmap *usb_misc;
	const struct mvebu_a3700_utmi_caps *caps;
	struct phy *phy;
};

static int mvebu_a3700_utmi_phy_power_on(struct phy *phy)
{
	struct mvebu_a3700_utmi *utmi = phy_get_drvdata(phy);
	struct device *dev = &phy->dev;
	int usb32 = utmi->caps->usb32;
	int ret = 0;
	u32 reg;

	/*
	 * Setup PLL. 40MHz clock used to be the default, being 25MHz now.
	 * See "PLL Settings for Typical REFCLK" table.
	 */
	reg = readl(utmi->regs + USB2_PHY_PLL_CTRL_REG0);
	reg &= ~(PLL_REF_DIV_MASK | PLL_FB_DIV_MASK | PLL_SEL_LPFR_MASK);
	reg |= (PLL_REF_DIV_5 << PLL_REF_DIV_OFF) |
	       (PLL_FB_DIV_96 << PLL_FB_DIV_OFF);
	writel(reg, utmi->regs + USB2_PHY_PLL_CTRL_REG0);

	/* Enable PHY pull up and disable USB2 suspend */
	regmap_update_bits(utmi->usb_misc, USB2_PHY_CTRL(usb32),
			   RB_USB2PHY_SUSPM(usb32) | RB_USB2PHY_PU,
			   RB_USB2PHY_SUSPM(usb32) | RB_USB2PHY_PU);

	if (usb32) {
		/* Power up OTG module */
		reg = readl(utmi->regs + USB2_PHY_OTG_CTRL);
		reg |= PHY_PU_OTG;
		writel(reg, utmi->regs + USB2_PHY_OTG_CTRL);

		/* Disable PHY charger detection */
		reg = readl(utmi->regs + USB2_PHY_CHRGR_DETECT);
		reg &= ~(PHY_CDP_EN | PHY_DCP_EN | PHY_PD_EN | PHY_PU_CHRG_DTC |
			 PHY_CDP_DM_AUTO | PHY_ENSWITCH_DP | PHY_ENSWITCH_DM);
		writel(reg, utmi->regs + USB2_PHY_CHRGR_DETECT);

		/* Disable PHY DP/DM pull-down (used for device mode) */
		regmap_update_bits(utmi->usb_misc, USB2_PHY_CTRL(usb32),
				   USB2_DP_PULLDN_DEV_MODE |
				   USB2_DM_PULLDN_DEV_MODE, 0);
	}

	/* Wait for PLL calibration */
	ret = readl_poll_timeout(utmi->regs + USB2_PHY_CAL_CTRL, reg,
				 reg & PHY_PLLCAL_DONE,
				 PLL_LOCK_DELAY_US, PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Failed to end USB2 PLL calibration\n");
		return ret;
	}

	/* Wait for impedance calibration */
	ret = readl_poll_timeout(utmi->regs + USB2_PHY_CAL_CTRL, reg,
				 reg & PHY_IMPCAL_DONE,
				 PLL_LOCK_DELAY_US, PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Failed to end USB2 impedance calibration\n");
		return ret;
	}

	/* Wait for squelch calibration */
	ret = readl_poll_timeout(utmi->regs + USB2_RX_CHAN_CTRL1, reg,
				 reg & USB2PHY_SQCAL_DONE,
				 PLL_LOCK_DELAY_US, PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Failed to end USB2 unknown calibration\n");
		return ret;
	}

	/* Wait for PLL to be locked */
	ret = readl_poll_timeout(utmi->regs + USB2_PHY_PLL_CTRL_REG0, reg,
				 reg & PLL_READY,
				 PLL_LOCK_DELAY_US, PLL_LOCK_TIMEOUT_US);
	if (ret)
		dev_err(dev, "Failed to lock USB2 PLL\n");

	return ret;
}

static int mvebu_a3700_utmi_phy_power_off(struct phy *phy)
{
	struct mvebu_a3700_utmi *utmi = phy_get_drvdata(phy);
	int usb32 = utmi->caps->usb32;
	u32 reg;

	/* Disable PHY pull-up and enable USB2 suspend */
	reg = readl(utmi->regs + USB2_PHY_CTRL(usb32));
	reg &= ~(RB_USB2PHY_PU | RB_USB2PHY_SUSPM(usb32));
	writel(reg, utmi->regs + USB2_PHY_CTRL(usb32));

	/* Power down OTG module */
	if (usb32) {
		reg = readl(utmi->regs + USB2_PHY_OTG_CTRL);
		reg &= ~PHY_PU_OTG;
		writel(reg, utmi->regs + USB2_PHY_OTG_CTRL);
	}

	return 0;
}

static const struct phy_ops mvebu_a3700_utmi_phy_ops = {
	.power_on = mvebu_a3700_utmi_phy_power_on,
	.power_off = mvebu_a3700_utmi_phy_power_off,
	.owner = THIS_MODULE,
};

static const struct mvebu_a3700_utmi_caps mvebu_a3700_utmi_otg_phy_caps = {
	.usb32 = true,
	.ops = &mvebu_a3700_utmi_phy_ops,
};

static const struct mvebu_a3700_utmi_caps mvebu_a3700_utmi_host_phy_caps = {
	.usb32 = false,
	.ops = &mvebu_a3700_utmi_phy_ops,
};

static const struct of_device_id mvebu_a3700_utmi_of_match[] = {
	{
		.compatible = "marvell,a3700-utmi-otg-phy",
		.data = &mvebu_a3700_utmi_otg_phy_caps,
	},
	{
		.compatible = "marvell,a3700-utmi-host-phy",
		.data = &mvebu_a3700_utmi_host_phy_caps,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mvebu_a3700_utmi_of_match);

static int mvebu_a3700_utmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mvebu_a3700_utmi *utmi;
	struct phy_provider *provider;
	struct resource *res;

	utmi = devm_kzalloc(dev, sizeof(*utmi), GFP_KERNEL);
	if (!utmi)
		return -ENOMEM;

	/* Get UTMI memory region */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Missing UTMI PHY memory resource\n");
		return -ENODEV;
	}

	utmi->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(utmi->regs))
		return PTR_ERR(utmi->regs);

	/* Get miscellaneous Host/PHY region */
	utmi->usb_misc = syscon_regmap_lookup_by_phandle(dev->of_node,
							 "marvell,usb-misc-reg");
	if (IS_ERR(utmi->usb_misc)) {
		dev_err(dev,
			"Missing USB misc purpose system controller\n");
		return PTR_ERR(utmi->usb_misc);
	}

	/* Retrieve PHY capabilities */
	utmi->caps = of_device_get_match_data(dev);

	/* Instantiate the PHY */
	utmi->phy = devm_phy_create(dev, NULL, utmi->caps->ops);
	if (IS_ERR(utmi->phy)) {
		dev_err(dev, "Failed to create the UTMI PHY\n");
		return PTR_ERR(utmi->phy);
	}

	phy_set_drvdata(utmi->phy, utmi);

	/* Ensure the PHY is powered off */
	utmi->caps->ops->power_off(utmi->phy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static struct platform_driver mvebu_a3700_utmi_driver = {
	.probe	= mvebu_a3700_utmi_phy_probe,
	.driver	= {
		.name		= "mvebu-a3700-utmi-phy",
		.of_match_table	= mvebu_a3700_utmi_of_match,
	 },
};
module_platform_driver(mvebu_a3700_utmi_driver);

MODULE_AUTHOR("Igal Liberman <igall@marvell.com>");
MODULE_AUTHOR("Miquel Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("Marvell EBU A3700 UTMI PHY driver");
MODULE_LICENSE("GPL v2");
