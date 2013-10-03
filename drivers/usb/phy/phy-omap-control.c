/*
 * omap-control-usb.c - The USB part of control module.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/usb/omap_control_usb.h>

/**
 * omap_control_usb_phy_power - power on/off the phy using control module reg
 * @dev: the control module device
 * @on: 0 or 1, based on powering on or off the PHY
 */
void omap_control_usb_phy_power(struct device *dev, int on)
{
	u32 val;
	unsigned long rate;
	struct omap_control_usb	*control_usb;

	if (IS_ERR(dev) || !dev) {
		pr_err("%s: invalid device\n", __func__);
		return;
	}

	control_usb = dev_get_drvdata(dev);
	if (!control_usb) {
		dev_err(dev, "%s: invalid control usb device\n", __func__);
		return;
	}

	if (control_usb->type == OMAP_CTRL_TYPE_OTGHS)
		return;

	val = readl(control_usb->power);

	switch (control_usb->type) {
	case OMAP_CTRL_TYPE_USB2:
		if (on)
			val &= ~OMAP_CTRL_DEV_PHY_PD;
		else
			val |= OMAP_CTRL_DEV_PHY_PD;
		break;

	case OMAP_CTRL_TYPE_PIPE3:
		rate = clk_get_rate(control_usb->sys_clk);
		rate = rate/1000000;

		if (on) {
			val &= ~(OMAP_CTRL_USB_PWRCTL_CLK_CMD_MASK |
					OMAP_CTRL_USB_PWRCTL_CLK_FREQ_MASK);
			val |= OMAP_CTRL_USB3_PHY_TX_RX_POWERON <<
				OMAP_CTRL_USB_PWRCTL_CLK_CMD_SHIFT;
			val |= rate << OMAP_CTRL_USB_PWRCTL_CLK_FREQ_SHIFT;
		} else {
			val &= ~OMAP_CTRL_USB_PWRCTL_CLK_CMD_MASK;
			val |= OMAP_CTRL_USB3_PHY_TX_RX_POWEROFF <<
				OMAP_CTRL_USB_PWRCTL_CLK_CMD_SHIFT;
		}
		break;

	case OMAP_CTRL_TYPE_DRA7USB2:
		if (on)
			val &= ~OMAP_CTRL_USB2_PHY_PD;
		else
			val |= OMAP_CTRL_USB2_PHY_PD;
		break;
	default:
		dev_err(dev, "%s: type %d not recognized\n",
					__func__, control_usb->type);
		break;
	}

	writel(val, control_usb->power);
}
EXPORT_SYMBOL_GPL(omap_control_usb_phy_power);

/**
 * omap_control_usb_host_mode - set AVALID, VBUSVALID and ID pin in grounded
 * @ctrl_usb: struct omap_control_usb *
 *
 * Writes to the mailbox register to notify the usb core that a usb
 * device has been connected.
 */
static void omap_control_usb_host_mode(struct omap_control_usb *ctrl_usb)
{
	u32 val;

	val = readl(ctrl_usb->otghs_control);
	val &= ~(OMAP_CTRL_DEV_IDDIG | OMAP_CTRL_DEV_SESSEND);
	val |= OMAP_CTRL_DEV_AVALID | OMAP_CTRL_DEV_VBUSVALID;
	writel(val, ctrl_usb->otghs_control);
}

/**
 * omap_control_usb_device_mode - set AVALID, VBUSVALID and ID pin in high
 * impedance
 * @ctrl_usb: struct omap_control_usb *
 *
 * Writes to the mailbox register to notify the usb core that it has been
 * connected to a usb host.
 */
static void omap_control_usb_device_mode(struct omap_control_usb *ctrl_usb)
{
	u32 val;

	val = readl(ctrl_usb->otghs_control);
	val &= ~OMAP_CTRL_DEV_SESSEND;
	val |= OMAP_CTRL_DEV_IDDIG | OMAP_CTRL_DEV_AVALID |
		OMAP_CTRL_DEV_VBUSVALID;
	writel(val, ctrl_usb->otghs_control);
}

/**
 * omap_control_usb_set_sessionend - Enable SESSIONEND and IDIG to high
 * impedance
 * @ctrl_usb: struct omap_control_usb *
 *
 * Writes to the mailbox register to notify the usb core it's now in
 * disconnected state.
 */
