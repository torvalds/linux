/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 *
 * Bus Glue for ep93xx.
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Russell King et al.
 *
 * Modified for LH7A404 from ohci-sa1111.c
 *  by Durgesh Pattamatta <pattamattad@sharpsec.com>
 *
 * Modified for pxa27x from ohci-lh7a404.c
 *  by Nick Bane <nick@cecomputing.co.uk> 26-8-2004
 *
 * Modified for ep93xx from ohci-pxa27x.c
 *  by Lennert Buytenhek <buytenh@wantstofly.org> 28-2-2006
 *  Based on an earlier driver by Ray Lehtiniemi
 *
 * This file is licenced under the GPL.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/signal.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI EP93xx driver"

static const char hcd_name[] = "ohci-ep93xx";

static struct hc_driver __read_mostly ohci_ep93xx_hc_driver;

static struct clk *usb_host_clock;

static int ohci_hcd_ep93xx_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	hcd = usb_create_hcd(&ohci_ep93xx_hc_driver, &pdev->dev, "ep93xx");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err_put_hcd;
	}

	usb_host_clock = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(usb_host_clock)) {
		ret = PTR_ERR(usb_host_clock);
		goto err_put_hcd;
	}

	clk_enable(usb_host_clock);

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret)
		goto err_clk_disable;

	return 0;

err_clk_disable:
	clk_disable(usb_host_clock);
err_put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int ohci_hcd_ep93xx_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	clk_disable(usb_host_clock);
	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ohci_hcd_ep93xx_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	bool do_wakeup = device_may_wakeup(&pdev->dev);
	int ret;

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	ret = ohci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	ep93xx_stop_hc(&pdev->dev);

	return ret;
}

static int ohci_hcd_ep93xx_drv_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	clk_enable(usb_host_clock);

	ohci_resume(hcd, false);
	return 0;
}
#endif

static struct platform_driver ohci_hcd_ep93xx_driver = {
	.probe		= ohci_hcd_ep93xx_drv_probe,
	.remove		= ohci_hcd_ep93xx_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef CONFIG_PM
	.suspend	= ohci_hcd_ep93xx_drv_suspend,
	.resume		= ohci_hcd_ep93xx_drv_resume,
#endif
	.driver		= {
		.name	= "ep93xx-ohci",
		.owner	= THIS_MODULE,
	},
};

static int __init ohci_ep93xx_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ohci_init_driver(&ohci_ep93xx_hc_driver, NULL);
	return platform_driver_register(&ohci_hcd_ep93xx_driver);
}
module_init(ohci_ep93xx_init);

static void __exit ohci_ep93xx_cleanup(void)
{
	platform_driver_unregister(&ohci_hcd_ep93xx_driver);
}
module_exit(ohci_ep93xx_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-ohci");
