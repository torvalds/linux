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
#include <linux/phy/omap_usb.h>
#include <linux/usb/phy_companion.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/phy/omap_control_phy.h>
#include <linux/phy/phy.h>
#include <linux/of_platform.h>

#define USB2PHY_DISCON_BYP_LATCH (1 << 31)
#define USB2PHY_ANA_CONFIG1 0x4c

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

static int omap_usb_set_vbus(struct usb_otg *otg, bool enabled)
{
	struct omap_usb *phy = phy_to_omapusb(otg->usb_phy);

	if (!phy->comparator)
		return -ENODEV;

	return phy->comparator->set_vbus(phy->comparator, enabled);
}

static int omap_usb_start_srp(struct usb_otg *otg)
{
	struct omap_usb *phy = phy_to_omapusb(otg->usb_phy);

	if (!phy->comparator)
		return -ENODEV;

	return phy->comparator->start_srp(phy->comparator);
}

static int omap_usb_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	otg->host = host;
	if (!host)
		otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int omap_usb_set_peripheral(struct usb_otg *otg,
		struct usb_gadget *gadget)
{
	otg->gadget = gadget;
	if (!gadget)
		otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int omap_usb_power_off(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);

	omap_control_phy_power(phy->control_dev, 0);

	return 0;
}

static int omap_usb_power_on(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);

	omap_control_phy_power(phy->control_dev, 1);

	return 0;
}

static int omap_usb_init(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);
	u32 val;

	if (phy->flags & OMAP_USB2_CALIBRATE_FALSE_DISCONNECT) {
		/*
		 *
		 * Reduce the sensitivity of internal PHY by enabling the
		 * DISCON_BYP_LATCH of the USB2PHY_ANA_CONFIG1 register. This
		 * resolves issues with certain devices which can otherwise
		 * be prone to false disconnects.
		 *
		 */
		val = omap_usb_readl(phy->phy_base, USB2PHY_ANA_CONFIG1);
		val |= USB2PHY_DISCON_BYP_LATCH;
		omap_usb_writel(phy->phy_base, USB2PHY_ANA_CONFIG1, val);
	}

	return 0;
}

static struct phy_ops ops = {
	.init		= omap_usb_init,
	.power_on	= omap_usb_power_on,
	.power_off	= omap_usb_power_off,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_OF
static const struct usb_phy_data omap_usb2_data = {
	.label = "omap_usb2",
	.flags = OMAP_USB2_HAS_START_SRP | OMAP_USB2_HAS_SET_VBUS,
};

static const struct usb_phy_data omap5_usb2_data = {
	.label = "omap5_usb2",
	.flags = 0,
};

static const struct usb_phy_data dra7x_usb2_data = {
	.label = "dra7x_usb2",
	.flags = OMAP_USB2_CALIBRATE_FALSE_DISCONNECT,
};

static const struct usb_phy_data am437x_usb2_data = {
	.label = "am437x_usb2",
	.flags =  0,
};

static const struct of_device_id omap_usb2_id_table[] = {
	{
		.compatible = "ti,omap-usb2",
		.data = &omap_usb2_data,
	},
	{
		.compatible = "ti,omap5-usb2",
		.data = &omap5_usb2_data,
	},
	{
		.compatible = "ti,dra7x-usb2",
		.data = &dra7x_usb2_data,
	},
	{
		.compatible = "ti,am437x-usb2",
		.data = &am437x_usb2_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_usb2_id_table);
#endif

static int omap_usb2_probe(struct platform_device *pdev)
{
	struct omap_usb	*phy;
	struct phy *generic_phy;
	struct resource *res;
	struct phy_provider *phy_provider;
	struct usb_otg *otg;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *control_node;
	struct platform_device *control_pdev;
	const struct of_device_id *of_id;
	struct usb_phy_data *phy_data;

	of_id = of_match_device(of_match_ptr(omap_usb2_id_table), &pdev->dev);

	if (!of_id)
		return -EINVAL;

	phy_data = (struct usb_phy_data *)of_id->data;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	phy->dev		= &pdev->dev;

	phy->phy.dev		= phy->dev;
	phy->phy.label		= phy_data->label;
	phy->phy.otg		= otg;
	phy->phy.type		= USB_PHY_TYPE_USB2;

	if (phy_data->flags & OMAP_USB2_CALIBRATE_FALSE_DISCONNECT) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		phy->phy_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(phy->phy_base))
			return PTR_ERR(phy->phy_base);
		phy->flags |= OMAP_USB2_CALIBRATE_FALSE_DISCONNECT;
	}

