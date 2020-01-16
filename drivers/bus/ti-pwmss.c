// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TI PWM Subsystem driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
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
	struct device_yesde *yesde = pdev->dev.of_yesde;

	pm_runtime_enable(&pdev->dev);

	/* Populate all the child yesdes here... */
	ret = of_platform_populate(yesde, NULL, NULL, &pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "yes child yesde found\n");

	return ret;
}

static int pwmss_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static struct platform_driver pwmss_driver = {
	.driver	= {
		.name	= "pwmss",
		.of_match_table	= pwmss_of_match,
	},
	.probe	= pwmss_probe,
	.remove	= pwmss_remove,
};

module_platform_driver(pwmss_driver);

MODULE_DESCRIPTION("PWM Subsystem driver");
MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");
