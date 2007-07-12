/*
 * EHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 2006-2007 Stefan Roese <sr@denx.de>, DENX Software Engineering
 *
 * Bus Glue for PPC On-Chip EHCI driver
 * Tested on AMCC 440EPx
 *
 * Based on "ehci-au12xx.c" by David Brownell <dbrownell@users.sourceforge.net>
 *
 * This file is licenced under the GPL.
 */

#include <linux/platform_device.h>

extern int usb_disabled(void);

/**
 * usb_ehci_ppc_soc_probe - initialize PPC-SoC-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
int usb_ehci_ppc_soc_probe(const struct hc_driver *driver,
			   struct usb_hcd **hcd_out,
			   struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;

	if (dev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		retval = -ENOMEM;
	}
	hcd = usb_create_hcd(driver, &dev->dev, "PPC-SOC EHCI");
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

	ehci = hcd_to_ehci(hcd);
	ehci->big_endian_mmio = 1;
	ehci->big_endian_desc = 1;
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH(ehci_readl(ehci, &ehci->caps->hc_capbase));

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

#if defined(CONFIG_440EPX)
	/*
	 * 440EPx Errata USBH_3
	 * Fix: Enable Break Memory Transfer (BMT) in INSNREG3
	 */
	out_be32((void *)((ulong)(&ehci->regs->command) + 0x8c), (1 << 0));
	ehci_dbg(ehci, "Break Memory Transfer (BMT) has beed enabled!\n");
#endif

	retval = usb_add_hcd(hcd, dev->resource[1].start, IRQF_DISABLED);
	if (retval == 0)
		return retval;

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
 * usb_ehci_hcd_ppc_soc_remove - shutdown processing for PPC-SoC-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_ehci_hcd_ppc_soc_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_ehci_ppc_soc_remove(struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

static const struct hc_driver ehci_ppc_soc_hc_driver = {
	.description = hcd_name,
	.product_desc = "PPC-SOC EHCI",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_init,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
#ifdef	CONFIG_PM
	.hub_suspend = ehci_hub_suspend,
	.hub_resume = ehci_hub_resume,
#endif
};

static int ehci_hcd_ppc_soc_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	int ret;

	pr_debug("In ehci_hcd_ppc_soc_drv_probe\n");

	if (usb_disabled())
		return -ENODEV;

	ret = usb_ehci_ppc_soc_probe(&ehci_ppc_soc_hc_driver, &hcd, pdev);
	return ret;
}

static int ehci_hcd_ppc_soc_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_ehci_ppc_soc_remove(hcd, pdev);
	return 0;
}

MODULE_ALIAS("ppc-soc-ehci");
static struct platform_driver ehci_ppc_soc_driver = {
	.probe = ehci_hcd_ppc_soc_drv_probe,
	.remove = ehci_hcd_ppc_soc_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		.name = "ppc-soc-ehci",
		.bus = &platform_bus_type
	}
};
