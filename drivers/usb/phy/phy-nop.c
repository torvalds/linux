/*
 * drivers/usb/otg/nop-usb-xceiv.c
 *
 * NOP USB transceiver for all USB transceiver which are either built-in
 * into USB IP or which are mostly autonomous.
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Ajay Kumar Gupta <ajay.gupta@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Current status:
 *	This provides a "nop" transceiver for PHYs which are
 *	autonomous such as isp1504, isp1707, etc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>

struct nop_usb_xceiv {
	struct usb_phy phy;
	struct device *dev;
	struct clk *clk;
	struct regulator *vcc;
	struct regulator *reset;
};

static struct platform_device *pd;

void usb_nop_xceiv_register(void)
{
	if (pd)
		return;
	pd = platform_device_register_simple("nop_usb_xceiv", -1, NULL, 0);
	if (!pd) {
		printk(KERN_ERR "Unable to register usb nop transceiver\n");
		return;
	}
}
EXPORT_SYMBOL(usb_nop_xceiv_register);

void usb_nop_xceiv_unregister(void)
{
	platform_device_unregister(pd);
	pd = NULL;
}
EXPORT_SYMBOL(usb_nop_xceiv_unregister);

static int nop_set_suspend(struct usb_phy *x, int suspend)
{
	return 0;
}

static int nop_init(struct usb_phy *phy)
{
	struct nop_usb_xceiv *nop = dev_get_drvdata(phy->dev);

	if (!IS_ERR(nop->vcc)) {
		if (regulator_enable(nop->vcc))
			dev_err(phy->dev, "Failed to enable power\n");
	}

	if (!IS_ERR(nop->clk))
		clk_enable(nop->clk);

	if (!IS_ERR(nop->reset)) {
		/* De-assert RESET */
		if (regulator_enable(nop->reset))
			dev_err(phy->dev, "Failed to de-assert reset\n");
	}

	return 0;
}

static void nop_shutdown(struct usb_phy *phy)
{
	struct nop_usb_xceiv *nop = dev_get_drvdata(phy->dev);

	if (!IS_ERR(nop->reset)) {
		/* Assert RESET */
		if (regulator_disable(nop->reset))
			dev_err(phy->dev, "Failed to assert reset\n");
	}

	if (!IS_ERR(nop->clk))
		clk_disable(nop->clk);

	if (!IS_ERR(nop->vcc)) {
		if (regulator_disable(nop->vcc))
			dev_err(phy->dev, "Failed to disable power\n");
	}
}

static int nop_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	if (!otg)
		return -ENODEV;

	if (!gadget) {
		otg->gadget = NULL;
		return -ENODEV;
	}

	otg->gadget = gadget;
	otg->phy->state = OTG_STATE_B_IDLE;
	return 0;
}

static int nop_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	if (!otg)
		return -ENODEV;

	if (!host) {
		otg->host = NULL;
		return -ENODEV;
	}

	otg->host = host;
	return 0;
}

