// SPDX-License-Identifier: GPL-2.0-only
/*
 * Force-disables a regulator to power down a device
 *
 * Michael Klein <michael@fossekall.de>
 *
 * Copyright (C) 2020 Michael Klein
 *
 * Based on the gpio-poweroff driver.
 */
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#define TIMEOUT_MS 3000

/*
 * Hold configuration here, cannot be more than one instance of the driver
 * since pm_power_off itself is global.
 */
static struct regulator *cpu_regulator;

static void regulator_poweroff_do_poweroff(void)
{
	if (cpu_regulator && regulator_is_enabled(cpu_regulator))
		regulator_force_disable(cpu_regulator);

	/* give it some time */
	mdelay(TIMEOUT_MS);

	WARN_ON(1);
}

static int regulator_poweroff_probe(struct platform_device *pdev)
{
	/* If a pm_power_off function has already been added, leave it alone */
	if (pm_power_off != NULL) {
		dev_err(&pdev->dev,
			"%s: pm_power_off function already registered\n",
			__func__);
		return -EBUSY;
	}

	cpu_regulator = devm_regulator_get(&pdev->dev, "cpu");
	if (IS_ERR(cpu_regulator))
		return PTR_ERR(cpu_regulator);

	pm_power_off = &regulator_poweroff_do_poweroff;
	return 0;
}

static int regulator_poweroff_remove(__maybe_unused struct platform_device *pdev)
{
	if (pm_power_off == &regulator_poweroff_do_poweroff)
		pm_power_off = NULL;

	return 0;
}

static const struct of_device_id of_regulator_poweroff_match[] = {
	{ .compatible = "regulator-poweroff", },
	{},
};

static struct platform_driver regulator_poweroff_driver = {
	.probe = regulator_poweroff_probe,
	.remove = regulator_poweroff_remove,
	.driver = {
		.name = "poweroff-regulator",
		.of_match_table = of_regulator_poweroff_match,
	},
};

module_platform_driver(regulator_poweroff_driver);

MODULE_AUTHOR("Michael Klein <michael@fossekall.de>");
MODULE_DESCRIPTION("Regulator poweroff driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:poweroff-regulator");
