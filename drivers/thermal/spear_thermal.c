/*
 * SPEAr thermal driver.
 *
 * Copyright (C) 2011-2012 ST Microelectronics
 * Author: Vincenzo Frascino <vincenzo.frascino@st.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#define MD_FACTOR	1000

/* SPEAr Thermal Sensor Dev Structure */
struct spear_thermal_dev {
	/* pointer to base address of the thermal sensor */
	void __iomem *thermal_base;
	/* clk structure */
	struct clk *clk;
	/* pointer to thermal flags */
	unsigned int flags;
};

static inline int thermal_get_temp(struct thermal_zone_device *thermal,
				int *temp)
{
	struct spear_thermal_dev *stdev = thermal->devdata;

	/*
	 * Data are ready to be read after 628 usec from POWERDOWN signal
	 * (PDN) = 1
	 */
	*temp = (readl_relaxed(stdev->thermal_base) & 0x7F) * MD_FACTOR;
	return 0;
}

static struct thermal_zone_device_ops ops = {
	.get_temp = thermal_get_temp,
};

static int __maybe_unused spear_thermal_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct thermal_zone_device *spear_thermal = platform_get_drvdata(pdev);
	struct spear_thermal_dev *stdev = spear_thermal->devdata;
	unsigned int actual_mask = 0;

	/* Disable SPEAr Thermal Sensor */
	actual_mask = readl_relaxed(stdev->thermal_base);
	writel_relaxed(actual_mask & ~stdev->flags, stdev->thermal_base);

	clk_disable(stdev->clk);
	dev_info(dev, "Suspended.\n");

	return 0;
}

static int __maybe_unused spear_thermal_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct thermal_zone_device *spear_thermal = platform_get_drvdata(pdev);
	struct spear_thermal_dev *stdev = spear_thermal->devdata;
	unsigned int actual_mask = 0;
	int ret = 0;

	ret = clk_enable(stdev->clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable clock\n");
		return ret;
	}

	/* Enable SPEAr Thermal Sensor */
	actual_mask = readl_relaxed(stdev->thermal_base);
	writel_relaxed(actual_mask | stdev->flags, stdev->thermal_base);

	dev_info(dev, "Resumed.\n");

	return 0;
}

static SIMPLE_DEV_PM_OPS(spear_thermal_pm_ops, spear_thermal_suspend,
		spear_thermal_resume);

static int spear_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *spear_thermal = NULL;
	struct spear_thermal_dev *stdev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	int ret = 0, val;

	if (!np || !of_property_read_u32(np, "st,thermal-flags", &val)) {
		dev_err(&pdev->dev, "Failed: DT Pdata not passed\n");
		return -EINVAL;
	}

	stdev = devm_kzalloc(&pdev->dev, sizeof(*stdev), GFP_KERNEL);
	if (!stdev)
		return -ENOMEM;

	/* Enable thermal sensor */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	stdev->thermal_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(stdev->thermal_base))
		return PTR_ERR(stdev->thermal_base);

	stdev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(stdev->clk)) {
		dev_err(&pdev->dev, "Can't get clock\n");
		return PTR_ERR(stdev->clk);
	}

	ret = clk_enable(stdev->clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable clock\n");
		return ret;
	}

	stdev->flags = val;
	writel_relaxed(stdev->flags, stdev->thermal_base);

	spear_thermal = thermal_zone_device_register("spear_thermal", 0, 0,
				stdev, &ops, NULL, 0, 0);
	if (IS_ERR(spear_thermal)) {
		dev_err(&pdev->dev, "thermal zone device is NULL\n");
		ret = PTR_ERR(spear_thermal);
		goto disable_clk;
	}

	platform_set_drvdata(pdev, spear_thermal);

	dev_info(&spear_thermal->device, "Thermal Sensor Loaded at: 0x%p.\n",
			stdev->thermal_base);

	return 0;

disable_clk:
	clk_disable(stdev->clk);

	return ret;
}

static int spear_thermal_exit(struct platform_device *pdev)
{
	unsigned int actual_mask = 0;
	struct thermal_zone_device *spear_thermal = platform_get_drvdata(pdev);
	struct spear_thermal_dev *stdev = spear_thermal->devdata;

	thermal_zone_device_unregister(spear_thermal);

	/* Disable SPEAr Thermal Sensor */
	actual_mask = readl_relaxed(stdev->thermal_base);
	writel_relaxed(actual_mask & ~stdev->flags, stdev->thermal_base);

	clk_disable(stdev->clk);

	return 0;
}

static const struct of_device_id spear_thermal_id_table[] = {
	{ .compatible = "st,thermal-spear1340" },
	{}
};
MODULE_DEVICE_TABLE(of, spear_thermal_id_table);

static struct platform_driver spear_thermal_driver = {
	.probe = spear_thermal_probe,
	.remove = spear_thermal_exit,
	.driver = {
		.name = "spear_thermal",
		.pm = &spear_thermal_pm_ops,
		.of_match_table = spear_thermal_id_table,
	},
};

module_platform_driver(spear_thermal_driver);

MODULE_AUTHOR("Vincenzo Frascino <vincenzo.frascino@st.com>");
MODULE_DESCRIPTION("SPEAr thermal driver");
MODULE_LICENSE("GPL");
