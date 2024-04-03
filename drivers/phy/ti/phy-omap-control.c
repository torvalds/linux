// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * omap-control-phy.c - The PHY part of control module.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/phy/omap_control_phy.h>

/**
 * omap_control_pcie_pcs - set the PCS delay count
 * @dev: the control module device
 * @delay: 8 bit delay value
 */
void omap_control_pcie_pcs(struct device *dev, u8 delay)
{
	u32 val;
	struct omap_control_phy	*control_phy;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: invalid device\n", __func__);
		return;
	}

	control_phy = dev_get_drvdata(dev);
	if (!control_phy) {
		dev_err(dev, "%s: invalid control phy device\n", __func__);
		return;
	}

	if (control_phy->type != OMAP_CTRL_TYPE_PCIE) {
		dev_err(dev, "%s: unsupported operation\n", __func__);
		return;
	}

	val = readl(control_phy->pcie_pcs);
	val &= ~(OMAP_CTRL_PCIE_PCS_MASK <<
		OMAP_CTRL_PCIE_PCS_DELAY_COUNT_SHIFT);
	val |= (delay << OMAP_CTRL_PCIE_PCS_DELAY_COUNT_SHIFT);
	writel(val, control_phy->pcie_pcs);
}
EXPORT_SYMBOL_GPL(omap_control_pcie_pcs);

/**
 * omap_control_phy_power - power on/off the phy using control module reg
 * @dev: the control module device
 * @on: 0 or 1, based on powering on or off the PHY
 */
void omap_control_phy_power(struct device *dev, int on)
{
	u32 val;
	unsigned long rate;
	struct omap_control_phy	*control_phy;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: invalid device\n", __func__);
		return;
	}

	control_phy = dev_get_drvdata(dev);
	if (!control_phy) {
		dev_err(dev, "%s: invalid control phy device\n", __func__);
		return;
	}

	if (control_phy->type == OMAP_CTRL_TYPE_OTGHS)
		return;

	val = readl(control_phy->power);

	switch (control_phy->type) {
	case OMAP_CTRL_TYPE_USB2:
		if (on)
			val &= ~OMAP_CTRL_DEV_PHY_PD;
		else
			val |= OMAP_CTRL_DEV_PHY_PD;
		break;

	case OMAP_CTRL_TYPE_PCIE:
	case OMAP_CTRL_TYPE_PIPE3:
		rate = clk_get_rate(control_phy->sys_clk);
		rate = rate/1000000;

		if (on) {
			val &= ~(OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_MASK |
				OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_FREQ_MASK);
			val |= OMAP_CTRL_PIPE3_PHY_TX_RX_POWERON <<
				OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_SHIFT;
			val |= rate <<
				OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_FREQ_SHIFT;
		} else {
			val &= ~OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_MASK;
			val |= OMAP_CTRL_PIPE3_PHY_TX_RX_POWEROFF <<
				OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_SHIFT;
		}
		break;

	case OMAP_CTRL_TYPE_DRA7USB2:
		if (on)
			val &= ~OMAP_CTRL_USB2_PHY_PD;
		else
			val |= OMAP_CTRL_USB2_PHY_PD;
		break;

	case OMAP_CTRL_TYPE_AM437USB2:
		if (on) {
			val &= ~(AM437X_CTRL_USB2_PHY_PD |
					AM437X_CTRL_USB2_OTG_PD);
			val |= (AM437X_CTRL_USB2_OTGVDET_EN |
					AM437X_CTRL_USB2_OTGSESSEND_EN);
		} else {
			val &= ~(AM437X_CTRL_USB2_OTGVDET_EN |
					AM437X_CTRL_USB2_OTGSESSEND_EN);
			val |= (AM437X_CTRL_USB2_PHY_PD |
					 AM437X_CTRL_USB2_OTG_PD);
		}
		break;
	default:
		dev_err(dev, "%s: type %d not recognized\n",
			__func__, control_phy->type);
		break;
	}

	writel(val, control_phy->power);
}
EXPORT_SYMBOL_GPL(omap_control_phy_power);

/**
 * omap_control_usb_host_mode - set AVALID, VBUSVALID and ID pin in grounded
 * @ctrl_phy: struct omap_control_phy *
 *
 * Writes to the mailbox register to notify the usb core that a usb
 * device has been connected.
 */
static void omap_control_usb_host_mode(struct omap_control_phy *ctrl_phy)
{
	u32 val;

	val = readl(ctrl_phy->otghs_control);
	val &= ~(OMAP_CTRL_DEV_IDDIG | OMAP_CTRL_DEV_SESSEND);
	val |= OMAP_CTRL_DEV_AVALID | OMAP_CTRL_DEV_VBUSVALID;
	writel(val, ctrl_phy->otghs_control);
}

/**
 * omap_control_usb_device_mode - set AVALID, VBUSVALID and ID pin in high
 * impedance
 * @ctrl_phy: struct omap_control_phy *
 *
 * Writes to the mailbox register to notify the usb core that it has been
 * connected to a usb host.
 */
static void omap_control_usb_device_mode(struct omap_control_phy *ctrl_phy)
{
	u32 val;

	val = readl(ctrl_phy->otghs_control);
	val &= ~OMAP_CTRL_DEV_SESSEND;
	val |= OMAP_CTRL_DEV_IDDIG | OMAP_CTRL_DEV_AVALID |
		OMAP_CTRL_DEV_VBUSVALID;
	writel(val, ctrl_phy->otghs_control);
}

/**
 * omap_control_usb_set_sessionend - Enable SESSIONEND and IDIG to high
 * impedance
 * @ctrl_phy: struct omap_control_phy *
 *
 * Writes to the mailbox register to notify the usb core it's now in
 * disconnected state.
 */
