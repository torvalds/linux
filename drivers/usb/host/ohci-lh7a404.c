/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 *
 * Bus Glue for Sharp LH7A404
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Rusell King et al.
 *
 * Modified for LH7A404 from ohci-sa1111.c
 *  by Durgesh Pattamatta <pattamattad@sharpsec.com>
 *
 * This file is licenced under the GPL.
 */

#include <linux/platform_device.h>
#include <linux/signal.h>

#include <asm/hardware.h>


extern int usb_disabled(void);

/*-------------------------------------------------------------------------*/

static void lh7a404_start_hc(struct platform_device *dev)
{
	printk(KERN_DEBUG __FILE__
	       ": starting LH7A404 OHCI USB Controller\n");

	/*
	 * Now, carefully enable the USB clock, and take
	 * the USB host controller out of reset.
	 */
	CSC_PWRCNT |= CSC_PWRCNT_USBH_EN; /* Enable clock */
	udelay(1000);
	USBH_CMDSTATUS = OHCI_HCR;

	printk(KERN_DEBUG __FILE__
		   ": Clock to USB host has been enabled \n");
}

static void lh7a404_stop_hc(struct platform_device *dev)
{
	printk(KERN_DEBUG __FILE__
	       ": stopping LH7A404 OHCI USB Controller\n");

	CSC_PWRCNT &= ~CSC_PWRCNT_USBH_EN; /* Disable clock */
}


/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */


/**
 * usb_hcd_lh7a404_probe - initialize LH7A404-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
int usb_hcd_lh7a404_probe (const struct hc_driver *driver,
			  struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;

	if (dev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		return -ENOMEM;
	}

	hcd = usb_create_hcd(driver, &dev->dev, "lh7a404");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = dev->resource[0].start;
	hcd->rsrc_len = dev->resource[0].end - dev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_debug("request_mem_region failed");
		retval = -EBUSY;
		goto err1;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err2;
	}

	lh7a404_start_hc(dev);
	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, dev->resource[1].start, IRQF_DISABLED);
	if (retval == 0)
		return retval;

	lh7a404_stop_hc(dev);
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
 * usb_hcd_lh7a404_remove - shutdown processing for LH7A404-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_lh7a404_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_hcd_lh7a404_remove (struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	lh7a404_stop_hc(dev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

/*-------------------------------------------------------------------------*/

static int __devinit
ohci_lh7a404_start (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

	ohci_dbg (ohci, "ohci_lh7a404_start, ohci:%p", ohci);
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

static const struct hc_driver ohci_lh7a404_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"LH7A404 OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_lh7a404_start,
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
	.hub_irq_enable =	ohci_rhsc_enable,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

static int ohci_hcd_lh7a404_drv_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug ("In ohci_hcd_lh7a404_drv_probe");

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_lh7a404_probe(&ohci_lh7a404_hc_driver, pdev);
	return ret;
}

static int ohci_hcd_lh7a404_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_lh7a404_remove(hcd, pdev);
	return 0;
}
	/*TBD*/
/*static int ohci_hcd_lh7a404_drv_suspend(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);

	return 0;
}
static int ohci_hcd_lh7a404_drv_resume(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);


	return 0;
}
*/

static struct platform_driver ohci_hcd_lh7a404_driver = {
	.probe		= ohci_hcd_lh7a404_drv_probe,
	.remove		= ohci_hcd_lh7a404_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	/*.suspend	= ohci_hcd_lh7a404_drv_suspend, */
	/*.resume	= ohci_hcd_lh7a404_drv_resume, */
	.driver		= {
		.name	= "lh7a404-ohci",
		.owner	= THIS_MODULE,
	},
};

MODULE_ALIAS("platform:lh7a404-ohci");
