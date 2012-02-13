/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/usb/ulpi.h>

#include "ci13xxx_udc.c"

#define MSM_USB_BASE	(udc->regs)

static irqreturn_t msm_udc_irq(int irq, void *data)
{
	return udc_irq();
}

static void ci13xxx_msm_notify_event(struct ci13xxx *udc, unsigned event)
{
	struct device *dev = udc->gadget.dev.parent;
	int val;

	switch (event) {
	case CI13XXX_CONTROLLER_RESET_EVENT:
		dev_dbg(dev, "CI13XXX_CONTROLLER_RESET_EVENT received\n");
		writel(0, USB_AHBBURST);
		writel(0, USB_AHBMODE);
		break;
	case CI13XXX_CONTROLLER_STOPPED_EVENT:
		dev_dbg(dev, "CI13XXX_CONTROLLER_STOPPED_EVENT received\n");
		/*
		 * Put the transceiver in non-driving mode. Otherwise host
		 * may not detect soft-disconnection.
		 */
		val = usb_phy_io_read(udc->transceiver, ULPI_FUNC_CTRL);
		val &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		val |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
		usb_phy_io_write(udc->transceiver, val, ULPI_FUNC_CTRL);
		break;
	default:
		dev_dbg(dev, "unknown ci13xxx_udc event\n");
		break;
	}
}

static struct ci13xxx_udc_driver ci13xxx_msm_udc_driver = {
	.name			= "ci13xxx_msm",
	.flags			= CI13XXX_REGS_SHARED |
				  CI13XXX_REQUIRE_TRANSCEIVER |
				  CI13XXX_PULLUP_ON_VBUS |
				  CI13XXX_DISABLE_STREAMING,

	.notify_event		= ci13xxx_msm_notify_event,
};

static int ci13xxx_msm_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *regs;
	int irq;
	int ret;

	dev_dbg(&pdev->dev, "ci13xxx_msm_probe\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform resource mem\n");
		return -ENXIO;
	}

	regs = ioremap(res->start, resource_size(res));
	if (!regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	ret = udc_probe(&ci13xxx_msm_udc_driver, &pdev->dev, regs);
	if (ret < 0) {
		dev_err(&pdev->dev, "udc_probe failed\n");
		goto iounmap;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "IRQ not found\n");
		ret = -ENXIO;
		goto udc_remove;
	}

	ret = request_irq(irq, msm_udc_irq, IRQF_SHARED, pdev->name, pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq failed\n");
		goto udc_remove;
	}

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

udc_remove:
	udc_remove();
iounmap:
	iounmap(regs);

	return ret;
}

static struct platform_driver ci13xxx_msm_driver = {
	.probe = ci13xxx_msm_probe,
	.driver = { .name = "msm_hsusb", },
};
MODULE_ALIAS("platform:msm_hsusb");

static int __init ci13xxx_msm_init(void)
{
	return platform_driver_register(&ci13xxx_msm_driver);
}
module_init(ci13xxx_msm_init);

MODULE_LICENSE("GPL v2");
