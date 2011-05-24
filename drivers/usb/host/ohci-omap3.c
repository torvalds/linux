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

#include <linux/platform_device.h>
#include <plat/usb.h>

/*-------------------------------------------------------------------------*/

static int ohci_omap3_init(struct usb_hcd *hcd)
{
	dev_dbg(hcd->self.controller, "starting OHCI controller\n");

	return ohci_init(hcd_to_ohci(hcd));
}

/*-------------------------------------------------------------------------*/

static int ohci_omap3_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	/*
	 * RemoteWakeupConnected has to be set explicitly before
	 * calling ohci_run. The reset value of RWC is 0.
	 */
	ohci->hc_control = OHCI_CTRL_RWC;
	writel(OHCI_CTRL_RWC, &ohci->regs->control);

	ret = ohci_run(ohci);

	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start\n");
		ohci_stop(hcd);
	}

	return ret;
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_omap3_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"OMAP3 OHCI Host Controller",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset =		ohci_omap3_init,
	.start =		ohci_omap3_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

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
static int __devinit ohci_hcd_omap3_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct usb_hcd		*hcd = NULL;
	void __iomem		*regs = NULL;
	struct resource		*res;
	int			ret = -ENODEV;
	int			irq;

	if (usb_disabled())
		goto err_end;

	if (!dev->parent) {
		dev_err(dev, "Missing parent device\n");
		return -ENODEV;
	}

	irq = platform_get_irq_byname(pdev, "ohci-irq");
	if (irq < 0) {
		dev_err(dev, "OHCI irq failed\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev,
				IORESOURCE_MEM, "ohci");
	if (!ret) {
		dev_err(dev, "UHH OHCI get resource failed\n");
		return -ENOMEM;
	}

	regs = ioremap(res->start, resource_size(res));
	if (!regs) {
		dev_err(dev, "UHH OHCI ioremap failed\n");
		return -ENOMEM;
	}


	hcd = usb_create_hcd(&ohci_omap3_hc_driver, dev,
			dev_name(dev));
	if (!hcd) {
		dev_err(dev, "usb_create_hcd failed\n");
		goto err_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs =  regs;

	ret = omap_usbhs_enable(dev);
	if (ret) {
		dev_dbg(dev, "failed to start ohci\n");
		goto err_end;
	}

	ohci_hcd_init(hcd_to_ohci(hcd));

	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED);
	if (ret) {
		dev_dbg(dev, "failed to add hcd with err %d\n", ret);
		goto err_add_hcd;
	}

	return 0;

err_add_hcd:
	omap_usbhs_disable(dev);

err_end:
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
static int __devexit ohci_hcd_omap3_remove(struct platform_device *pdev)
{
	struct device *dev	= &pdev->dev;
	struct usb_hcd *hcd	= dev_get_drvdata(dev);

	iounmap(hcd->regs);
	usb_remove_hcd(hcd);
	omap_usbhs_disable(dev);
	usb_put_hcd(hcd);

	return 0;
}

static void ohci_hcd_omap3_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = dev_get_drvdata(&pdev->dev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver ohci_hcd_omap3_driver = {
	.probe		= ohci_hcd_omap3_probe,
	.remove		= __devexit_p(ohci_hcd_omap3_remove),
	.shutdown	= ohci_hcd_omap3_shutdown,
	.driver		= {
		.name	= "ohci-omap3",
	},
};

MODULE_ALIAS("platform:ohci-omap3");
MODULE_AUTHOR("Anand Gadiyar <gadiyar@ti.com>");
