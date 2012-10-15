/*
 * PMC MSP EHCI (Host Controller Driver) for USB.
 *
 * (C) Copyright 2006-2010 PMC-Sierra Inc
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

/* includes */
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/usb.h>
#include <msp_usb.h>

/* stream disable*/
#define USB_CTRL_MODE_STREAM_DISABLE	0x10

/* threshold */
#define USB_CTRL_FIFO_THRESH		0x00300000

/* register offset for usb_mode */
#define USB_EHCI_REG_USB_MODE		0x68

/* register offset for usb fifo */
#define USB_EHCI_REG_USB_FIFO		0x24

/* register offset for usb status */
#define USB_EHCI_REG_USB_STATUS		0x44

/* serial/parallel transceiver */
#define USB_EHCI_REG_BIT_STAT_STS	(1<<29)

/* TWI USB0 host device pin */
#define MSP_PIN_USB0_HOST_DEV		49

/* TWI USB1 host device pin */
#define MSP_PIN_USB1_HOST_DEV		50


static void usb_hcd_tdi_set_mode(struct ehci_hcd *ehci)
{
	u8 *base;
	u8 *statreg;
	u8 *fiforeg;
	u32 val;
	struct ehci_regs *reg_base = ehci->regs;

	/* get register base */
	base = (u8 *)reg_base + USB_EHCI_REG_USB_MODE;
	statreg = (u8 *)reg_base + USB_EHCI_REG_USB_STATUS;
	fiforeg = (u8 *)reg_base + USB_EHCI_REG_USB_FIFO;

	/* Disable controller mode stream */
	val = ehci_readl(ehci, (u32 *)base);
	ehci_writel(ehci, (val | USB_CTRL_MODE_STREAM_DISABLE),
			(u32 *)base);

	/* clear STS to select parallel transceiver interface */
	val = ehci_readl(ehci, (u32 *)statreg);
	val = val & ~USB_EHCI_REG_BIT_STAT_STS;
	ehci_writel(ehci, val, (u32 *)statreg);

	/* write to set the proper fifo threshold */
	ehci_writel(ehci, USB_CTRL_FIFO_THRESH, (u32 *)fiforeg);

	/* set TWI GPIO USB_HOST_DEV pin high */
	gpio_direction_output(MSP_PIN_USB0_HOST_DEV, 1);
#ifdef CONFIG_MSP_HAS_DUAL_USB
	gpio_direction_output(MSP_PIN_USB1_HOST_DEV, 1);
#endif
}

/* called during probe() after chip reset completes */
static int ehci_msp_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	int			retval;

	ehci->big_endian_mmio = 1;
	ehci->big_endian_desc = 1;

	ehci->caps = hcd->regs;
	hcd->has_tt = 1;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	usb_hcd_tdi_set_mode(ehci);
	ehci_port_power(ehci, 0);

	return retval;
}


/* configure so an HC device and id are always provided
 * always called with process context; sleeping is OK
 */

static int usb_hcd_msp_map_regs(struct mspusb_device *dev)
{
	struct resource *res;
	struct platform_device *pdev = &dev->dev;
	u32 res_len;
	int retval;

	/* MAB register space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res == NULL)
		return -ENOMEM;
	res_len = resource_size(res);
	if (!request_mem_region(res->start, res_len, "mab regs"))
		return -EBUSY;

	dev->mab_regs = ioremap_nocache(res->start, res_len);
	if (dev->mab_regs == NULL) {
		retval = -ENOMEM;
		goto err1;
	}

	/* MSP USB register space */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (res == NULL) {
		retval = -ENOMEM;
		goto err2;
	}
	res_len = resource_size(res);
	if (!request_mem_region(res->start, res_len, "usbid regs")) {
		retval = -EBUSY;
		goto err2;
	}
	dev->usbid_regs = ioremap_nocache(res->start, res_len);
	if (dev->usbid_regs == NULL) {
		retval = -ENOMEM;
		goto err3;
	}

	return 0;
err3:
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	res_len = resource_size(res);
	release_mem_region(res->start, res_len);
err2:
	iounmap(dev->mab_regs);
err1:
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	res_len = resource_size(res);
	release_mem_region(res->start, res_len);
	dev_err(&pdev->dev, "Failed to map non-EHCI regs.\n");
	return retval;
}

