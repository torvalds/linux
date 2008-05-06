/*
 * EHCI HCD (Host Controller Driver) for USB.
 *
 * Bus Glue for AMD Alchemy Au1xxx
 *
 * Based on "ohci-au1xxx.c" by Matt Porter <mporter@kernel.crashing.org>
 *
 * Modified for AMD Alchemy Au1200 EHC
 *  by K.Boge <karsten.boge@amd.com>
 *
 * This file is licenced under the GPL.
 */

#include <linux/platform_device.h>
#include <asm/mach-au1x00/au1000.h>

#define USB_HOST_CONFIG   (USB_MSR_BASE + USB_MSR_MCFG)
#define USB_MCFG_PFEN     (1<<31)
#define USB_MCFG_RDCOMB   (1<<30)
#define USB_MCFG_SSDEN    (1<<23)
#define USB_MCFG_PHYPLLEN (1<<19)
#define USB_MCFG_EHCCLKEN (1<<17)
#define USB_MCFG_UCAM     (1<<7)
#define USB_MCFG_EBMEN    (1<<3)
#define USB_MCFG_EMEMEN   (1<<2)

#define USBH_ENABLE_CE    (USB_MCFG_PHYPLLEN | USB_MCFG_EHCCLKEN)

#ifdef CONFIG_DMA_COHERENT
#define USBH_ENABLE_INIT  (USBH_ENABLE_CE \
                         | USB_MCFG_PFEN | USB_MCFG_RDCOMB \
                         | USB_MCFG_SSDEN | USB_MCFG_UCAM \
                         | USB_MCFG_EBMEN | USB_MCFG_EMEMEN)
#else
#define USBH_ENABLE_INIT  (USBH_ENABLE_CE \
                         | USB_MCFG_PFEN | USB_MCFG_RDCOMB \
                         | USB_MCFG_SSDEN \
                         | USB_MCFG_EBMEN | USB_MCFG_EMEMEN)
#endif
#define USBH_DISABLE      (USB_MCFG_EBMEN | USB_MCFG_EMEMEN)

extern int usb_disabled(void);

/*-------------------------------------------------------------------------*/

static void au1xxx_start_ehc(struct platform_device *dev)
{
	pr_debug(__FILE__ ": starting Au1xxx EHCI USB Controller\n");

	/* write HW defaults again in case Yamon cleared them */
	if (au_readl(USB_HOST_CONFIG) == 0) {
		au_writel(0x00d02000, USB_HOST_CONFIG);
		au_readl(USB_HOST_CONFIG);
		udelay(1000);
	}
	/* enable host controller */
	au_writel(USBH_ENABLE_CE | au_readl(USB_HOST_CONFIG), USB_HOST_CONFIG);
	au_readl(USB_HOST_CONFIG);
	udelay(1000);
	au_writel(USBH_ENABLE_INIT | au_readl(USB_HOST_CONFIG),
		  USB_HOST_CONFIG);
	au_readl(USB_HOST_CONFIG);
	udelay(1000);

	pr_debug(__FILE__ ": Clock to USB host has been enabled\n");
}

static void au1xxx_stop_ehc(struct platform_device *dev)
{
	pr_debug(__FILE__ ": stopping Au1xxx EHCI USB Controller\n");

	/* Disable mem */
	au_writel(~USBH_DISABLE & au_readl(USB_HOST_CONFIG), USB_HOST_CONFIG);
	udelay(1000);
	/* Disable clock */
	au_writel(~USB_MCFG_EHCCLKEN & au_readl(USB_HOST_CONFIG),
		  USB_HOST_CONFIG);
	au_readl(USB_HOST_CONFIG);
}

/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_ehci_au1xxx_probe - initialize Au1xxx-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
int usb_ehci_au1xxx_probe(const struct hc_driver *driver,
			  struct usb_hcd **hcd_out, struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;

#if defined(CONFIG_SOC_AU1200) && defined(CONFIG_DMA_COHERENT)

	/* Au1200 AB USB does not support coherent memory */
	if (!(read_c0_prid() & 0xff)) {
		pr_info("%s: this is chip revision AB!\n", dev->name);
		pr_info("%s: update your board or re-configure the kernel\n",
			dev->name);
		return -ENODEV;
	}
#endif

	au1xxx_start_ehc(dev);

	if (dev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		retval = -ENOMEM;
	}
	hcd = usb_create_hcd(driver, &dev->dev, "Au1xxx");
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
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH(readl(&ehci->caps->hc_capbase));
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	/* ehci_hcd_init(hcd_to_ehci(hcd)); */

	retval =
	    usb_add_hcd(hcd, dev->resource[1].start, IRQF_DISABLED | IRQF_SHARED);
	if (retval == 0)
		return retval;

	au1xxx_stop_ehc(dev);
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
 * usb_ehci_hcd_au1xxx_remove - shutdown processing for Au1xxx-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_ehci_hcd_au1xxx_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_ehci_au1xxx_remove(struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	au1xxx_stop_ehc(dev);
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ehci_au1xxx_hc_driver = {
	.description = hcd_name,
	.product_desc = "Au1xxx EHCI",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 *
	 * FIXME -- ehci_init() doesn't do enough here.
	 * See ehci-ppc-soc for a complete implementation.
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
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
	.relinquish_port = ehci_relinquish_port,
};

/*-------------------------------------------------------------------------*/

static int ehci_hcd_au1xxx_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = NULL;
	int ret;

	pr_debug("In ehci_hcd_au1xxx_drv_probe\n");

	if (usb_disabled())
		return -ENODEV;

	/* FIXME we only want one one probe() not two */
	ret = usb_ehci_au1xxx_probe(&ehci_au1xxx_hc_driver, &hcd, pdev);
	return ret;
}

static int ehci_hcd_au1xxx_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	/* FIXME we only want one one remove() not two */
	usb_ehci_au1xxx_remove(hcd, pdev);
	return 0;
}

 /*TBD*/
/*static int ehci_hcd_au1xxx_drv_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	return 0;
}
static int ehci_hcd_au1xxx_drv_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	return 0;
}
*/
MODULE_ALIAS("platform:au1xxx-ehci");
static struct platform_driver ehci_hcd_au1xxx_driver = {
	.probe = ehci_hcd_au1xxx_drv_probe,
	.remove = ehci_hcd_au1xxx_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	/*.suspend      = ehci_hcd_au1xxx_drv_suspend, */
	/*.resume       = ehci_hcd_au1xxx_drv_resume, */
	.driver = {
		.name = "au1xxx-ehci",
	}
};