static void omap_control_usb_set_sessionend(struct omap_control_phy *ctrl_phy)
{
	u32 val;

	val = readl(ctrl_phy->otghs_control);
	val &= ~(OMAP_CTRL_DEV_AVALID | OMAP_CTRL_DEV_VBUSVALID);
	val |= OMAP_CTRL_DEV_IDDIG | OMAP_CTRL_DEV_SESSEND;
	writel(val, ctrl_phy->otghs_control);
}

/**
 * omap_control_usb_set_mode - Calls to functions to set USB in one of host mode
 * or device mode or to denote disconnected state
 * @dev: the control module device
 * @mode: The mode to which usb should be configured
 *
 * This is an API to write to the mailbox register to notify the usb core that
 * a usb device has been connected.
 */
void omap_control_usb_set_mode(struct device *dev,
	enum omap_control_usb_mode mode)
{
	struct omap_control_phy	*ctrl_phy;

	if (IS_ERR_OR_NULL(dev))
		return;

	ctrl_phy = dev_get_drvdata(dev);
	if (!ctrl_phy) {
		dev_err(dev, "Invalid control phy device\n");
		return;
	}

	if (ctrl_phy->type != OMAP_CTRL_TYPE_OTGHS)
		return;

	switch (mode) {
	case USB_MODE_HOST:
		omap_control_usb_host_mode(ctrl_phy);
		break;
	case USB_MODE_DEVICE:
		omap_control_usb_device_mode(ctrl_phy);
		break;
	case USB_MODE_DISCONNECT:
		omap_control_usb_set_sessionend(ctrl_phy);
		break;
	default:
		dev_vdbg(dev, "invalid omap control usb mode\n");
	}
}
EXPORT_SYMBOL_GPL(omap_control_usb_set_mode);

static const enum omap_control_phy_type otghs_data = OMAP_CTRL_TYPE_OTGHS;
static const enum omap_control_phy_type usb2_data = OMAP_CTRL_TYPE_USB2;
static const enum omap_control_phy_type pipe3_data = OMAP_CTRL_TYPE_PIPE3;
static const enum omap_control_phy_type pcie_data = OMAP_CTRL_TYPE_PCIE;
static const enum omap_control_phy_type dra7usb2_data = OMAP_CTRL_TYPE_DRA7USB2;
static const enum omap_control_phy_type am437usb2_data = OMAP_CTRL_TYPE_AM437USB2;

static const struct of_device_id omap_control_phy_id_table[] = {
	{
		.compatible = "ti,control-phy-otghs",
		.data = &otghs_data,
	},
	{
		.compatible = "ti,control-phy-usb2",
		.data = &usb2_data,
	},
	{
		.compatible = "ti,control-phy-pipe3",
		.data = &pipe3_data,
	},
	{
		.compatible = "ti,control-phy-pcie",
		.data = &pcie_data,
	},
	{
		.compatible = "ti,control-phy-usb2-dra7",
		.data = &dra7usb2_data,
	},
	{
		.compatible = "ti,control-phy-usb2-am437",
		.data = &am437usb2_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_control_phy_id_table);

static int omap_control_phy_probe(struct platform_device *pdev)
{
	struct omap_control_phy *control_phy;

	control_phy = devm_kzalloc(&pdev->dev, sizeof(*control_phy),
		GFP_KERNEL);
	if (!control_phy)
		return -ENOMEM;

	control_phy->dev = &pdev->dev;
	control_phy->type = *(enum omap_control_phy_type *)device_get_match_data(&pdev->dev);

	if (control_phy->type == OMAP_CTRL_TYPE_OTGHS) {
		control_phy->otghs_control =
			devm_platform_ioremap_resource_byname(pdev, "otghs_control");
		if (IS_ERR(control_phy->otghs_control))
			return PTR_ERR(control_phy->otghs_control);
	} else {
		control_phy->power =
			devm_platform_ioremap_resource_byname(pdev, "power");
		if (IS_ERR(control_phy->power)) {
			dev_err(&pdev->dev, "Couldn't get power register\n");
			return PTR_ERR(control_phy->power);
		}
	}

	if (control_phy->type == OMAP_CTRL_TYPE_PIPE3 ||
	    control_phy->type == OMAP_CTRL_TYPE_PCIE) {
		control_phy->sys_clk = devm_clk_get(control_phy->dev,
			"sys_clkin");
		if (IS_ERR(control_phy->sys_clk)) {
			pr_err("%s: unable to get sys_clkin\n", __func__);
			return -EINVAL;
		}
	}

	if (control_phy->type == OMAP_CTRL_TYPE_PCIE) {
		control_phy->pcie_pcs =
			devm_platform_ioremap_resource_byname(pdev, "pcie_pcs");
		if (IS_ERR(control_phy->pcie_pcs))
			return PTR_ERR(control_phy->pcie_pcs);
	}

	dev_set_drvdata(control_phy->dev, control_phy);

	return 0;
}

static struct platform_driver omap_control_phy_driver = {
	.probe		= omap_control_phy_probe,
	.driver		= {
		.name	= "omap-control-phy",
		.of_match_table = omap_control_phy_id_table,
	},
};

static int __init omap_control_phy_init(void)
{
	return platform_driver_register(&omap_control_phy_driver);
}
subsys_initcall(omap_control_phy_init);

static void __exit omap_control_phy_exit(void)
{
	platform_driver_unregister(&omap_control_phy_driver);
}
module_exit(omap_control_phy_exit);

MODULE_ALIAS("platform:omap_control_phy");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP Control Module PHY Driver");
MODULE_LICENSE("GPL v2");
