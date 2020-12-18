// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016, NVIDIA Corporation
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/reset.h>

#include <linux/usb/chipidea.h>

#include "ci.h"

struct tegra_usb {
	struct ci_hdrc_platform_data data;
	struct platform_device *dev;

	struct usb_phy *phy;
	struct clk *clk;
};

struct tegra_usb_soc_info {
	unsigned long flags;
};

static const struct tegra_usb_soc_info tegra_udc_soc_info = {
	.flags = CI_HDRC_REQUIRES_ALIGNED_DMA,
};

static const struct of_device_id tegra_usb_of_match[] = {
	{
		.compatible = "nvidia,tegra20-udc",
		.data = &tegra_udc_soc_info,
	}, {
		.compatible = "nvidia,tegra30-udc",
		.data = &tegra_udc_soc_info,
	}, {
		.compatible = "nvidia,tegra114-udc",
		.data = &tegra_udc_soc_info,
	}, {
		.compatible = "nvidia,tegra124-udc",
		.data = &tegra_udc_soc_info,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, tegra_usb_of_match);

static int tegra_usb_probe(struct platform_device *pdev)
{
	const struct tegra_usb_soc_info *soc;
	struct tegra_usb *usb;
	int err;

	usb = devm_kzalloc(&pdev->dev, sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return -ENOMEM;

	soc = of_device_get_match_data(&pdev->dev);
	if (!soc) {
		dev_err(&pdev->dev, "failed to match OF data\n");
		return -EINVAL;
	}

	usb->phy = devm_usb_get_phy_by_phandle(&pdev->dev, "nvidia,phy", 0);
	if (IS_ERR(usb->phy)) {
		err = PTR_ERR(usb->phy);
		dev_err(&pdev->dev, "failed to get PHY: %d\n", err);
		return err;
	}

	usb->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(usb->clk)) {
		err = PTR_ERR(usb->clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(usb->clk);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	/* setup and register ChipIdea HDRC device */
	usb->data.name = "tegra-usb";
	usb->data.flags = soc->flags;
	usb->data.usb_phy = usb->phy;
	usb->data.capoffset = DEF_CAPOFFSET;

	usb->dev = ci_hdrc_add_device(&pdev->dev, pdev->resource,
				      pdev->num_resources, &usb->data);
	if (IS_ERR(usb->dev)) {
		err = PTR_ERR(usb->dev);
		dev_err(&pdev->dev, "failed to add HDRC device: %d\n", err);
		goto fail_power_off;
	}

	platform_set_drvdata(pdev, usb);

	return 0;

fail_power_off:
	clk_disable_unprepare(usb->clk);
	return err;
}

static int tegra_usb_remove(struct platform_device *pdev)
{
	struct tegra_usb *usb = platform_get_drvdata(pdev);

	ci_hdrc_remove_device(usb->dev);
	clk_disable_unprepare(usb->clk);

	return 0;
}

static struct platform_driver tegra_usb_driver = {
	.driver = {
		.name = "tegra-usb",
		.of_match_table = tegra_usb_of_match,
	},
	.probe = tegra_usb_probe,
	.remove = tegra_usb_remove,
};
module_platform_driver(tegra_usb_driver);

MODULE_DESCRIPTION("NVIDIA Tegra USB driver");
MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_LICENSE("GPL v2");
