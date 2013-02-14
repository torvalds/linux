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

#define MSM_USB_BASE	(ci->hw_bank.abs)

static void ci13xxx_msm_notify_event(struct ci13xxx *ci, unsigned event)
{
	struct device *dev = ci->gadget.dev.parent;
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
		val = usb_phy_io_read(ci->transceiver, ULPI_FUNC_CTRL);
		val &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
		val |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
		usb_phy_io_write(ci->transceiver, val, ULPI_FUNC_CTRL);
		break;
	default:
		dev_dbg(dev, "unknown ci13xxx event\n");
		break;
	}
}

static struct ci13xxx_platform_data ci13xxx_msm_platdata = {
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

	dev_dbg(&pdev->dev, "ci13xxx_msm_probe\n");

	plat_ci = ci13xxx_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci13xxx_msm_platdata);
	if (IS_ERR(plat_ci)) {
		dev_err(&pdev->dev, "ci13xxx_add_device failed!\n");
		return PTR_ERR(plat_ci);
	}

	platform_set_drvdata(pdev, plat_ci);

	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int ci13xxx_msm_remove(struct platform_device *pdev)
{
	struct platform_device *plat_ci = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci13xxx_remove_device(plat_ci);

	return 0;
}

static struct platform_driver ci13xxx_msm_driver = {
	.probe = ci13xxx_msm_probe,
	.remove = ci13xxx_msm_remove,
	.driver = { .name = "msm_hsusb", },
};

module_platform_driver(ci13xxx_msm_driver);

MODULE_ALIAS("platform:msm_hsusb");
MODULE_LICENSE("GPL v2");