/**
 * usb_hcd_msp_probe - initialize PMC MSP-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */
int usb_hcd_msp_probe(const struct hc_driver *driver,
			  struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd;
	struct resource *res;
	struct ehci_hcd		*ehci ;

	hcd = usb_create_hcd(driver, &dev->dev, "pmcmsp");
	if (!hcd)
		return -ENOMEM;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_debug("No IOMEM resource info for %s.\n", dev->name);
		retval = -ENOMEM;
		goto err1;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, dev->name)) {
		retval = -EBUSY;
		goto err1;
	}
	hcd->regs = ioremap_nocache(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err2;
	}

	res = platform_get_resource(dev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&dev->dev, "No IRQ resource info for %s.\n", dev->name);
		retval = -ENOMEM;
		goto err3;
	}

	/* Map non-EHCI register spaces */
	retval = usb_hcd_msp_map_regs(to_mspusb_device(dev));
	if (retval != 0)
		goto err3;

	ehci = hcd_to_ehci(hcd);
	ehci->big_endian_mmio = 1;
	ehci->big_endian_desc = 1;


	retval = usb_add_hcd(hcd, res->start, IRQF_SHARED);
	if (retval == 0)
		return 0;

	usb_remove_hcd(hcd);
err3:
	iounmap(hcd->regs);
err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err1:
	usb_put_hcd(hcd);

	return retval;
}



/**
 * usb_hcd_msp_remove - shutdown processing for PMC MSP-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_msp_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 * may be called without controller electrically present
 * may be called with controller, bus, and devices active
 */
void usb_hcd_msp_remove(struct usb_hcd *hcd, struct platform_device *dev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

#ifdef CONFIG_MSP_HAS_DUAL_USB
/*
 * Wrapper around the main ehci_irq.  Since both USB host controllers are
 * sharing the same IRQ, need to first determine whether we're the intended
 * recipient of this interrupt.
 */
static irqreturn_t ehci_msp_irq(struct usb_hcd *hcd)
{
	u32 int_src;
	struct device *dev = hcd->self.controller;
	struct platform_device *pdev;
	struct mspusb_device *mdev;
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	/* need to reverse-map a couple of containers to get our device */
	pdev = to_platform_device(dev);
	mdev = to_mspusb_device(pdev);

	/* Check to see if this interrupt is for this host controller */
	int_src = ehci_readl(ehci, &mdev->mab_regs->int_stat);
	if (int_src & (1 << pdev->id))
		return ehci_irq(hcd);

	/* Not for this device */
	return IRQ_NONE;
}
#endif /* DUAL_USB */

static const struct hc_driver ehci_msp_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"PMC MSP EHCI",
	.hcd_priv_size =	sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
#ifdef CONFIG_MSP_HAS_DUAL_USB
	.irq =			ehci_msp_irq,
#else
	.irq =			ehci_irq,
#endif
	.flags =		HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset =		ehci_msp_setup,
	.start =		ehci_run,
	.shutdown		= ehci_shutdown,
	.start			= ehci_run,
	.stop			= ehci_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static int ehci_hcd_msp_drv_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("In ehci_hcd_msp_drv_probe");

	if (usb_disabled())
		return -ENODEV;

	gpio_request(MSP_PIN_USB0_HOST_DEV, "USB0_HOST_DEV_GPIO");
#ifdef CONFIG_MSP_HAS_DUAL_USB
	gpio_request(MSP_PIN_USB1_HOST_DEV, "USB1_HOST_DEV_GPIO");
#endif

	ret = usb_hcd_msp_probe(&ehci_msp_hc_driver, pdev);

	return ret;
}

static int ehci_hcd_msp_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_msp_remove(hcd, pdev);

	/* free TWI GPIO USB_HOST_DEV pin */
	gpio_free(MSP_PIN_USB0_HOST_DEV);
#ifdef CONFIG_MSP_HAS_DUAL_USB
	gpio_free(MSP_PIN_USB1_HOST_DEV);
#endif

	return 0;
}

MODULE_ALIAS("pmcmsp-ehci");

static struct platform_driver ehci_hcd_msp_driver = {
	.probe		= ehci_hcd_msp_drv_probe,
	.remove		= ehci_hcd_msp_drv_remove,
	.driver		= {
		.name	= "pmcmsp-ehci",
		.owner	= THIS_MODULE,
	},
};
