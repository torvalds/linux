/*
 * ohci-omap3.c - driver for OHCI on OMAP3 and later processors
 *
 * Bus Glue for OMAP3 USBHOST 3 port OHCI controller
 * This controller is also used in later OMAPs and AM35x chips
 *
 * Copyright (C) 2007-2010 Texas Instruments, Inc.
 * Author: Vikram Pandita <vikram.pandita@ti.com>
 * Author: Anand Gadiyar <gadiyar@ti.com>
 * Author: Keshava Munegowda <keshava_mgowda@ti.com>
 *
 * Based on ehci-omap.c and some other ohci glue layers
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * TODO (last updated Feb 27, 2011):
 *	- add kernel-doc
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/usb/otg.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI OMAP3 driver"

static const char hcd_name[] = "ohci-omap3";
static struct hc_driver __read_mostly ohci_omap3_hc_driver;

/*
 * configure so an HC device and id are always provided
 * always called with process context; sleeping is OK
 */

/**
 * ohci_hcd_omap3_probe - initialize OMAP-based HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
static int ohci_hcd_omap3_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct ohci_hcd		*ohci;
	struct usb_hcd		*hcd = NULL;
	void __iomem		*regs = NULL;
	struct resource		*res;
	int			ret = -ENODEV;
	int			irq;

	if (usb_disabled())
		return -ENODEV;

	if (!dev->parent) {
		dev_err(dev, "Missing parent device\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "OHCI irq failed\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "UHH OHCI get resource failed\n");
		return -ENOMEM;
	}

	regs = ioremap(res->start, resource_size(res));
	if (!regs) {
		dev_err(dev, "UHH OHCI ioremap failed\n");
		return -ENOMEM;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	hcd = usb_create_hcd(&ohci_omap3_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "usb_create_hcd failed\n");
		goto err_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs =  regs;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	ohci = hcd_to_ohci(hcd);
	/*
	 * RemoteWakeupConnected has to be set explicitly before
	 * calling ohci_run. The reset value of RWC is 0.
	 */
	ohci->hc_control = OHCI_CTRL_RWC;

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret) {
		dev_dbg(dev, "failed to add hcd with err %d\n", ret);
		goto err_add_hcd;
	}

	return 0;

err_add_hcd:
	pm_runtime_put_sync(dev);
	usb_put_hcd(hcd);

err_io:
	iounmap(regs);

	return ret;
}

/*
 * may be called without controller electrically present
 * may be called with controller, bus, and devices active
 */

/**
 * ohci_hcd_omap3_remove - shutdown processing for OHCI HCDs
 * @pdev: USB Host Controller being removed
 *
 * Reverses the effect of ohci_hcd_omap3_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 */
static int ohci_hcd_omap3_remove(struct platform_device *pdev)
{
	struct device *dev	= &pdev->dev;
	struct usb_hcd *hcd	= dev_get_drvdata(dev);

	iounmap(hcd->regs);
	usb_remove_hcd(hcd);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	usb_put_hcd(hcd);
	return 0;
}

static const struct of_device_id omap_ohci_dt_ids[] = {
	{ .compatible = "ti,ohci-omap3" },
	{ }
};

MODULE_DEVICE_TABLE(of, omap_ohci_dt_ids);

static struct platform_driver ohci_hcd_omap3_driver = {
	.probe		= ohci_hcd_omap3_probe,
	.remove		= ohci_hcd_omap3_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "ohci-omap3",
		.of_match_table = omap_ohci_dt_ids,
	},
};

static int __init ohci_omap3_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ohci_init_driver(&ohci_omap3_hc_driver, NULL);
	return platform_driver_register(&ohci_hcd_omap3_driver);
}
module_init(ohci_omap3_init);

static void __exit ohci_omap3_cleanup(void)
{
	platform_driver_unregister(&ohci_hcd_omap3_driver);
}
module_exit(ohci_omap3_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_ALIAS("platform:ohci-omap3");
MODULE_AUTHOR("Anand Gadiyar <gadiyar@ti.com>");
MODULE_LICENSE("GPL");
