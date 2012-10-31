/* ehci-msm.c - HSUSB Host Controller Driver Implementation
 *
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 *
 * Partly derived from ehci-fsl.c and ehci-hcd.c
 * Copyright (c) 2000-2004 by David Brownell
 * Copyright (c) 2005 MontaVista Software
 *
 * All source code in this file is licensed under the following license except
 * where indicated.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>

#include <linux/usb/otg.h>
#include <linux/usb/msm_hsusb_hw.h>

#define MSM_USB_BASE (hcd->regs)

static struct usb_phy *phy;

static int ehci_msm_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	ehci->caps = USB_CAPLENGTH;
	hcd->has_tt = 1;

	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	/* bursts of unspecified length. */
	writel(0, USB_AHBBURST);
	/* Use the AHB transactor */
	writel(0, USB_AHBMODE);
	/* Disable streaming mode and select host mode */
	writel(0x13, USB_USBMODE);

	return 0;
}

static struct hc_driver msm_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Qualcomm On-Chip EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_USB2 | HCD_MEMORY,

	.reset			= ehci_msm_reset,
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
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,

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

	/*
	 * PM support
	 */
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
};

static int ehci_msm_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int ret;

	dev_dbg(&pdev->dev, "ehci_msm proble\n");

	hcd = usb_create_hcd(&msm_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return  -ENOMEM;
	}

	hcd->irq = platform_get_irq(pdev, 0);
	if (hcd->irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		ret = hcd->irq;
		goto put_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_hcd;
	}

	/*
	 * OTG driver takes care of PHY initialization, clock management,
	 * powering up VBUS, mapping of registers address space and power
	 * management.
	 */
	phy = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(phy)) {
		dev_err(&pdev->dev, "unable to find transceiver\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	ret = otg_set_host(phy->otg, &hcd->self);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register with transceiver\n");
		goto put_hcd;
	}

	device_init_wakeup(&pdev->dev, 1);
	/*
	 * OTG device parent of HCD takes care of putting
	 * hardware into low power mode.
	 */
	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int __devexit ehci_msm_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	otg_set_host(phy->otg, NULL);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ehci_msm_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);

	dev_dbg(dev, "ehci-msm PM suspend\n");

	return ehci_suspend(hcd, do_wakeup);
}

static int ehci_msm_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	dev_dbg(dev, "ehci-msm PM resume\n");
	ehci_resume(hcd, false);

	return 0;
}
#else
#define ehci_msm_pm_suspend	NULL
#define ehci_msm_pm_resume	NULL
#endif

static const struct dev_pm_ops ehci_msm_dev_pm_ops = {
	.suspend         = ehci_msm_pm_suspend,
	.resume          = ehci_msm_pm_resume,
};

static struct platform_driver ehci_msm_driver = {
	.probe	= ehci_msm_probe,
	.remove	= __devexit_p(ehci_msm_remove),
	.driver = {
		   .name = "msm_hsusb_host",
		   .pm = &ehci_msm_dev_pm_ops,
	},
};