	control_node = of_parse_phandle(node, "ctrl-module", 0);
	if (!control_node) {
		dev_err(&pdev->dev, "Failed to get control device phandle\n");
		return -EINVAL;
	}

	control_pdev = of_find_device_by_node(control_node);
	if (!control_pdev) {
		dev_err(&pdev->dev, "Failed to get control device\n");
		return -EINVAL;
	}

	phy->control_dev = &control_pdev->dev;
	omap_control_phy_power(phy->control_dev, 0);

	otg->set_host		= omap_usb_set_host;
	otg->set_peripheral	= omap_usb_set_peripheral;
	if (phy_data->flags & OMAP_USB2_HAS_SET_VBUS)
		otg->set_vbus		= omap_usb_set_vbus;
	if (phy_data->flags & OMAP_USB2_HAS_START_SRP)
		otg->start_srp		= omap_usb_start_srp;
	otg->usb_phy		= &phy->phy;

	platform_set_drvdata(pdev, phy);
	pm_runtime_enable(phy->dev);

	generic_phy = devm_phy_create(phy->dev, NULL, &ops);
	if (IS_ERR(generic_phy)) {
		pm_runtime_disable(phy->dev);
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, phy);

	phy_provider = devm_of_phy_provider_register(phy->dev,
			of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		pm_runtime_disable(phy->dev);
		return PTR_ERR(phy_provider);
	}

	phy->wkupclk = devm_clk_get(phy->dev, "wkupclk");
	if (IS_ERR(phy->wkupclk)) {
		dev_warn(&pdev->dev, "unable to get wkupclk, trying old name\n");
		phy->wkupclk = devm_clk_get(phy->dev, "usb_phy_cm_clk32k");
		if (IS_ERR(phy->wkupclk)) {
			dev_err(&pdev->dev, "unable to get usb_phy_cm_clk32k\n");
			return PTR_ERR(phy->wkupclk);
		} else {
			dev_warn(&pdev->dev,
				 "found usb_phy_cm_clk32k, please fix DTS\n");
		}
	}
	clk_prepare(phy->wkupclk);

	phy->optclk = devm_clk_get(phy->dev, "refclk");
	if (IS_ERR(phy->optclk)) {
		dev_dbg(&pdev->dev, "unable to get refclk, trying old name\n");
		phy->optclk = devm_clk_get(phy->dev, "usb_otg_ss_refclk960m");
		if (IS_ERR(phy->optclk)) {
			dev_dbg(&pdev->dev,
				"unable to get usb_otg_ss_refclk960m\n");
		} else {
			dev_warn(&pdev->dev,
				 "found usb_otg_ss_refclk960m, please fix DTS\n");
		}
	} else {
		clk_prepare(phy->optclk);
	}

	usb_add_phy_dev(&phy->phy);

	return 0;
}

static int omap_usb2_remove(struct platform_device *pdev)
{
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	clk_unprepare(phy->wkupclk);
	if (!IS_ERR(phy->optclk))
		clk_unprepare(phy->optclk);
	usb_remove_phy(&phy->phy);
	pm_runtime_disable(phy->dev);

	return 0;
}

#ifdef CONFIG_PM

static int omap_usb2_runtime_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	clk_disable(phy->wkupclk);
	if (!IS_ERR(phy->optclk))
		clk_disable(phy->optclk);

	return 0;
}

static int omap_usb2_runtime_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct omap_usb	*phy = platform_get_drvdata(pdev);
	int ret;

	ret = clk_enable(phy->wkupclk);
	if (ret < 0) {
		dev_err(phy->dev, "Failed to enable wkupclk %d\n", ret);
		goto err0;
	}

	if (!IS_ERR(phy->optclk)) {
		ret = clk_enable(phy->optclk);
		if (ret < 0) {
			dev_err(phy->dev, "Failed to enable optclk %d\n", ret);
			goto err1;
		}
	}

	return 0;

err1:
	clk_disable(phy->wkupclk);

err0:
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

static struct platform_driver omap_usb2_driver = {
	.probe		= omap_usb2_probe,
	.remove		= omap_usb2_remove,
	.driver		= {
		.name	= "omap-usb2",
		.pm	= DEV_PM_OPS,
		.of_match_table = of_match_ptr(omap_usb2_id_table),
	},
};

module_platform_driver(omap_usb2_driver);

MODULE_ALIAS("platform:omap_usb2");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP USB2 phy driver");
MODULE_LICENSE("GPL v2");
