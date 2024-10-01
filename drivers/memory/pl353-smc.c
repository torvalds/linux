// SPDX-License-Identifier: GPL-2.0
/*
 * ARM PL353 SMC driver
 *
 * Copyright (C) 2012 - 2018 Xilinx, Inc
 * Author: Punnaiah Choudary Kalluri <punnaiah@xilinx.com>
 * Author: Naga Sureshkumar Relli <nagasure@xilinx.com>
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>

/**
 * struct pl353_smc_data - Private smc driver structure
 * @memclk:		Pointer to the peripheral clock
 * @aclk:		Pointer to the AXI peripheral clock
 */
struct pl353_smc_data {
	struct clk		*memclk;
	struct clk		*aclk;
};

static int __maybe_unused pl353_smc_suspend(struct device *dev)
{
	struct pl353_smc_data *pl353_smc = dev_get_drvdata(dev);

	clk_disable(pl353_smc->memclk);
	clk_disable(pl353_smc->aclk);

	return 0;
}

static int __maybe_unused pl353_smc_resume(struct device *dev)
{
	struct pl353_smc_data *pl353_smc = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(pl353_smc->aclk);
	if (ret) {
		dev_err(dev, "Cannot enable axi domain clock.\n");
		return ret;
	}

	ret = clk_enable(pl353_smc->memclk);
	if (ret) {
		dev_err(dev, "Cannot enable memory clock.\n");
		clk_disable(pl353_smc->aclk);
		return ret;
	}

	return ret;
}

static SIMPLE_DEV_PM_OPS(pl353_smc_dev_pm_ops, pl353_smc_suspend,
			 pl353_smc_resume);

static const struct of_device_id pl353_smc_supported_children[] = {
	{
		.compatible = "cfi-flash"
	},
	{
		.compatible = "arm,pl353-nand-r2p1",
	},
	{}
};

static int pl353_smc_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct device_node *of_node = adev->dev.of_node;
	const struct of_device_id *match = NULL;
	struct pl353_smc_data *pl353_smc;

	pl353_smc = devm_kzalloc(&adev->dev, sizeof(*pl353_smc), GFP_KERNEL);
	if (!pl353_smc)
		return -ENOMEM;

	pl353_smc->aclk = devm_clk_get_enabled(&adev->dev, "apb_pclk");
	if (IS_ERR(pl353_smc->aclk))
		return dev_err_probe(&adev->dev, PTR_ERR(pl353_smc->aclk),
				     "aclk clock not found.\n");

	pl353_smc->memclk = devm_clk_get_enabled(&adev->dev, "memclk");
	if (IS_ERR(pl353_smc->memclk))
		return dev_err_probe(&adev->dev, PTR_ERR(pl353_smc->memclk),
				     "memclk clock not found.\n");

	amba_set_drvdata(adev, pl353_smc);

	/* Find compatible children. Only a single child is supported */
	for_each_available_child_of_node_scoped(of_node, child) {
		match = of_match_node(pl353_smc_supported_children, child);
		if (!match) {
			dev_warn(&adev->dev, "unsupported child node\n");
			continue;
		}
		of_platform_device_create(child, NULL, &adev->dev);
		break;
	}
	if (!match) {
		dev_err(&adev->dev, "no matching children\n");
		return -ENODEV;
	}

	return 0;
}

static const struct amba_id pl353_ids[] = {
	{
		.id = 0x00041353,
		.mask = 0x000fffff,
	},
	{ 0, 0 },
};
MODULE_DEVICE_TABLE(amba, pl353_ids);

static struct amba_driver pl353_smc_driver = {
	.drv = {
		.name = "pl353-smc",
		.pm = &pl353_smc_dev_pm_ops,
	},
	.id_table = pl353_ids,
	.probe = pl353_smc_probe,
};

module_amba_driver(pl353_smc_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("ARM PL353 SMC Driver");
MODULE_LICENSE("GPL");
