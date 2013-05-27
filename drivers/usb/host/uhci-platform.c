/*
 * Generic UHCI HCD (Host Controller Driver) for Platform Devices
 *
 * Copyright (c) 2011 Tony Prisk <linux@prisktech.co.nz>
 *
 * This file is based on uhci-grlib.c
 * (C) Copyright 2004-2007 Alan Stern, stern@rowland.harvard.edu
 */

#include <linux/of.h>
#include <linux/platform_device.h>

static int uhci_platform_init(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	uhci->rh_numports = uhci_count_ports(hcd);

	/* Set up pointers to to generic functions */
	uhci->reset_hc = uhci_generic_reset_hc;
	uhci->check_and_reset_hc = uhci_generic_check_and_reset_hc;

	/* No special actions need to be taken for the functions below */
	uhci->configure_hc = NULL;
	uhci->resume_detect_interrupts_are_broken = NULL;
	uhci->global_suspend_mode_is_broken = NULL;

	/* Reset if the controller isn't already safely quiescent. */
	check_and_reset_hc(uhci);
	return 0;
}

static const struct hc_driver uhci_platform_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"Generic UHCI Host Controller",
	.hcd_priv_size =	sizeof(struct uhci_hcd),

	/* Generic hardware linkage */
	.irq =			uhci_irq,
	.flags =		HCD_MEMORY | HCD_USB11,

	/* Basic lifecycle operations */
	.reset =		uhci_platform_init,
	.start =		uhci_start,
#ifdef CONFIG_PM
	.pci_suspend =		NULL,
	.pci_resume =		NULL,
	.bus_suspend =		uhci_rh_suspend,
	.bus_resume =		uhci_rh_resume,
#endif
	.stop =			uhci_stop,

	.urb_enqueue =		uhci_urb_enqueue,
	.urb_dequeue =		uhci_urb_dequeue,

	.endpoint_disable =	uhci_hcd_endpoint_disable,
	.get_frame_number =	uhci_hcd_get_frame_number,

	.hub_status_data =	uhci_hub_status_data,
	.hub_control =		uhci_hub_control,
};

static int uhci_hcd_platform_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct uhci_hcd	*uhci;
	struct resource *res;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	hcd = usb_create_hcd(&uhci_platform_hc_driver, &pdev->dev,
			pdev->name);
	if (!hcd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		pr_err("%s: request_mem_region failed\n", __func__);
		ret = -EBUSY;
		goto err_rmr;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_err("%s: ioremap failed\n", __func__);
		ret = -ENOMEM;
		goto err_irq;
	}
	uhci = hcd_to_uhci(hcd);

	uhci->regs = hcd->regs;

	ret = usb_add_hcd(hcd, pdev->resource[1].start, IRQF_DISABLED |
								IRQF_SHARED);
	if (ret)
		goto err_uhci;

	return 0;

err_uhci:
	iounmap(hcd->regs);
err_irq:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err_rmr:
	usb_put_hcd(hcd);

	return ret;
}

static int uhci_hcd_platform_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	return 0;
}

/* Make sure the controller is quiescent and that we're not using it
 * any more.  This is mainly for the benefit of programs which, like kexec,
 * expect the hardware to be idle: not doing DMA or generating IRQs.
 *
 * This routine may be called in a damaged or failing kernel.  Hence we
 * do not acquire the spinlock before shutting down the controller.
 */
static void uhci_hcd_platform_shutdown(struct platform_device *op)
{
	struct usb_hcd *hcd = dev_get_drvdata(&op->dev);

	uhci_hc_died(hcd_to_uhci(hcd));
}

static const struct of_device_id platform_uhci_ids[] = {
	{ .compatible = "platform-uhci", },
	{}
};

static struct platform_driver uhci_platform_driver = {
	.probe		= uhci_hcd_platform_probe,
	.remove		= uhci_hcd_platform_remove,
	.shutdown	= uhci_hcd_platform_shutdown,
	.driver = {
		.name = "platform-uhci",
		.owner = THIS_MODULE,
		.of_match_table = platform_uhci_ids,
	},
};
