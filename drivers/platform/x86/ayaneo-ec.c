// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform driver for the Embedded Controller (EC) of Ayaneo devices. Handles
 * hwmon (fan speed, fan control), battery charge limits, and magic module
 * control (connected modules, controller disconnection).
 *
 * Copyright (C) 2025 Antheas Kapenekakis <lkml@antheas.dev>
 */

#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct ayaneo_ec_quirk {
};

struct ayaneo_ec_platform_data {
	struct platform_device *pdev;
	struct ayaneo_ec_quirk *quirks;
};

static const struct ayaneo_ec_quirk quirk_ayaneo3 = {
};

static const struct dmi_system_id dmi_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "AYANEO 3"),
		},
		.driver_data = (void *)&quirk_ayaneo3,
	},
	{},
};

static int ayaneo_ec_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_entry;
	struct ayaneo_ec_platform_data *data;

	dmi_entry = dmi_first_match(dmi_table);
	if (!dmi_entry)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	data->quirks = dmi_entry->driver_data;
	platform_set_drvdata(pdev, data);

	return 0;
}

static struct platform_driver ayaneo_platform_driver = {
	.driver = {
		.name = "ayaneo-ec",
	},
	.probe = ayaneo_ec_probe,
};

static struct platform_device *ayaneo_platform_device;

static int __init ayaneo_ec_init(void)
{
	ayaneo_platform_device =
		platform_create_bundle(&ayaneo_platform_driver,
				       ayaneo_ec_probe, NULL, 0, NULL, 0);

	return PTR_ERR_OR_ZERO(ayaneo_platform_device);
}

static void __exit ayaneo_ec_exit(void)
{
	platform_device_unregister(ayaneo_platform_device);
	platform_driver_unregister(&ayaneo_platform_driver);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

module_init(ayaneo_ec_init);
module_exit(ayaneo_ec_exit);

MODULE_AUTHOR("Antheas Kapenekakis <lkml@antheas.dev>");
MODULE_DESCRIPTION("Ayaneo Embedded Controller (EC) platform features");
MODULE_LICENSE("GPL");
