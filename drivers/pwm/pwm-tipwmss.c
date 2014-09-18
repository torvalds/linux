/*
 * TI PWM Subsystem driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>

#include "pwm-tipwmss.h"

#define PWMSS_CLKCONFIG		0x8	/* Clock gating reg */
#define PWMSS_CLKSTATUS		0xc	/* Clock gating status reg */

struct pwmss_info {
	void __iomem	*mmio_base;
	struct mutex	pwmss_lock;
	u16		pwmss_clkconfig;
};

u16 pwmss_submodule_state_change(struct device *dev, int set)
{
	struct pwmss_info *info = dev_get_drvdata(dev);
	u16 val;

	mutex_lock(&info->pwmss_lock);
	val = readw(info->mmio_base + PWMSS_CLKCONFIG);
	val |= set;
	writew(val , info->mmio_base + PWMSS_CLKCONFIG);
	mutex_unlock(&info->pwmss_lock);

	return readw(info->mmio_base + PWMSS_CLKSTATUS);
}
EXPORT_SYMBOL(pwmss_submodule_state_change);

static const struct of_device_id pwmss_of_match[] = {
	{ .compatible	= "ti,am33xx-pwmss" },
	{},
};
MODULE_DEVICE_TABLE(of, pwmss_of_match);

static int pwmss_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;
	struct pwmss_info *info;
	struct device_node *node = pdev->dev.of_node;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->pwmss_lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->mmio_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(info->mmio_base))
		return PTR_ERR(info->mmio_base);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
	platform_set_drvdata(pdev, info);

	/* Populate all the child nodes here... */
	ret = of_platform_populate(node, NULL, NULL, &pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "no child node found\n");

	return ret;
}

static int pwmss_remove(struct platform_device *pdev)
{
	struct pwmss_info *info = platform_get_drvdata(pdev);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&info->pwmss_lock);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pwmss_suspend(struct device *dev)
{
	struct pwmss_info *info = dev_get_drvdata(dev);

	info->pwmss_clkconfig = readw(info->mmio_base + PWMSS_CLKCONFIG);
	pm_runtime_put_sync(dev);
	return 0;
}

static int pwmss_resume(struct device *dev)
{
	struct pwmss_info *info = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	writew(info->pwmss_clkconfig, info->mmio_base + PWMSS_CLKCONFIG);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pwmss_pm_ops, pwmss_suspend, pwmss_resume);

static struct platform_driver pwmss_driver = {
	.driver	= {
		.name	= "pwmss",
		.owner	= THIS_MODULE,
		.pm	= &pwmss_pm_ops,
		.of_match_table	= pwmss_of_match,
	},
	.probe	= pwmss_probe,
	.remove	= pwmss_remove,
};

module_platform_driver(pwmss_driver);

MODULE_DESCRIPTION("PWM Subsystem driver");
MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");
