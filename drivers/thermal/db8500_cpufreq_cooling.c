/*
 * db8500_cpufreq_cooling.c - DB8500 cpufreq works as cooling device.
 *
 * Copyright (C) 2012 ST-Ericsson
 * Copyright (C) 2012 Linaro Ltd.
 *
 * Author: Hongbo Zhang <hongbo.zhang@linaro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/cpu_cooling.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static int db8500_cpufreq_cooling_probe(struct platform_device *pdev)
{
	struct thermal_cooling_device *cdev;

	cdev = cpufreq_cooling_register(cpu_present_mask);
	if (IS_ERR(cdev)) {
		dev_err(&pdev->dev, "Failed to register cooling device\n");
		return PTR_ERR(cdev);
	}

	platform_set_drvdata(pdev, cdev);

	dev_info(&pdev->dev, "Cooling device registered: %s\n",	cdev->type);

	return 0;
}

static int db8500_cpufreq_cooling_remove(struct platform_device *pdev)
{
	struct thermal_cooling_device *cdev = platform_get_drvdata(pdev);

	cpufreq_cooling_unregister(cdev);

	return 0;
}

static int db8500_cpufreq_cooling_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	return -ENOSYS;
}

static int db8500_cpufreq_cooling_resume(struct platform_device *pdev)
{
	return -ENOSYS;
}

#ifdef CONFIG_OF
static const struct of_device_id db8500_cpufreq_cooling_match[] = {
	{ .compatible = "stericsson,db8500-cpufreq-cooling" },
	{},
};
#endif

static struct platform_driver db8500_cpufreq_cooling_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "db8500-cpufreq-cooling",
		.of_match_table = of_match_ptr(db8500_cpufreq_cooling_match),
	},
	.probe = db8500_cpufreq_cooling_probe,
	.suspend = db8500_cpufreq_cooling_suspend,
	.resume = db8500_cpufreq_cooling_resume,
	.remove = db8500_cpufreq_cooling_remove,
};

static int __init db8500_cpufreq_cooling_init(void)
{
	return platform_driver_register(&db8500_cpufreq_cooling_driver);
}

static void __exit db8500_cpufreq_cooling_exit(void)
{
	platform_driver_unregister(&db8500_cpufreq_cooling_driver);
}

/* Should be later than db8500_cpufreq_register */
late_initcall(db8500_cpufreq_cooling_init);
module_exit(db8500_cpufreq_cooling_exit);

MODULE_AUTHOR("Hongbo Zhang <hongbo.zhang@stericsson.com>");
MODULE_DESCRIPTION("DB8500 cpufreq cooling driver");
MODULE_LICENSE("GPL");
