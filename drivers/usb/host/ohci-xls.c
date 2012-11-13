/*
 * OHCI HCD for Netlogic XLS processors.
 *
 * (C) Copyright 2011 Netlogic Microsystems Inc.
 *
 *  Based on ohci-au1xxx.c, and other Linux OHCI drivers.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/platform_device.h>
#include <linux/signal.h>

static int ohci_xls_probe_internal(const struct hc_driver *driver,
			struct platform_device *dev)
{
	struct resource *res;
	struct usb_hcd *hcd;
	int retval, irq;

	/* Get our IRQ from an earlier registered Platform Resource */
	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "Found HC with no IRQ\n");
		return -ENODEV;
	}

	/* Get our Memory Handle */
	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&dev->dev, "MMIO Handle incorrect!\n");
		return -ENODEV;
	}

	hcd = usb_create_hcd(driver, &dev->dev, "XLS");
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
			driver->description)) {
		dev_dbg(&dev->dev, "Controller already in use\n");
		retval = -EBUSY;
		goto err2;
	}

	hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
	if (hcd->regs == NULL) {
		dev_dbg(&dev->dev, "error mapping memory\n");
		retval = -EFAULT;
		goto err3;
	}

	retval = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (retval != 0)
		goto err4;
	return retval;

err4:
	iounmap(hcd->regs);
err3:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err2:
	usb_put_hcd(hcd);
err1:
	dev_err(&dev->dev, "init fail, %d\n", retval);
	return retval;
}

static int ohci_xls_reset(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_hcd_init(ohci);
	return ohci_init(ohci);
}

static int __devinit ohci_xls_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci;
	int ret;

	ohci = hcd_to_ohci(hcd);
	ret = ohci_run(ohci);
	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start %s\n",
			hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}
	return 0;
}

static struct hc_driver ohci_xls_hc_driver = {
	.description	= hcd_name,
	.product_desc	= "XLS OHCI Host Controller",
	.hcd_priv_size	= sizeof(struct ohci_hcd),
	.irq		= ohci_irq,
	.flags		= HCD_MEMORY | HCD_USB11,
	.reset		= ohci_xls_reset,
	.start		= ohci_xls_start,
	.stop		= ohci_stop,
	.shutdown	= ohci_shutdown,
	.urb_enqueue	= ohci_urb_enqueue,
	.urb_dequeue	= ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,
	.get_frame_number = ohci_get_frame,
	.hub_status_data = ohci_hub_status_data,
	.hub_control	= ohci_hub_control,
#ifdef CONFIG_PM
	.bus_suspend	= ohci_bus_suspend,
	.bus_resume	= ohci_bus_resume,
#endif
	.start_port_reset = ohci_start_port_reset,
};

static int ohci_xls_probe(struct platform_device *dev)
{
	int ret;

	pr_debug("In ohci_xls_probe");
	if (usb_disabled())
		return -ENODEV;
	ret = ohci_xls_probe_internal(&ohci_xls_hc_driver, dev);
	return ret;
}

static int ohci_xls_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	return 0;
}

static struct platform_driver ohci_xls_driver = {
	.probe		= ohci_xls_probe,
	.remove		= ohci_xls_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "ohci-xls-0",
		.owner	= THIS_MODULE,
	},
};
