/*
 *  Bus Glue for Loongson LS1X built-in EHCI controller.
 *
 *  Copyright (c) 2012 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */


#include <linux/platform_device.h>

static int ehci_ls1x_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int ret;

	ehci->caps = hcd->regs;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	ehci_port_power(ehci, 0);

	return 0;
}

static const struct hc_driver ehci_ls1x_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "LOONGSON1 EHCI",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset			= ehci_ls1x_reset,
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
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static int ehci_hcd_ls1x_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int ret;

	pr_debug("initializing loongson1 ehci USB Controller\n");

	if (usb_disabled())
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}
	irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}

	hcd = usb_create_hcd(&ehci_ls1x_hc_driver, &pdev->dev,
				dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start	= res->start;
	hcd->rsrc_len	= resource_size(res);

	hcd->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto err_put_hcd;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_put_hcd;

	return ret;

err_put_hcd:
	usb_put_hcd(hcd);
	return ret;
}

static int ehci_hcd_ls1x_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	return 0;
}

static struct platform_driver ehci_ls1x_driver = {
	.probe = ehci_hcd_ls1x_probe,
	.remove = ehci_hcd_ls1x_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		.name = "ls1x-ehci",
		.owner	= THIS_MODULE,
	},
};

MODULE_ALIAS(PLATFORM_MODULE_PREFIX "ls1x-ehci");
