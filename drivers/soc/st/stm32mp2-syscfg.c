// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Marek Vasut
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static int stm32mp2_syscfg_probe(struct platform_device *pdev)
{
	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id stm32mp2_syscfg_ids[] = {
	{ .compatible = "st,stm32mp23-syscfg" },
	{ .compatible = "st,stm32mp25-syscfg" },
	{ }
};
MODULE_DEVICE_TABLE(of, stm32mp2_syscfg_ids);

static struct platform_driver stm32mp2_syscfg_driver = {
	.driver = {
		.name	= "stm32mp2_syscfg",
		.of_match_table = stm32mp2_syscfg_ids,
	},
	.probe = stm32mp2_syscfg_probe,
};
module_platform_driver(stm32mp2_syscfg_driver);

MODULE_AUTHOR("Marek Vasut <marex@nabladev.com>");
MODULE_DESCRIPTION("ST STM32MP2 SYSCFG driver");
MODULE_LICENSE("GPL");
