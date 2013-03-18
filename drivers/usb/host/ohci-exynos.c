/*
 * SAMSUNG EXYNOS USB HOST OHCI Controller
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/platform_data/usb-exynos.h>
#include <linux/usb/phy.h>
#include <linux/usb/samsung_usb_phy.h>
#include <plat/usb-phy.h>

struct exynos_ohci_hcd {
	struct device *dev;
	struct usb_hcd *hcd;
	struct clk *clk;
	struct usb_phy *phy;
	struct usb_otg *otg;
	struct exynos4_ohci_platdata *pdata;
};

static void exynos_ohci_phy_enable(struct exynos_ohci_hcd *exynos_ohci)
{
	struct platform_device *pdev = to_platform_device(exynos_ohci->dev);

	if (exynos_ohci->phy)
		usb_phy_init(exynos_ohci->phy);
	else if (exynos_ohci->pdata->phy_init)
		exynos_ohci->pdata->phy_init(pdev, USB_PHY_TYPE_HOST);
}

static void exynos_ohci_phy_disable(struct exynos_ohci_hcd *exynos_ohci)
{
	struct platform_device *pdev = to_platform_device(exynos_ohci->dev);

	if (exynos_ohci->phy)
		usb_phy_shutdown(exynos_ohci->phy);
	else if (exynos_ohci->pdata->phy_exit)
		exynos_ohci->pdata->phy_exit(pdev, USB_PHY_TYPE_HOST);
}

static int ohci_exynos_reset(struct usb_hcd *hcd)
{
	return ohci_init(hcd_to_ohci(hcd));
}

static int ohci_exynos_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	ohci_dbg(ohci, "ohci_exynos_start, ohci:%p", ohci);

	ret = ohci_run(ohci);
	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start %s\n",
			hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}

	return 0;
}

static const struct hc_driver exynos_ohci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "EXYNOS OHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ohci_hcd),

	.irq			= ohci_irq,
	.flags			= HCD_MEMORY|HCD_USB11,

	.reset			= ohci_exynos_reset,
	.start			= ohci_exynos_start,
	.stop			= ohci_stop,
	.shutdown		= ohci_shutdown,

	.get_frame_number	= ohci_get_frame,

	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,

	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend		= ohci_bus_suspend,
	.bus_resume		= ohci_bus_resume,
#endif
	.start_port_reset	= ohci_start_port_reset,
};

static u64 ohci_exynos_dma_mask = DMA_BIT_MASK(32);

static int exynos_ohci_probe(struct platform_device *pdev)
{
	struct exynos4_ohci_platdata *pdata = pdev->dev.platform_data;
	struct exynos_ohci_hcd *exynos_ohci;
	struct usb_hcd *hcd;
	struct ohci_hcd *ohci;
	struct resource *res;
	struct usb_phy *phy;
	int irq;
	int err;

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &ohci_exynos_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	exynos_ohci = devm_kzalloc(&pdev->dev, sizeof(struct exynos_ohci_hcd),
					GFP_KERNEL);
	if (!exynos_ohci)
		return -ENOMEM;

	phy = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(phy)) {
		/* Fallback to pdata */
		if (!pdata) {
			dev_warn(&pdev->dev, "no platform data or transceiver defined\n");
			return -EPROBE_DEFER;
		} else {
			exynos_ohci->pdata = pdata;
		}
	} else {
		exynos_ohci->phy = phy;
		exynos_ohci->otg = phy->otg;
	}

	exynos_ohci->dev = &pdev->dev;

	hcd = usb_create_hcd(&exynos_ohci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return -ENOMEM;
	}

	exynos_ohci->hcd = hcd;
	exynos_ohci->clk = devm_clk_get(&pdev->dev, "usbhost");

	if (IS_ERR(exynos_ohci->clk)) {
		dev_err(&pdev->dev, "Failed to get usbhost clock\n");
		err = PTR_ERR(exynos_ohci->clk);
		goto fail_clk;
	}

	err = clk_prepare_enable(exynos_ohci->clk);
	if (err)
		goto fail_clk;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, res->start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail_io;
	}

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg,
					&exynos_ohci->hcd->self);

	exynos_ohci_phy_enable(exynos_ohci);

	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
		goto fail_add_hcd;
	}

	platform_set_drvdata(pdev, exynos_ohci);

	return 0;

fail_add_hcd:
	exynos_ohci_phy_disable(exynos_ohci);
fail_io:
	clk_disable_unprepare(exynos_ohci->clk);
fail_clk:
	usb_put_hcd(hcd);
	return err;
}

static int exynos_ohci_remove(struct platform_device *pdev)
{
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;

	usb_remove_hcd(hcd);

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg,
					&exynos_ohci->hcd->self);

	exynos_ohci_phy_disable(exynos_ohci);

	clk_disable_unprepare(exynos_ohci->clk);

	usb_put_hcd(hcd);

	return 0;
}

static void exynos_ohci_shutdown(struct platform_device *pdev)
{
	struct exynos_ohci_hcd *exynos_ohci = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = exynos_ohci->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

#ifdef CONFIG_PM
static int exynos_ohci_suspend(struct device *dev)
{
	struct exynos_ohci_hcd *exynos_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = exynos_ohci->hcd;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	unsigned long flags;
	int rc = 0;

	/*
	 * Root hub was already suspended. Disable irq emission and
	 * mark HW unaccessible, bail out if RH has been resumed. Use
	 * the spinlock to properly synchronize with possible pending
	 * RH suspend or resume activity.
	 */
	spin_lock_irqsave(&ohci->lock, flags);
	if (ohci->rh_state != OHCI_RH_SUSPENDED &&
			ohci->rh_state != OHCI_RH_HALTED) {
		rc = -EINVAL;
		goto fail;
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg,
					&exynos_ohci->hcd->self);

	exynos_ohci_phy_disable(exynos_ohci);

	clk_disable_unprepare(exynos_ohci->clk);

fail:
	spin_unlock_irqrestore(&ohci->lock, flags);

	return rc;
}

static int exynos_ohci_resume(struct device *dev)
{
	struct exynos_ohci_hcd *exynos_ohci = dev_get_drvdata(dev);
	struct usb_hcd *hcd = exynos_ohci->hcd;

	clk_prepare_enable(exynos_ohci->clk);

	if (exynos_ohci->otg)
		exynos_ohci->otg->set_host(exynos_ohci->otg,
					&exynos_ohci->hcd->self);

	exynos_ohci_phy_enable(exynos_ohci);

	ohci_resume(hcd, false);

	return 0;
}
#else
#define exynos_ohci_suspend	NULL
#define exynos_ohci_resume	NULL
#endif

static const struct dev_pm_ops exynos_ohci_pm_ops = {
	.suspend	= exynos_ohci_suspend,
	.resume		= exynos_ohci_resume,
};

#ifdef CONFIG_OF
static const struct of_device_id exynos_ohci_match[] = {
	{ .compatible = "samsung,exynos4210-ohci" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_ohci_match);
#endif

static struct platform_driver exynos_ohci_driver = {
	.probe		= exynos_ohci_probe,
	.remove		= exynos_ohci_remove,
	.shutdown	= exynos_ohci_shutdown,
	.driver = {
		.name	= "exynos-ohci",
		.owner	= THIS_MODULE,
		.pm	= &exynos_ohci_pm_ops,
		.of_match_table	= of_match_ptr(exynos_ohci_match),
	}
};

MODULE_ALIAS("platform:exynos-ohci");
MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
