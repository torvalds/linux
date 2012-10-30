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


extern int usb_disabled(void);

static int au1xxx_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret;

	ehci->caps = hcd->regs;
	ret = ehci_setup(hcd);

	ehci->need_io_watchdog = 0;
	return ret;
}

static const struct hc_driver ehci_au1xxx_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Au1xxx EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 *
	 * FIXME -- ehci_init() doesn't do enough here.
	 * See ehci-ppc-soc for a complete implementation.
	 */
	.reset			= au1xxx_ehci_setup,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

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

static int ehci_hcd_au1xxx_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	if (pdev->resource[1].flags != IORESOURCE_IRQ) {
		pr_debug("resource[1] is not IORESOURCE_IRQ");
		return -ENOMEM;
	}
	hcd = usb_create_hcd(&ehci_au1xxx_hc_driver, &pdev->dev, "Au1xxx");
	if (!hcd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!hcd->regs) {
		pr_debug("devm_request_and_ioremap failed");
		ret = -ENOMEM;
		goto err1;
	}

	if (alchemy_usb_control(ALCHEMY_USB_EHCI0, 1)) {
		printk(KERN_INFO "%s: controller init failed!\n", pdev->name);
		ret = -ENODEV;
		goto err1;
	}

	ret = usb_add_hcd(hcd, pdev->resource[1].start,
			  IRQF_SHARED);
	if (ret == 0) {
		platform_set_drvdata(pdev, hcd);
		return ret;
	}

	alchemy_usb_control(ALCHEMY_USB_EHCI0, 0);
err1:
	usb_put_hcd(hcd);
	return ret;
}

static int ehci_hcd_au1xxx_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	alchemy_usb_control(ALCHEMY_USB_EHCI0, 0);
	usb_put_hcd(hcd);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int ehci_hcd_au1xxx_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);
	int rc;

	rc = ehci_suspend(hcd, do_wakeup);
	alchemy_usb_control(ALCHEMY_USB_EHCI0, 0);

	return rc;
}

static int ehci_hcd_au1xxx_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	alchemy_usb_control(ALCHEMY_USB_EHCI0, 1);
	ehci_resume(hcd, false);

	return 0;
}

static const struct dev_pm_ops au1xxx_ehci_pmops = {
	.suspend	= ehci_hcd_au1xxx_drv_suspend,
	.resume		= ehci_hcd_au1xxx_drv_resume,
};

#define AU1XXX_EHCI_PMOPS &au1xxx_ehci_pmops

#else
#define AU1XXX_EHCI_PMOPS NULL
#endif

static struct platform_driver ehci_hcd_au1xxx_driver = {
	.probe		= ehci_hcd_au1xxx_drv_probe,
	.remove		= ehci_hcd_au1xxx_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "au1xxx-ehci",
		.owner	= THIS_MODULE,
		.pm	= AU1XXX_EHCI_PMOPS,
	}
};

MODULE_ALIAS("platform:au1xxx-ehci");
