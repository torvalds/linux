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

struct nop_usb_xceiv {
	struct usb_phy		phy;
	struct device		*dev;
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

static int __devinit nop_usb_xceiv_probe(struct platform_device *pdev)
{
	struct nop_usb_xceiv_platform_data *pdata = pdev->dev.platform_data;
	struct nop_usb_xceiv	*nop;
	enum usb_phy_type	type = USB_PHY_TYPE_USB2;
	int err;

	nop = kzalloc(sizeof *nop, GFP_KERNEL);
	if (!nop)
		return -ENOMEM;

	nop->phy.otg = kzalloc(sizeof *nop->phy.otg, GFP_KERNEL);
	if (!nop->phy.otg) {
		kfree(nop);
		return -ENOMEM;
	}

	if (pdata)
		type = pdata->type;

	nop->dev		= &pdev->dev;
	nop->phy.dev		= nop->dev;
	nop->phy.label		= "nop-xceiv";
	nop->phy.set_suspend	= nop_set_suspend;
	nop->phy.state		= OTG_STATE_UNDEFINED;

	nop->phy.otg->phy		= &nop->phy;
	nop->phy.otg->set_host		= nop_set_host;
	nop->phy.otg->set_peripheral	= nop_set_peripheral;

	err = usb_add_phy(&nop->phy, type);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver, err: %d\n",
			err);
		goto exit;
	}

	platform_set_drvdata(pdev, nop);

	ATOMIC_INIT_NOTIFIER_HEAD(&nop->phy.notifier);

	return 0;
exit:
	kfree(nop->phy.otg);
	kfree(nop);
	return err;
}

static int __devexit nop_usb_xceiv_remove(struct platform_device *pdev)
{
	struct nop_usb_xceiv *nop = platform_get_drvdata(pdev);

	usb_remove_phy(&nop->phy);

	platform_set_drvdata(pdev, NULL);
	kfree(nop->phy.otg);
	kfree(nop);

	return 0;
}

static struct platform_driver nop_usb_xceiv_driver = {
	.probe		= nop_usb_xceiv_probe,
	.remove		= nop_usb_xceiv_remove,
	.driver		= {
		.name	= "nop_usb_xceiv",
		.owner	= THIS_MODULE,
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
