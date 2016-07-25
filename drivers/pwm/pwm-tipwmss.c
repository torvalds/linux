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

static const struct of_device_id pwmss_of_match[] = {
	{ .compatible	= "ti,am33xx-pwmss" },
	{},
};
MODULE_DEVICE_TABLE(of, pwmss_of_match);

static int pwmss_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	/* Populate all the child nodes here... */
	ret = of_platform_populate(node, NULL, NULL, &pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "no child node found\n");

	return ret;
}

static int pwmss_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pwmss_suspend(struct device *dev)
{
	pm_runtime_put_sync(dev);
	return 0;
}

static int pwmss_resume(struct device *dev)
{
	pm_runtime_get_sync(dev);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pwmss_pm_ops, pwmss_suspend, pwmss_resume);

static struct platform_driver pwmss_driver = {
	.driver	= {
		.name	= "pwmss",
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
