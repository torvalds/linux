// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
//

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define TI_UFS_SS_CTRL		0x4
#define TI_UFS_SS_RST_N_PCS	BIT(0)
#define TI_UFS_SS_CLK_26MHZ	BIT(4)

static int ti_j721e_ufs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned long clk_rate;
	void __iomem *regbase;
	struct clk *clk;
	u32 reg = 0;
	int ret;

	regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regbase))
		return PTR_ERR(regbase);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		goto disable_pm;
	}

	/* Select MPHY refclk frequency */
	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "Cannot claim MPHY clock.\n");
		goto clk_err;
	}
	clk_rate = clk_get_rate(clk);
	if (clk_rate == 26000000)
		reg |= TI_UFS_SS_CLK_26MHZ;
	devm_clk_put(dev, clk);

	/*  Take UFS slave device out of reset */
	reg |= TI_UFS_SS_RST_N_PCS;
	writel(reg, regbase + TI_UFS_SS_CTRL);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL,
				   dev);
	if (ret) {
		dev_err(dev, "failed to populate child nodes %d\n", ret);
		goto clk_err;
	}

	return ret;

clk_err:
	pm_runtime_put_sync(dev);
disable_pm:
	pm_runtime_disable(dev);
	return ret;
}

static int ti_j721e_ufs_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id ti_j721e_ufs_of_match[] = {
	{
		.compatible = "ti,j721e-ufs",
	},
	{ },
};

static struct platform_driver ti_j721e_ufs_driver = {
	.probe	= ti_j721e_ufs_probe,
	.remove	= ti_j721e_ufs_remove,
	.driver	= {
		.name   = "ti-j721e-ufs",
		.of_match_table = ti_j721e_ufs_of_match,
	},
};
module_platform_driver(ti_j721e_ufs_driver);

MODULE_AUTHOR("Vignesh Raghavendra <vigneshr@ti.com>");
MODULE_DESCRIPTION("TI UFS host controller glue driver");
MODULE_LICENSE("GPL v2");
