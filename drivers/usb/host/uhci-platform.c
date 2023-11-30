// SPDX-License-Identifier: GPL-2.0
/*
 * Generic UHCI HCD (Host Controller Driver) for Platform Devices
 *
 * Copyright (c) 2011 Tony Prisk <linux@prisktech.co.nz>
 *
 * This file is based on uhci-grlib.c
 * (C) Copyright 2004-2007 Alan Stern, stern@rowland.harvard.edu
 */

#include <linux/of.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

static int uhci_platform_init(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	/* Probe number of ports if not already provided by DT */
	if (!uhci->rh_numports)
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
	.flags =		HCD_MEMORY | HCD_DMA | HCD_USB11,

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
	struct device_node *np = pdev->dev.of_node;
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
	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	hcd = usb_create_hcd(&uhci_platform_hc_driver, &pdev->dev,
			pdev->name);
	if (!hcd)
		return -ENOMEM;

	uhci = hcd_to_uhci(hcd);

	hcd->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err_rmr;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	uhci->regs = hcd->regs;

	/* Grab some things from the device-tree */
	if (np) {
		u32 num_ports;

		if (of_property_read_u32(np, "#ports", &num_ports) == 0) {
			uhci->rh_numports = num_ports;
			dev_info(&pdev->dev,
				"Detected %d ports from device-tree\n",
				num_ports);
		}
		if (of_device_is_compatible(np, "aspeed,ast2400-uhci") ||
		    of_device_is_compatible(np, "aspeed,ast2500-uhci") ||
		    of_device_is_compatible(np, "aspeed,ast2600-uhci") ||
		    of_device_is_compatible(np, "aspeed,ast2700-uhci")) {
			uhci->is_aspeed = 1;
			dev_info(&pdev->dev,
				 "Enabled Aspeed implementation workarounds\n");
		}
	}

	/* Get and enable clock if any specified */
	uhci->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(uhci->clk)) {
		ret = PTR_ERR(uhci->clk);
		goto err_rmr;
	}
	ret = clk_prepare_enable(uhci->clk);
	if (ret) {
		dev_err(&pdev->dev, "Error couldn't enable clock (%d)\n", ret);
		goto err_rmr;
	}
#ifdef CONFIG_MACH_ASPEED_G7
	uhci->rsts = devm_reset_control_array_get_optional_shared(&pdev->dev);
	if (IS_ERR(uhci->rsts)) {
		ret = PTR_ERR(uhci->rsts);
		goto err_clk;
	}

	mdelay(10);

	ret = reset_control_deassert(uhci->rsts);
	if (ret)
		goto err_clk;
#endif
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto err_clk;

	ret = usb_add_hcd(hcd, ret, IRQF_SHARED);
	if (ret)
		goto err_clk;

	device_wakeup_enable(hcd->self.controller);
	return 0;

err_clk:
	clk_disable_unprepare(uhci->clk);
err_rmr:
	usb_put_hcd(hcd);

	return ret;
}

static void uhci_hcd_platform_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	clk_disable_unprepare(uhci->clk);
	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
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
	struct usb_hcd *hcd = platform_get_drvdata(op);

	uhci_hc_died(hcd_to_uhci(hcd));
}

static const struct of_device_id platform_uhci_ids[] = {
	{ .compatible = "generic-uhci", },
	{ .compatible = "platform-uhci", },
	{}
};
MODULE_DEVICE_TABLE(of, platform_uhci_ids);

static struct platform_driver uhci_platform_driver = {
	.probe		= uhci_hcd_platform_probe,
	.remove_new	= uhci_hcd_platform_remove,
	.shutdown	= uhci_hcd_platform_shutdown,
	.driver = {
		.name = "platform-uhci",
		.of_match_table = platform_uhci_ids,
	},
};
