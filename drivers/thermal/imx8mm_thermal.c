// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 NXP.
 *
 * Author: Anson Huang <Anson.Huang@nxp.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "thermal_core.h"

#define TER			0x0	/* TMU enable */
#define TRITSR			0x20	/* TMU immediate temp */

#define TER_EN			BIT(31)
#define TRITSR_VAL_MASK		0xff

#define TEMP_LOW_LIMIT		10

struct imx8mm_tmu {
	struct thermal_zone_device *tzd;
	void __iomem *base;
	struct clk *clk;
};

static int tmu_get_temp(void *data, int *temp)
{
	struct imx8mm_tmu *tmu = data;
	u32 val;

	val = readl_relaxed(tmu->base + TRITSR) & TRITSR_VAL_MASK;
	if (val < TEMP_LOW_LIMIT)
		return -EAGAIN;

	*temp = val * 1000;

	return 0;
}

static struct thermal_zone_of_device_ops tmu_tz_ops = {
	.get_temp = tmu_get_temp,
};

static int imx8mm_tmu_probe(struct platform_device *pdev)
{
	struct imx8mm_tmu *tmu;
	u32 val;
	int ret;

	tmu = devm_kzalloc(&pdev->dev, sizeof(struct imx8mm_tmu), GFP_KERNEL);
	if (!tmu)
		return -ENOMEM;

	tmu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tmu->base))
		return PTR_ERR(tmu->base);

	tmu->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tmu->clk)) {
		ret = PTR_ERR(tmu->clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to get tmu clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(tmu->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable tmu clock: %d\n", ret);
		return ret;
	}

	tmu->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev, 0,
							tmu, &tmu_tz_ops);
	if (IS_ERR(tmu->tzd)) {
		dev_err(&pdev->dev,
			"failed to register thermal zone sensor: %d\n", ret);
		return PTR_ERR(tmu->tzd);
	}

	platform_set_drvdata(pdev, tmu);

	/* enable the monitor */
	val = readl_relaxed(tmu->base + TER);
	val |= TER_EN;
	writel_relaxed(val, tmu->base + TER);

	return 0;
}

static int imx8mm_tmu_remove(struct platform_device *pdev)
{
	struct imx8mm_tmu *tmu = platform_get_drvdata(pdev);
	u32 val;

	/* disable TMU */
	val = readl_relaxed(tmu->base + TER);
	val &= ~TER_EN;
	writel_relaxed(val, tmu->base + TER);

	clk_disable_unprepare(tmu->clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id imx8mm_tmu_table[] = {
	{ .compatible = "fsl,imx8mm-tmu", },
	{ },
};

static struct platform_driver imx8mm_tmu = {
	.driver = {
		.name	= "i.mx8mm_thermal",
		.of_match_table = imx8mm_tmu_table,
	},
	.probe = imx8mm_tmu_probe,
	.remove = imx8mm_tmu_remove,
};
module_platform_driver(imx8mm_tmu);

MODULE_AUTHOR("Anson Huang <Anson.Huang@nxp.com>");
MODULE_DESCRIPTION("i.MX8MM Thermal Monitor Unit driver");
MODULE_LICENSE("GPL v2");
