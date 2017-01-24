/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 *
 * SA1111 Bus Glue
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Russell King et al.
 *
 * This file is licenced under the GPL.
 */

#include <asm/mach-types.h>
#include <asm/hardware/sa1111.h>

#ifndef CONFIG_SA1111
#error "This file is SA-1111 bus glue.  CONFIG_SA1111 must be defined."
#endif

#define USB_STATUS	0x0118
#define USB_RESET	0x011c
#define USB_IRQTEST	0x0120

#define USB_RESET_FORCEIFRESET	(1 << 0)
#define USB_RESET_FORCEHCRESET	(1 << 1)
#define USB_RESET_CLKGENRESET	(1 << 2)
#define USB_RESET_SIMSCALEDOWN	(1 << 3)
#define USB_RESET_USBINTTEST	(1 << 4)
#define USB_RESET_SLEEPSTBYEN	(1 << 5)
#define USB_RESET_PWRSENSELOW	(1 << 6)
#define USB_RESET_PWRCTRLLOW	(1 << 7)

#define USB_STATUS_IRQHCIRMTWKUP  (1 <<  7)
#define USB_STATUS_IRQHCIBUFFACC  (1 <<  8)
#define USB_STATUS_NIRQHCIM       (1 <<  9)
#define USB_STATUS_NHCIMFCLR      (1 << 10)
#define USB_STATUS_USBPWRSENSE    (1 << 11)

#if 0
static void dump_hci_status(struct usb_hcd *hcd, const char *label)
{
	unsigned long status = sa1111_readl(hcd->regs + USB_STATUS);

	printk(KERN_DEBUG "%s USB_STATUS = { %s%s%s%s%s}\n", label,
	     ((status & USB_STATUS_IRQHCIRMTWKUP) ? "IRQHCIRMTWKUP " : ""),
	     ((status & USB_STATUS_IRQHCIBUFFACC) ? "IRQHCIBUFFACC " : ""),
	     ((status & USB_STATUS_NIRQHCIM) ? "" : "IRQHCIM "),
	     ((status & USB_STATUS_NHCIMFCLR) ? "" : "HCIMFCLR "),
	     ((status & USB_STATUS_USBPWRSENSE) ? "USBPWRSENSE " : ""));
}
#endif

static int ohci_sa1111_reset(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_hcd_init(ohci);
	return ohci_init(ohci);
}

static int ohci_sa1111_start(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	int ret;

	ret = ohci_run(ohci);
	if (ret < 0) {
		ohci_err(ohci, "can't start\n");
		ohci_stop(hcd);
	}
	return ret;
}

static const struct hc_driver ohci_sa1111_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"SA-1111 OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset =		ohci_sa1111_reset,
	.start =		ohci_sa1111_start,
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

static int sa1111_start_hc(struct sa1111_dev *dev)
{
	unsigned int usb_rst = 0;
	int ret;

	dev_dbg(&dev->dev, "starting SA-1111 OHCI USB Controller\n");

	if (machine_is_xp860() ||
	    machine_is_assabet() ||
	    machine_is_pfs168() ||
	    machine_is_badge4())
		usb_rst = USB_RESET_PWRSENSELOW | USB_RESET_PWRCTRLLOW;

	/*
	 * Configure the power sense and control lines.  Place the USB
	 * host controller in reset.
	 */
	sa1111_writel(usb_rst | USB_RESET_FORCEIFRESET | USB_RESET_FORCEHCRESET,
		      dev->mapbase + USB_RESET);

	/*
	 * Now, carefully enable the USB clock, and take
	 * the USB host controller out of reset.
	 */
	ret = sa1111_enable_device(dev);
	if (ret == 0) {
		udelay(11);
		sa1111_writel(usb_rst, dev->mapbase + USB_RESET);
	}

	return ret;
}

static void sa1111_stop_hc(struct sa1111_dev *dev)
{
	unsigned int usb_rst;

	dev_dbg(&dev->dev, "stopping SA-1111 OHCI USB Controller\n");

	/*
	 * Put the USB host controller into reset.
	 */
	usb_rst = sa1111_readl(dev->mapbase + USB_RESET);
	sa1111_writel(usb_rst | USB_RESET_FORCEIFRESET | USB_RESET_FORCEHCRESET,
		      dev->mapbase + USB_RESET);

	/*
	 * Stop the USB clock.
	 */
	sa1111_disable_device(dev);
}

/**
 * ohci_hcd_sa1111_probe - initialize SA-1111-based HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it.
 */
static int ohci_hcd_sa1111_probe(struct sa1111_dev *dev)
{
	struct usb_hcd *hcd;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	/*
	 * We don't call dma_set_mask_and_coherent() here because the
	 * DMA mask has already been appropraitely setup by the core
	 * SA-1111 bus code (which includes bug workarounds.)
	 */

	hcd = usb_create_hcd(&ohci_sa1111_hc_driver, &dev->dev, "sa1111");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = dev->res.start;
	hcd->rsrc_len = resource_size(&dev->res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&dev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto err1;
	}

	hcd->regs = dev->mapbase;

	ret = sa1111_start_hc(dev);
	if (ret)
		goto err2;

	ret = usb_add_hcd(hcd, dev->irq[1], 0);
	if (ret == 0) {
		device_wakeup_enable(hcd->self.controller);
		return ret;
	}

	sa1111_stop_hc(dev);
 err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
 err1:
	usb_put_hcd(hcd);
	return ret;
}

/**
 * ohci_hcd_sa1111_remove - shutdown processing for SA-1111-based HCDs
 * @dev: USB Host Controller being removed
 *
 * Reverses the effect of ohci_hcd_sa1111_probe(), first invoking
 * the HCD's stop() method.
 */
static int ohci_hcd_sa1111_remove(struct sa1111_dev *dev)
{
	struct usb_hcd *hcd = sa1111_get_drvdata(dev);

	usb_remove_hcd(hcd);
	sa1111_stop_hc(dev);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	return 0;
}

static void ohci_hcd_sa1111_shutdown(struct sa1111_dev *dev)
{
	struct usb_hcd *hcd = sa1111_get_drvdata(dev);

	if (test_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags)) {
		hcd->driver->shutdown(hcd);
		sa1111_stop_hc(dev);
	}
}

static struct sa1111_driver ohci_hcd_sa1111_driver = {
	.drv = {
		.name	= "sa1111-ohci",
		.owner	= THIS_MODULE,
	},
	.devid		= SA1111_DEVID_USB,
	.probe		= ohci_hcd_sa1111_probe,
	.remove		= ohci_hcd_sa1111_remove,
	.shutdown	= ohci_hcd_sa1111_shutdown,
};
