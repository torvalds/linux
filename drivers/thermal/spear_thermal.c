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
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/spear_thermal.h>
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
				unsigned long *temp)
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

#ifdef CONFIG_PM
static int spear_thermal_suspend(struct device *dev)
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

static int spear_thermal_resume(struct device *dev)
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
#endif

static SIMPLE_DEV_PM_OPS(spear_thermal_pm_ops, spear_thermal_suspend,
		spear_thermal_resume);

static int spear_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *spear_thermal = NULL;
	struct spear_thermal_dev *stdev;
	struct spear_thermal_pdata *pdata;
	int ret = 0;
	struct resource *stres = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!stres) {
		dev_err(&pdev->dev, "memory resource missing\n");
		return -ENODEV;
	}

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "platform data is NULL\n");
		return -EINVAL;
	}

	stdev = devm_kzalloc(&pdev->dev, sizeof(*stdev), GFP_KERNEL);
	if (!stdev) {
		dev_err(&pdev->dev, "kzalloc fail\n");
		return -ENOMEM;
	}

	/* Enable thermal sensor */
	stdev->thermal_base = devm_ioremap(&pdev->dev, stres->start,
			resource_size(stres));
	if (!stdev->thermal_base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	stdev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(stdev->clk)) {
		dev_err(&pdev->dev, "Can't get clock\n");
		return PTR_ERR(stdev->clk);
	}

	ret = clk_enable(stdev->clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable clock\n");
		goto put_clk;
	}

	stdev->flags = pdata->thermal_flags;
	writel_relaxed(stdev->flags, stdev->thermal_base);

	spear_thermal = thermal_zone_device_register("spear_thermal", 0, 0,
				stdev, &ops, 0, 0, 0, 0);
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
put_clk:
	clk_put(stdev->clk);

	return ret;
}

static int spear_thermal_exit(struct platform_device *pdev)
{
	unsigned int actual_mask = 0;
	struct thermal_zone_device *spear_thermal = platform_get_drvdata(pdev);
	struct spear_thermal_dev *stdev = spear_thermal->devdata;

	thermal_zone_device_unregister(spear_thermal);
	platform_set_drvdata(pdev, NULL);

	/* Disable SPEAr Thermal Sensor */
	actual_mask = readl_relaxed(stdev->thermal_base);
	writel_relaxed(actual_mask & ~stdev->flags, stdev->thermal_base);

	clk_disable(stdev->clk);
	clk_put(stdev->clk);

	return 0;
}

static struct platform_driver spear_thermal_driver = {
	.probe = spear_thermal_probe,
	.remove = spear_thermal_exit,
	.driver = {
		.name = "spear_thermal",
		.owner = THIS_MODULE,
		.pm = &spear_thermal_pm_ops,
	},
};

module_platform_driver(spear_thermal_driver);

MODULE_AUTHOR("Vincenzo Frascino <vincenzo.frascino@st.com>");
MODULE_DESCRIPTION("SPEAr thermal driver");
MODULE_LICENSE("GPL");
