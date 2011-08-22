/*
 *  Bus Glue for Atheros AR7XXX/AR9XXX built-in EHCI controller.
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 *	Copyright (C) 2007 Atheros Communications, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/platform_device.h>

enum {
	EHCI_ATH79_IP_V1 = 0,
	EHCI_ATH79_IP_V2,
};

static const struct platform_device_id ehci_ath79_id_table[] = {
	{
		.name		= "ar71xx-ehci",
		.driver_data	= EHCI_ATH79_IP_V1,
	},
	{
		.name		= "ar724x-ehci",
		.driver_data	= EHCI_ATH79_IP_V2,
	},
	{
		.name		= "ar913x-ehci",
		.driver_data	= EHCI_ATH79_IP_V2,
	},
	{
		/* terminating entry */
	},
};

MODULE_DEVICE_TABLE(platform, ehci_ath79_id_table);

static int ehci_ath79_init(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct platform_device *pdev = to_platform_device(hcd->self.controller);
	const struct platform_device_id *id;
	int ret;

	id = platform_get_device_id(pdev);
	if (!id) {
		dev_err(hcd->self.controller, "missing device id\n");
		return -EINVAL;
	}

	switch (id->driver_data) {
	case EHCI_ATH79_IP_V1:
		ehci->has_synopsys_hc_bug = 1;

		ehci->caps = hcd->regs;
		ehci->regs = hcd->regs +
			HC_LENGTH(ehci,
				  ehci_readl(ehci, &ehci->caps->hc_capbase));
		break;

	case EHCI_ATH79_IP_V2:
		hcd->has_tt = 1;

		ehci->caps = hcd->regs + 0x100;
		ehci->regs = hcd->regs + 0x100 +
			HC_LENGTH(ehci,
				  ehci_readl(ehci, &ehci->caps->hc_capbase));
		break;

	default:
		BUG();
	}

	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);
	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	ret = ehci_init(hcd);
	if (ret)
		return ret;

	ehci_port_power(ehci, 0);

	return 0;
}

static const struct hc_driver ehci_ath79_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Atheros built-in EHCI controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),
	.irq			= ehci_irq,
	.flags			= HCD_MEMORY | HCD_USB2,

	.reset			= ehci_ath79_init,
	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,

	.get_frame_number	= ehci_get_frame,

	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,

	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static int ehci_ath79_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_dbg(&pdev->dev, "no IRQ specified\n");
		return -ENODEV;
	}
	irq = res->start;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_dbg(&pdev->dev, "no base address specified\n");
		return -ENODEV;
	}

	hcd = usb_create_hcd(&ehci_ath79_hc_driver, &pdev->dev,
			     dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start	= res->start;
	hcd->rsrc_len	= res->end - res->start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		ret = -EBUSY;
		goto err_put_hcd;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		ret = -EFAULT;
		goto err_release_region;
	}

	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (ret)
		goto err_iounmap;

	return 0;

err_iounmap:
	iounmap(hcd->regs);

err_release_region:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err_put_hcd:
	usb_put_hcd(hcd);
	return ret;
}

static int ehci_ath79_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	return 0;
}

static struct platform_driver ehci_ath79_driver = {
	.probe		= ehci_ath79_probe,
	.remove		= ehci_ath79_remove,
	.id_table	= ehci_ath79_id_table,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "ath79-ehci",
	}
};

MODULE_ALIAS(PLATFORM_MODULE_PREFIX "ath79-ehci");
