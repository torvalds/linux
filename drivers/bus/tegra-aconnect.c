/*
 * Tegra ACONNECT Bus Driver
 *
 * Copyright (C) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

struct tegra_aconnect {
	struct clk	*ape_clk;
	struct clk	*apb2ape_clk;
};

static int tegra_aconnect_probe(struct platform_device *pdev)
{
	struct tegra_aconnect *aconnect;

	if (!pdev->dev.of_node)
		return -EINVAL;

	aconnect = devm_kzalloc(&pdev->dev, sizeof(struct tegra_aconnect),
				GFP_KERNEL);
	if (!aconnect)
		return -ENOMEM;

	aconnect->ape_clk = devm_clk_get(&pdev->dev, "ape");
	if (IS_ERR(aconnect->ape_clk)) {
		dev_err(&pdev->dev, "Can't retrieve ape clock\n");
		return PTR_ERR(aconnect->ape_clk);
	}

	aconnect->apb2ape_clk = devm_clk_get(&pdev->dev, "apb2ape");
	if (IS_ERR(aconnect->apb2ape_clk)) {
		dev_err(&pdev->dev, "Can't retrieve apb2ape clock\n");
		return PTR_ERR(aconnect->apb2ape_clk);
	}

	dev_set_drvdata(&pdev->dev, aconnect);
	pm_runtime_enable(&pdev->dev);

	of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	dev_info(&pdev->dev, "Tegra ACONNECT bus registered\n");

	return 0;
}

static void tegra_aconnect_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static int tegra_aconnect_runtime_resume(struct device *dev)
{
	struct tegra_aconnect *aconnect = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(aconnect->ape_clk);
	if (ret) {
		dev_err(dev, "ape clk_enable failed: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(aconnect->apb2ape_clk);
	if (ret) {
		clk_disable_unprepare(aconnect->ape_clk);
		dev_err(dev, "apb2ape clk_enable failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tegra_aconnect_runtime_suspend(struct device *dev)
{
	struct tegra_aconnect *aconnect = dev_get_drvdata(dev);

	clk_disable_unprepare(aconnect->ape_clk);
	clk_disable_unprepare(aconnect->apb2ape_clk);

	return 0;
}

static const struct dev_pm_ops tegra_aconnect_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_aconnect_runtime_suspend,
			   tegra_aconnect_runtime_resume, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				      pm_runtime_force_resume)
};

static const struct of_device_id tegra_aconnect_of_match[] = {
	{ .compatible = "nvidia,tegra210-aconnect", },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_aconnect_of_match);

static struct platform_driver tegra_aconnect_driver = {
	.probe = tegra_aconnect_probe,
	.remove_new = tegra_aconnect_remove,
	.driver = {
		.name = "tegra-aconnect",
		.of_match_table = tegra_aconnect_of_match,
		.pm = &tegra_aconnect_pm_ops,
	},
};
module_platform_driver(tegra_aconnect_driver);

MODULE_DESCRIPTION("NVIDIA Tegra ACONNECT Bus Driver");
MODULE_AUTHOR("Jon Hunter <jonathanh@nvidia.com>");
MODULE_LICENSE("GPL v2");