static int nop_usb_xceiv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nop_usb_xceiv_platform_data *pdata = pdev->dev.platform_data;
	struct nop_usb_xceiv	*nop;
	enum usb_phy_type	type = USB_PHY_TYPE_USB2;
	int err;
	u32 clk_rate = 0;
	bool needs_vcc = false;
	bool needs_reset = false;

	nop = devm_kzalloc(&pdev->dev, sizeof(*nop), GFP_KERNEL);
	if (!nop)
		return -ENOMEM;

	nop->phy.otg = devm_kzalloc(&pdev->dev, sizeof(*nop->phy.otg),
							GFP_KERNEL);
	if (!nop->phy.otg)
		return -ENOMEM;

	if (dev->of_node) {
		struct device_node *node = dev->of_node;

		if (of_property_read_u32(node, "clock-frequency", &clk_rate))
			clk_rate = 0;

		needs_vcc = of_property_read_bool(node, "vcc-supply");
		needs_reset = of_property_read_bool(node, "reset-supply");

	} else if (pdata) {
		type = pdata->type;
		clk_rate = pdata->clk_rate;
		needs_vcc = pdata->needs_vcc;
		needs_reset = pdata->needs_reset;
	}

	nop->clk = devm_clk_get(&pdev->dev, "main_clk");
	if (IS_ERR(nop->clk)) {
		dev_dbg(&pdev->dev, "Can't get phy clock: %ld\n",
					PTR_ERR(nop->clk));
	}

	if (!IS_ERR(nop->clk) && clk_rate) {
		err = clk_set_rate(nop->clk, clk_rate);
		if (err) {
			dev_err(&pdev->dev, "Error setting clock rate\n");
			return err;
		}
	}

	if (!IS_ERR(nop->clk)) {
		err = clk_prepare(nop->clk);
		if (err) {
			dev_err(&pdev->dev, "Error preparing clock\n");
			return err;
		}
	}

	nop->vcc = devm_regulator_get(&pdev->dev, "vcc");
	if (IS_ERR(nop->vcc)) {
		dev_dbg(&pdev->dev, "Error getting vcc regulator: %ld\n",
					PTR_ERR(nop->vcc));
		if (needs_vcc)
			return -EPROBE_DEFER;
	}

	nop->reset = devm_regulator_get(&pdev->dev, "reset");
	if (IS_ERR(nop->reset)) {
		dev_dbg(&pdev->dev, "Error getting reset regulator: %ld\n",
					PTR_ERR(nop->reset));
		if (needs_reset)
			return -EPROBE_DEFER;
	}

	nop->dev		= &pdev->dev;
	nop->phy.dev		= nop->dev;
	nop->phy.label		= "nop-xceiv";
	nop->phy.set_suspend	= nop_set_suspend;
	nop->phy.init		= nop_init;
	nop->phy.shutdown	= nop_shutdown;
	nop->phy.state		= OTG_STATE_UNDEFINED;
	nop->phy.type		= type;

	nop->phy.otg->phy		= &nop->phy;
	nop->phy.otg->set_host		= nop_set_host;
	nop->phy.otg->set_peripheral	= nop_set_peripheral;

	err = usb_add_phy_dev(&nop->phy);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		goto err_add;
	}

	platform_set_drvdata(pdev, nop);

	ATOMIC_INIT_NOTIFIER_HEAD(&nop->phy.notifier);

	return 0;

err_add:
	if (!IS_ERR(nop->clk))
		clk_unprepare(nop->clk);
	return err;
}

static int nop_usb_xceiv_remove(struct platform_device *pdev)
{
	struct nop_usb_xceiv *nop = platform_get_drvdata(pdev);

	if (!IS_ERR(nop->clk))
		clk_unprepare(nop->clk);

	usb_remove_phy(&nop->phy);

	return 0;
}

static const struct of_device_id nop_xceiv_dt_ids[] = {
	{ .compatible = "usb-nop-xceiv" },
	{ }
};

MODULE_DEVICE_TABLE(of, nop_xceiv_dt_ids);

static struct platform_driver nop_usb_xceiv_driver = {
	.probe		= nop_usb_xceiv_probe,
	.remove		= nop_usb_xceiv_remove,
	.driver		= {
		.name	= "nop_usb_xceiv",
		.owner	= THIS_MODULE,
		.of_match_table = nop_xceiv_dt_ids,
	},
};

static int __init nop_usb_xceiv_init(void)
{
	return platform_driver_register(&nop_usb_xceiv_driver);
}
subsys_initcall(nop_usb_xceiv_init);

static void __exit nop_usb_xceiv_exit(void)
{
	platform_driver_unregister(&nop_usb_xceiv_driver);
}
module_exit(nop_usb_xceiv_exit);

MODULE_ALIAS("platform:nop_usb_xceiv");
MODULE_AUTHOR("Texas Instruments Inc");
MODULE_DESCRIPTION("NOP USB Transceiver driver");
MODULE_LICENSE("GPL");
