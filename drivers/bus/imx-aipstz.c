// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 NXP
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#define IMX_AIPSTZ_MPR0 0x0

struct imx_aipstz_config {
	u32 mpr0;
};

struct imx_aipstz_data {
	void __iomem *base;
	const struct imx_aipstz_config *default_cfg;
};

static void imx_aipstz_apply_default(struct imx_aipstz_data *data)
{
	writel(data->default_cfg->mpr0, data->base + IMX_AIPSTZ_MPR0);
}

static const struct of_device_id imx_aipstz_match_table[] = {
	{ .compatible = "simple-bus", },
	{ }
};

static int imx_aipstz_probe(struct platform_device *pdev)
{
	struct imx_aipstz_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed to allocate data memory\n");

	data->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(data->base))
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed to get/ioremap AC memory\n");

	data->default_cfg = of_device_get_match_data(&pdev->dev);

	imx_aipstz_apply_default(data);

	dev_set_drvdata(&pdev->dev, data);

	pm_runtime_set_active(&pdev->dev);
	devm_pm_runtime_enable(&pdev->dev);

	return of_platform_populate(pdev->dev.of_node, imx_aipstz_match_table,
				    NULL, &pdev->dev);
}

static void imx_aipstz_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
}

static int imx_aipstz_runtime_resume(struct device *dev)
{
	struct imx_aipstz_data *data = dev_get_drvdata(dev);

	/* restore potentially lost configuration during domain power-off */
	imx_aipstz_apply_default(data);

	return 0;
}

static const struct dev_pm_ops imx_aipstz_pm_ops = {
	RUNTIME_PM_OPS(NULL, imx_aipstz_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

/*
 * following configuration is equivalent to:
 *	masters 0-7 => trusted for R/W + use AHB's HPROT[1] to det. privilege
 */
static const struct imx_aipstz_config imx8mp_aipstz_default_cfg = {
	.mpr0 = 0x77777777,
};

static const struct of_device_id imx_aipstz_of_ids[] = {
	{ .compatible = "fsl,imx8mp-aipstz", .data = &imx8mp_aipstz_default_cfg },
	{ }
};
MODULE_DEVICE_TABLE(of, imx_aipstz_of_ids);

static struct platform_driver imx_aipstz_of_driver = {
	.probe = imx_aipstz_probe,
	.remove = imx_aipstz_remove,
	.driver = {
		.name = "imx-aipstz",
		.of_match_table = imx_aipstz_of_ids,
		.pm = pm_ptr(&imx_aipstz_pm_ops),
	},
};
module_platform_driver(imx_aipstz_of_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IMX secure AHB to IP Slave bus (AIPSTZ) bridge driver");
MODULE_AUTHOR("Laurentiu Mihalcea <laurentiu.mihalcea@nxp.com>");
