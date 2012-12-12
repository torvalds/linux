/*
 * omap-usb2.c - USB PHY, talking to musb controller in OMAP.
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
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
#include <linux/io.h>
#include <linux/usb/omap_usb.h>
#include <linux/usb/phy_companion.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

/**
 * omap_usb2_set_comparator - links the comparator present in the sytem with
 *	this phy
 * @comparator - the companion phy(comparator) for this phy
 *
 * The phy companion driver should call this API passing the phy_companion
 * filled with set_vbus and start_srp to be used by usb phy.
 *
 * For use by phy companion driver
 */
int omap_usb2_set_comparator(struct phy_companion *comparator)
{
	struct omap_usb	*phy;
	struct usb_phy	*x = usb_get_phy(USB_PHY_TYPE_USB2);

	if (IS_ERR(x))
		return -ENODEV;

	phy = phy_to_omapusb(x);
	phy->comparator = comparator;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_usb2_set_comparator);

/**
 * omap_usb_phy_power - power on/off the phy using control module reg
 * @phy: struct omap_usb *
 * @on: 0 or 1, based on powering on or off the PHY
 *
 * XXX: Remove this function once control module driver gets merged
 */
static void omap_usb_phy_power(struct omap_usb *phy, int on)
{
	u32 val;

	if (on) {
		val = readl(phy->control_dev);
		if (val & PHY_PD) {
			writel(~PHY_PD, phy->control_dev);
			/* XXX: add proper documentation for this delay */
			mdelay(200);
		}
	} else {
		writel(PHY_PD, phy->control_dev);
	}
}

static int omap_usb_set_vbus(struct usb_otg *otg, bool enabled)
{
	struct omap_usb *phy = phy_to_omapusb(otg->phy);

	if (!phy->comparator)
		return -ENODEV;

	return phy->comparator->set_vbus(phy->comparator, enabled);
}

static int omap_usb_start_srp(struct usb_otg *otg)
{
	struct omap_usb *phy = phy_to_omapusb(otg->phy);

	if (!phy->comparator)
		return -ENODEV;

	return phy->comparator->start_srp(phy->comparator);
}

static int omap_usb_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct usb_phy	*phy = otg->phy;

	otg->host = host;
	if (!host)
		phy->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int omap_usb_set_peripheral(struct usb_otg *otg,
		struct usb_gadget *gadget)
{
	struct usb_phy	*phy = otg->phy;

	otg->gadget = gadget;
	if (!gadget)
		phy->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int omap_usb2_suspend(struct usb_phy *x, int suspend)
{
	u32 ret;
	struct omap_usb *phy = phy_to_omapusb(x);

	if (suspend && !phy->is_suspended) {
		omap_usb_phy_power(phy, 0);
		pm_runtime_put_sync(phy->dev);
		phy->is_suspended = 1;
	} else if (!suspend && phy->is_suspended) {
		ret = pm_runtime_get_sync(phy->dev);
		if (ret < 0) {
			dev_err(phy->dev, "get_sync failed with err %d\n",
									ret);
			return ret;
		}
		omap_usb_phy_power(phy, 1);
		phy->is_suspended = 0;
	}

	return 0;
}

static int __devinit omap_usb2_probe(struct platform_device *pdev)
{
	struct omap_usb			*phy;
	struct usb_otg			*otg;
	struct resource			*res;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy) {
		dev_err(&pdev->dev, "unable to allocate memory for USB2 PHY\n");
		return -ENOMEM;
	}

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg) {
		dev_err(&pdev->dev, "unable to allocate memory for USB OTG\n");
		return -ENOMEM;
	}

	phy->dev		= &pdev->dev;

	phy->phy.dev		= phy->dev;
	phy->phy.label		= "omap-usb2";
	phy->phy.set_suspend	= omap_usb2_suspend;
	phy->phy.otg		= otg;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	phy->control_dev = devm_request_and_ioremap(&pdev->dev, res);
	if (phy->control_dev == NULL) {
		dev_err(&pdev->dev, "Failed to obtain io memory\n");
		return -ENXIO;
	}

	phy->is_suspended	= 1;
	omap_usb_phy_power(phy, 0);

	otg->set_host		= omap_usb_set_host;
	otg->set_peripheral	= omap_usb_set_peripheral;
	otg->set_vbus		= omap_usb_set_vbus;
	otg->start_srp		= omap_usb_start_srp;
	otg->phy		= &phy->phy;

	phy->wkupclk = devm_clk_get(phy->dev, "usb_phy_cm_clk32k");
	if (IS_ERR(phy->wkupclk)) {
		dev_err(&pdev->dev, "unable to get usb_phy_cm_clk32k\n");
		return PTR_ERR(phy->wkupclk);
	}
	clk_prepare(phy->wkupclk);

	usb_add_phy(&phy->phy, USB_PHY_TYPE_USB2);

	platform_set_drvdata(pdev, phy);

	pm_runtime_enable(phy->dev);

	return 0;
}

static int __devexit omap_usb2_remove(struct platform_device *pdev)
{
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	clk_unprepare(phy->wkupclk);
	usb_remove_phy(&phy->phy);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME

static int omap_usb2_runtime_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	clk_disable(phy->wkupclk);

	return 0;
}

static int omap_usb2_runtime_resume(struct device *dev)
{
	u32 ret = 0;
	struct platform_device	*pdev = to_platform_device(dev);
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	ret = clk_enable(phy->wkupclk);
	if (ret < 0)
		dev_err(phy->dev, "Failed to enable wkupclk %d\n", ret);

	return ret;
}

static const struct dev_pm_ops omap_usb2_pm_ops = {
	SET_RUNTIME_PM_OPS(omap_usb2_runtime_suspend, omap_usb2_runtime_resume,
		NULL)
};

#define DEV_PM_OPS     (&omap_usb2_pm_ops)
#else
#define DEV_PM_OPS     NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id omap_usb2_id_table[] = {
	{ .compatible = "ti,omap-usb2" },
	{}
};
MODULE_DEVICE_TABLE(of, omap_usb2_id_table);
#endif

static struct platform_driver omap_usb2_driver = {
	.probe		= omap_usb2_probe,
	.remove		= __devexit_p(omap_usb2_remove),
	.driver		= {
		.name	= "omap-usb2",
		.owner	= THIS_MODULE,
		.pm	= DEV_PM_OPS,
		.of_match_table = of_match_ptr(omap_usb2_id_table),
	},
};

module_platform_driver(omap_usb2_driver);

MODULE_ALIAS("platform: omap_usb2");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP USB2 phy driver");
MODULE_LICENSE("GPL v2");
