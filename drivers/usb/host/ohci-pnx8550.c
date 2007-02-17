/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 * (C) Copyright 2005 Embedded Alley Solutions, Inc.
 *
 * Bus Glue for PNX8550
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Russell King et al.
 *
 * Modified for LH7A404 from ohci-sa1111.c
 *  by Durgesh Pattamatta <pattamattad@sharpsec.com>
 *
 * Modified for PNX8550 from ohci-sa1111.c and sa-omap.c
 *  by Vitaly Wool <vitalywool@gmail.com>
 *
 * This file is licenced under the GPL.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <asm/mach-pnx8550/usb.h>
#include <asm/mach-pnx8550/int.h>
#include <asm/mach-pnx8550/pci.h>

#ifndef CONFIG_PNX8550
#error "This file is PNX8550 bus glue.  CONFIG_PNX8550 must be defined."
#endif

extern int usb_disabled(void);

/*-------------------------------------------------------------------------*/

static void pnx8550_start_hc(struct platform_device *dev)
{
	/*
	 * Set register CLK48CTL to enable and 48MHz
	 */
	outl(0x00000003, PCI_BASE | 0x0004770c);

	/*
	 * Set register CLK12CTL to enable and 48MHz
	 */
	outl(0x00000003, PCI_BASE | 0x00047710);

	udelay(100);
}

static void pnx8550_stop_hc(struct platform_device *dev)
{
	udelay(10);
}


/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


/**
 * usb_hcd_pnx8550_probe - initialize pnx8550-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
int usb_hcd_pnx8550_probe (const struct hc_driver *driver,
			  struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;

	if (dev->resource[0].flags != IORESOURCE_MEM ||
			dev->resource[1].flags != IORESOURCE_IRQ) {
		dev_err (&dev->dev,"invalid resource type\n");
		return -ENOMEM;
	}

	hcd = usb_create_hcd (driver, &dev->dev, "pnx8550");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = dev->resource[0].start;
	hcd->rsrc_len = dev->resource[0].end - dev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_err(&dev->dev, "request_mem_region [0x%08llx, 0x%08llx] "
				"failed\n", hcd->rsrc_start, hcd->rsrc_len);
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&dev->dev, "ioremap [[0x%08llx, 0x%08llx] failed\n",
				hcd->rsrc_start, hcd->rsrc_len);
		retval = -ENOMEM;
		goto err2;
	}

	pnx8550_start_hc(dev);

	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, dev->resource[1].start, IRQF_DISABLED);
	if (retval == 0)
		return retval;

	pnx8550_stop_hc(dev);
	iounmap(hcd->regs);
 err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
 err1:
	usb_put_hcd(hcd);
	return retval;
}


/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_pnx8550_remove - shutdown processing for pnx8550-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_pnx8550_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_hcd_pnx8550_remove (struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	pnx8550_stop_hc(dev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

/*-------------------------------------------------------------------------*/

static int __devinit
ohci_pnx8550_start (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

	ohci_dbg (ohci, "ohci_pnx8550_start, ohci:%p", ohci);

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run (ohci)) < 0) {
		err ("can't start %s", hcd->self.bus_name);
		ohci_stop (hcd);
		return ret;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_pnx8550_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"PNX8550 OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_pnx8550_start,
	.stop =			ohci_stop,

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
	.hub_irq_enable =	ohci_rhsc_enable,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

static int ohci_hcd_pnx8550_drv_probe(struct platform_device *pdev)
{
	int ret;

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_pnx8550_probe(&ohci_pnx8550_hc_driver, pdev);
	return ret;
}

static int ohci_hcd_pnx8550_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_pnx8550_remove(hcd, pdev);
	return 0;
}

MODULE_ALIAS("pnx8550-ohci");

static struct platform_driver ohci_hcd_pnx8550_driver = {
	.driver = {
		.name		= "pnx8550-ohci",
	},
	.probe		= ohci_hcd_pnx8550_drv_probe,
	.remove		= ohci_hcd_pnx8550_drv_remove,
};

