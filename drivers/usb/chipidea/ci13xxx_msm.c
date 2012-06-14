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
#include <linux/usb/gadget.h>
#include <linux/usb/chipidea.h>

#include "ci.h"

#define MSM_USB_BASE	(udc->hw_bank.abs)

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
	struct platform_device *plat_ci;
	int ret;

	dev_dbg(&pdev->dev, "ci13xxx_msm_probe\n");

	plat_ci = platform_device_alloc("ci_hdrc", -1);
	if (!plat_ci) {
		dev_err(&pdev->dev, "can't allocate ci_hdrc platform device\n");
		return -ENOMEM;
	}

	ret = platform_device_add_resources(plat_ci, pdev->resource,
					    pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "can't add resources to platform device\n");
		goto put_platform;
	}

	ret = platform_device_add_data(plat_ci, &ci13xxx_msm_udc_driver,
				       sizeof(ci13xxx_msm_udc_driver));
	if (ret)
		goto put_platform;

	ret = platform_device_add(plat_ci);
	if (ret)
		goto put_platform;

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

put_platform:
	platform_device_put(plat_ci);

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
