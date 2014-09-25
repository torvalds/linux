/*
 *  Driver for Samsung Q10 and related laptops: controls the backlight
 *
 *  Copyright (c) 2011 Frederick van der Wyck <fvanderwyck@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/dmi.h>
#include <linux/acpi.h>

#define SAMSUNGQ10_BL_MAX_INTENSITY 7

static acpi_handle ec_handle;

static bool force;
module_param(force, bool, 0);
MODULE_PARM_DESC(force,
		"Disable the DMI check and force the driver to be loaded");

static int samsungq10_bl_set_intensity(struct backlight_device *bd)
{

	acpi_status status;
	int i;

	for (i = 0; i < SAMSUNGQ10_BL_MAX_INTENSITY; i++) {
		status = acpi_evaluate_object(ec_handle, "_Q63", NULL, NULL);
		if (ACPI_FAILURE(status))
			return -EIO;
	}
	for (i = 0; i < bd->props.brightness; i++) {
		status = acpi_evaluate_object(ec_handle, "_Q64", NULL, NULL);
		if (ACPI_FAILURE(status))
			return -EIO;
	}

	return 0;
}

static const struct backlight_ops samsungq10_bl_ops = {
	.update_status	= samsungq10_bl_set_intensity,
};

static int samsungq10_probe(struct platform_device *pdev)
{

	struct backlight_properties props;
	struct backlight_device *bd;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = SAMSUNGQ10_BL_MAX_INTENSITY;
	bd = backlight_device_register("samsung", &pdev->dev, NULL,
				       &samsungq10_bl_ops, &props);
	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);

	return 0;
}

static int samsungq10_remove(struct platform_device *pdev)
{

	struct backlight_device *bd = platform_get_drvdata(pdev);

	backlight_device_unregister(bd);

	return 0;
}

static struct platform_driver samsungq10_driver = {
	.driver		= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.probe		= samsungq10_probe,
	.remove		= samsungq10_remove,
};

static struct platform_device *samsungq10_device;

static int __init dmi_check_callback(const struct dmi_system_id *id)
{
	printk(KERN_INFO KBUILD_MODNAME ": found model '%s'\n", id->ident);
	return 1;
}

static struct dmi_system_id __initdata samsungq10_dmi_table[] = {
	{
		.ident = "Samsung Q10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Samsung"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SQ10"),
		},
		.callback = dmi_check_callback,
	},
	{
		.ident = "Samsung Q20",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG Electronics"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SENS Q20"),
		},
		.callback = dmi_check_callback,
	},
	{
		.ident = "Samsung Q25",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG Electronics"),
			DMI_MATCH(DMI_PRODUCT_NAME, "NQ25"),
		},
		.callback = dmi_check_callback,
	},
	{
		.ident = "Dell Latitude X200",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X200"),
		},
		.callback = dmi_check_callback,
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, samsungq10_dmi_table);

static int __init samsungq10_init(void)
{
	if (!force && !dmi_check_system(samsungq10_dmi_table))
		return -ENODEV;

	ec_handle = ec_get_handle();

	if (!ec_handle)
		return -ENODEV;

	samsungq10_device = platform_create_bundle(&samsungq10_driver,
						   samsungq10_probe,
						   NULL, 0, NULL, 0);

	return PTR_ERR_OR_ZERO(samsungq10_device);
}

static void __exit samsungq10_exit(void)
{
	platform_device_unregister(samsungq10_device);
	platform_driver_unregister(&samsungq10_driver);
}

module_init(samsungq10_init);
module_exit(samsungq10_exit);

MODULE_AUTHOR("Frederick van der Wyck <fvanderwyck@gmail.com>");
MODULE_DESCRIPTION("Samsung Q10 Driver");
MODULE_LICENSE("GPL");
