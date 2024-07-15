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
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>

#define TIMEOUT_MS 3000

static int regulator_poweroff_do_poweroff(struct sys_off_data *data)
{
	struct regulator *cpu_regulator = data->cb_data;

	if (cpu_regulator && regulator_is_enabled(cpu_regulator))
		regulator_force_disable(cpu_regulator);

	/* give it some time */
	mdelay(TIMEOUT_MS);

	WARN_ON(1);

	return NOTIFY_DONE;
}

static int regulator_poweroff_probe(struct platform_device *pdev)
{
	struct regulator *cpu_regulator;

	cpu_regulator = devm_regulator_get(&pdev->dev, "cpu");
	if (IS_ERR(cpu_regulator))
		return PTR_ERR(cpu_regulator);

	/* Set this handler to low priority to not override an existing handler */
	return devm_register_sys_off_handler(&pdev->dev,
					     SYS_OFF_MODE_POWER_OFF,
					     SYS_OFF_PRIO_LOW,
					     regulator_poweroff_do_poweroff,
					     cpu_regulator);
}

static const struct of_device_id of_regulator_poweroff_match[] = {
	{ .compatible = "regulator-poweroff", },
	{},
};
MODULE_DEVICE_TABLE(of, of_regulator_poweroff_match);

static struct platform_driver regulator_poweroff_driver = {
	.probe = regulator_poweroff_probe,
	.driver = {
		.name = "poweroff-regulator",
		.of_match_table = of_regulator_poweroff_match,
	},
};

module_platform_driver(regulator_poweroff_driver);

MODULE_AUTHOR("Michael Klein <michael@fossekall.de>");
MODULE_DESCRIPTION("Regulator poweroff driver");
MODULE_ALIAS("platform:poweroff-regulator");
