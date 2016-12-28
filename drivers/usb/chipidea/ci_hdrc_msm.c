/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/usb/gadget.h>
#include <linux/usb/chipidea.h>
#include <linux/clk.h>
#include <linux/reset.h>

#include "ci.h"

#define HS_PHY_AHB_MODE			0x0098

struct ci_hdrc_msm {
	struct platform_device *ci;
	struct clk *core_clk;
	struct clk *iface_clk;
	struct clk *fs_clk;
};

static void ci_hdrc_msm_notify_event(struct ci_hdrc *ci, unsigned event)
{
	struct device *dev = ci->gadget.dev.parent;

	switch (event) {
	case CI_HDRC_CONTROLLER_RESET_EVENT:
		dev_dbg(dev, "CI_HDRC_CONTROLLER_RESET_EVENT received\n");
		/* use AHB transactor, allow posted data writes */
		hw_write_id_reg(ci, HS_PHY_AHB_MODE, 0xffffffff, 0x8);
		usb_phy_init(ci->usb_phy);
		break;
	case CI_HDRC_CONTROLLER_STOPPED_EVENT:
		dev_dbg(dev, "CI_HDRC_CONTROLLER_STOPPED_EVENT received\n");
		/*
		 * Put the phy in non-driving mode. Otherwise host
		 * may not detect soft-disconnection.
		 */
		usb_phy_notify_disconnect(ci->usb_phy, USB_SPEED_UNKNOWN);
		break;
	default:
		dev_dbg(dev, "unknown ci_hdrc event\n");
		break;
	}
}

static struct ci_hdrc_platform_data ci_hdrc_msm_platdata = {
	.name			= "ci_hdrc_msm",
	.capoffset		= DEF_CAPOFFSET,
	.flags			= CI_HDRC_REGS_SHARED |
				  CI_HDRC_DISABLE_STREAMING |
				  CI_HDRC_OVERRIDE_AHB_BURST,

	.notify_event		= ci_hdrc_msm_notify_event,
};

static int ci_hdrc_msm_probe(struct platform_device *pdev)
{
	struct ci_hdrc_msm *ci;
	struct platform_device *plat_ci;
	struct usb_phy *phy;
	struct clk *clk;
	struct reset_control *reset;
	int ret;

	dev_dbg(&pdev->dev, "ci_hdrc_msm_probe\n");

	ci = devm_kzalloc(&pdev->dev, sizeof(*ci), GFP_KERNEL);
	if (!ci)
		return -ENOMEM;
	platform_set_drvdata(pdev, ci);

	/*
	 * OTG(PHY) driver takes care of PHY initialization, clock management,
	 * powering up VBUS, mapping of registers address space and power
	 * management.
	 */
	phy = devm_usb_get_phy_by_phandle(&pdev->dev, "usb-phy", 0);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	ci_hdrc_msm_platdata.usb_phy = phy;

	reset = devm_reset_control_get(&pdev->dev, "core");
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	ci->core_clk = clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ci->iface_clk = clk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ci->fs_clk = clk = devm_clk_get(&pdev->dev, "fs");
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		ci->fs_clk = NULL;
	}

	ret = clk_prepare_enable(ci->fs_clk);
	if (ret)
		return ret;

	reset_control_assert(reset);
	usleep_range(10000, 12000);
	reset_control_deassert(reset);

	clk_disable_unprepare(ci->fs_clk);

	ret = clk_prepare_enable(ci->core_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(ci->iface_clk);
	if (ret)
		goto err_iface;

	plat_ci = ci_hdrc_add_device(&pdev->dev,
				pdev->resource, pdev->num_resources,
				&ci_hdrc_msm_platdata);
	if (IS_ERR(plat_ci)) {
		dev_err(&pdev->dev, "ci_hdrc_add_device failed!\n");
		ret = PTR_ERR(plat_ci);
		goto err_mux;
	}

	ci->ci = plat_ci;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

err_mux:
	clk_disable_unprepare(ci->iface_clk);
err_iface:
	clk_disable_unprepare(ci->core_clk);
	return ret;
}

static int ci_hdrc_msm_remove(struct platform_device *pdev)
{
	struct ci_hdrc_msm *ci = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	ci_hdrc_remove_device(ci->ci);
	clk_disable_unprepare(ci->iface_clk);
	clk_disable_unprepare(ci->core_clk);

	return 0;
}

static const struct of_device_id msm_ci_dt_match[] = {
	{ .compatible = "qcom,ci-hdrc", },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_ci_dt_match);

static struct platform_driver ci_hdrc_msm_driver = {
	.probe = ci_hdrc_msm_probe,
	.remove = ci_hdrc_msm_remove,
	.driver = {
		.name = "msm_hsusb",
		.of_match_table = msm_ci_dt_match,
	},
};

module_platform_driver(ci_hdrc_msm_driver);

MODULE_ALIAS("platform:msm_hsusb");
MODULE_ALIAS("platform:ci13xxx_msm");
MODULE_LICENSE("GPL v2");