static void omap_control_usb_set_sessionend(struct omap_control_usb *ctrl_usb)
{
	u32 val;

	val = readl(ctrl_usb->otghs_control);
	val &= ~(OMAP_CTRL_DEV_AVALID | OMAP_CTRL_DEV_VBUSVALID);
	val |= OMAP_CTRL_DEV_IDDIG | OMAP_CTRL_DEV_SESSEND;
	writel(val, ctrl_usb->otghs_control);
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
	struct omap_control_usb	*ctrl_usb;

	if (IS_ERR(dev) || !dev)
		return;

	ctrl_usb = dev_get_drvdata(dev);

	if (!ctrl_usb) {
		dev_err(dev, "Invalid control usb device\n");
		return;
	}

	if (ctrl_usb->type != OMAP_CTRL_TYPE_OTGHS)
		return;

	switch (mode) {
	case USB_MODE_HOST:
		omap_control_usb_host_mode(ctrl_usb);
		break;
	case USB_MODE_DEVICE:
		omap_control_usb_device_mode(ctrl_usb);
		break;
	case USB_MODE_DISCONNECT:
		omap_control_usb_set_sessionend(ctrl_usb);
		break;
	default:
		dev_vdbg(dev, "invalid omap control usb mode\n");
	}
}
EXPORT_SYMBOL_GPL(omap_control_usb_set_mode);

#ifdef CONFIG_OF

static const enum omap_control_usb_type otghs_data = OMAP_CTRL_TYPE_OTGHS;
static const enum omap_control_usb_type usb2_data = OMAP_CTRL_TYPE_USB2;
static const enum omap_control_usb_type pipe3_data = OMAP_CTRL_TYPE_PIPE3;
static const enum omap_control_usb_type dra7usb2_data = OMAP_CTRL_TYPE_DRA7USB2;

static const struct of_device_id omap_control_usb_id_table[] = {
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
		.compatible = "ti,control-phy-dra7usb2",
		.data = &dra7usb2_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_control_usb_id_table);
#endif


static int omap_control_usb_probe(struct platform_device *pdev)
{
	struct resource	*res;
	const struct of_device_id *of_id;
	struct omap_control_usb *control_usb;

	of_id = of_match_device(of_match_ptr(omap_control_usb_id_table),
								&pdev->dev);
	if (!of_id)
		return -EINVAL;

	control_usb = devm_kzalloc(&pdev->dev, sizeof(*control_usb),
		GFP_KERNEL);
	if (!control_usb) {
		dev_err(&pdev->dev, "unable to alloc memory for control usb\n");
		return -ENOMEM;
	}

	control_usb->dev = &pdev->dev;
	control_usb->type = *(enum omap_control_usb_type *)of_id->data;

	if (control_usb->type == OMAP_CTRL_TYPE_OTGHS) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
			"otghs_control");
		control_usb->otghs_control = devm_ioremap_resource(
			&pdev->dev, res);
		if (IS_ERR(control_usb->otghs_control))
			return PTR_ERR(control_usb->otghs_control);
	} else {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
				"power");
		control_usb->power = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(control_usb->power)) {
			dev_err(&pdev->dev, "Couldn't get power register\n");
			return PTR_ERR(control_usb->power);
		}
	}

	if (control_usb->type == OMAP_CTRL_TYPE_PIPE3) {
		control_usb->sys_clk = devm_clk_get(control_usb->dev,
			"sys_clkin");
		if (IS_ERR(control_usb->sys_clk)) {
			pr_err("%s: unable to get sys_clkin\n", __func__);
			return -EINVAL;
		}
	}

	dev_set_drvdata(control_usb->dev, control_usb);

	return 0;
}

static struct platform_driver omap_control_usb_driver = {
	.probe		= omap_control_usb_probe,
	.driver		= {
		.name	= "omap-control-usb",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(omap_control_usb_id_table),
	},
};

static int __init omap_control_usb_init(void)
{
	return platform_driver_register(&omap_control_usb_driver);
}
subsys_initcall(omap_control_usb_init);

static void __exit omap_control_usb_exit(void)
{
	platform_driver_unregister(&omap_control_usb_driver);
}
module_exit(omap_control_usb_exit);

MODULE_ALIAS("platform: omap_control_usb");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP Control Module USB Driver");
MODULE_LICENSE("GPL v2");
