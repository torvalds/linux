// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Surface GPE/Lid driver to enable wakeup from suspend via the lid by
 * properly configuring the respective GPEs. Required for wakeup via lid on
 * newer Intel-based Microsoft Surface devices.
 *
 * Copyright (C) 2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/*
 * Note: The GPE numbers for the lid devices found below have been obtained
 *       from ACPI/the DSDT table, specifically from the GPE handler for the
 *       lid.
 */

static const struct property_entry lid_device_props_l17[] = {
	PROPERTY_ENTRY_U32("gpe", 0x17),
	{},
};

static const struct property_entry lid_device_props_l4B[] = {
	PROPERTY_ENTRY_U32("gpe", 0x4B),
	{},
};

static const struct property_entry lid_device_props_l4D[] = {
	PROPERTY_ENTRY_U32("gpe", 0x4D),
	{},
};

static const struct property_entry lid_device_props_l4F[] = {
	PROPERTY_ENTRY_U32("gpe", 0x4F),
	{},
};

static const struct property_entry lid_device_props_l57[] = {
	PROPERTY_ENTRY_U32("gpe", 0x57),
	{},
};

/*
 * Note: When changing this, don't forget to check that the MODULE_ALIAS below
 *       still fits.
 */
static const struct dmi_system_id dmi_lid_device_table[] = {
	{
		.ident = "Surface Pro 4",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Pro 4"),
		},
		.driver_data = (void *)lid_device_props_l17,
	},
	{
		.ident = "Surface Pro 5",
		.matches = {
			/*
			 * We match for SKU here due to generic product name
			 * "Surface Pro".
			 */
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "Surface_Pro_1796"),
		},
		.driver_data = (void *)lid_device_props_l4F,
	},
	{
		.ident = "Surface Pro 5 (LTE)",
		.matches = {
			/*
			 * We match for SKU here due to generic product name
			 * "Surface Pro"
			 */
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "Surface_Pro_1807"),
		},
		.driver_data = (void *)lid_device_props_l4F,
	},
	{
		.ident = "Surface Pro 6",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Pro 6"),
		},
		.driver_data = (void *)lid_device_props_l4F,
	},
	{
		.ident = "Surface Pro 7",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Pro 7"),
		},
		.driver_data = (void *)lid_device_props_l4D,
	},
	{
		.ident = "Surface Book 1",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Book"),
		},
		.driver_data = (void *)lid_device_props_l17,
	},
	{
		.ident = "Surface Book 2",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Book 2"),
		},
		.driver_data = (void *)lid_device_props_l17,
	},
	{
		.ident = "Surface Book 3",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Book 3"),
		},
		.driver_data = (void *)lid_device_props_l4D,
	},
	{
		.ident = "Surface Laptop 1",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Laptop"),
		},
		.driver_data = (void *)lid_device_props_l57,
	},
	{
		.ident = "Surface Laptop 2",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Laptop 2"),
		},
		.driver_data = (void *)lid_device_props_l57,
	},
	{
		.ident = "Surface Laptop 3 (Intel 13\")",
		.matches = {
			/*
			 * We match for SKU here due to different variants: The
			 * AMD (15") version does not rely on GPEs.
			 */
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "Surface_Laptop_3_1867:1868"),
		},
		.driver_data = (void *)lid_device_props_l4D,
	},
	{
		.ident = "Surface Laptop 3 (Intel 15\")",
		.matches = {
			/*
			 * We match for SKU here due to different variants: The
			 * AMD (15") version does not rely on GPEs.
			 */
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "Surface_Laptop_3_1872"),
		},
		.driver_data = (void *)lid_device_props_l4D,
	},
	{
		.ident = "Surface Laptop Studio",
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Surface Laptop Studio"),
		},
		.driver_data = (void *)lid_device_props_l4B,
	},
	{ }
};

struct surface_lid_device {
	u32 gpe_number;
};

static int surface_lid_enable_wakeup(struct device *dev, bool enable)
{
	const struct surface_lid_device *lid = dev_get_drvdata(dev);
	int action = enable ? ACPI_GPE_ENABLE : ACPI_GPE_DISABLE;
	acpi_status status;

	status = acpi_set_gpe_wake_mask(NULL, lid->gpe_number, action);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "failed to set GPE wake mask: %s\n",
			acpi_format_exception(status));
		return -EINVAL;
	}

	return 0;
}

static int __maybe_unused surface_gpe_suspend(struct device *dev)
{
	return surface_lid_enable_wakeup(dev, true);
}

static int __maybe_unused surface_gpe_resume(struct device *dev)
{
	return surface_lid_enable_wakeup(dev, false);
}

static SIMPLE_DEV_PM_OPS(surface_gpe_pm, surface_gpe_suspend, surface_gpe_resume);

static int surface_gpe_probe(struct platform_device *pdev)
{
	struct surface_lid_device *lid;
	u32 gpe_number;
	acpi_status status;
	int ret;

	ret = device_property_read_u32(&pdev->dev, "gpe", &gpe_number);
	if (ret) {
		dev_err(&pdev->dev, "failed to read 'gpe' property: %d\n", ret);
		return ret;
	}

	lid = devm_kzalloc(&pdev->dev, sizeof(*lid), GFP_KERNEL);
	if (!lid)
		return -ENOMEM;

	lid->gpe_number = gpe_number;
	platform_set_drvdata(pdev, lid);

	status = acpi_mark_gpe_for_wake(NULL, gpe_number);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev, "failed to mark GPE for wake: %s\n",
			acpi_format_exception(status));
		return -EINVAL;
	}

	status = acpi_enable_gpe(NULL, gpe_number);
	if (ACPI_FAILURE(status)) {
		dev_err(&pdev->dev, "failed to enable GPE: %s\n",
			acpi_format_exception(status));
		return -EINVAL;
	}

	ret = surface_lid_enable_wakeup(&pdev->dev, false);
	if (ret)
		acpi_disable_gpe(NULL, gpe_number);

	return ret;
}

static int surface_gpe_remove(struct platform_device *pdev)
{
	struct surface_lid_device *lid = dev_get_drvdata(&pdev->dev);

	/* restore default behavior without this module */
	surface_lid_enable_wakeup(&pdev->dev, false);
	acpi_disable_gpe(NULL, lid->gpe_number);

	return 0;
}

static struct platform_driver surface_gpe_driver = {
	.probe = surface_gpe_probe,
	.remove = surface_gpe_remove,
	.driver = {
		.name = "surface_gpe",
		.pm = &surface_gpe_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static struct platform_device *surface_gpe_device;

static int __init surface_gpe_init(void)
{
	const struct dmi_system_id *match;
	struct platform_device *pdev;
	struct fwnode_handle *fwnode;
	int status;

	match = dmi_first_match(dmi_lid_device_table);
	if (!match) {
		pr_info("no compatible Microsoft Surface device found, exiting\n");
		return -ENODEV;
	}

	status = platform_driver_register(&surface_gpe_driver);
	if (status)
		return status;

	fwnode = fwnode_create_software_node(match->driver_data, NULL);
	if (IS_ERR(fwnode)) {
		status = PTR_ERR(fwnode);
		goto err_node;
	}

	pdev = platform_device_alloc("surface_gpe", PLATFORM_DEVID_NONE);
	if (!pdev) {
		status = -ENOMEM;
		goto err_alloc;
	}

	pdev->dev.fwnode = fwnode;

	status = platform_device_add(pdev);
	if (status)
		goto err_add;

	surface_gpe_device = pdev;
	return 0;

err_add:
	platform_device_put(pdev);
err_alloc:
	fwnode_remove_software_node(fwnode);
err_node:
	platform_driver_unregister(&surface_gpe_driver);
	return status;
}
module_init(surface_gpe_init);

static void __exit surface_gpe_exit(void)
{
	struct fwnode_handle *fwnode = surface_gpe_device->dev.fwnode;

	platform_device_unregister(surface_gpe_device);
	platform_driver_unregister(&surface_gpe_driver);
	fwnode_remove_software_node(fwnode);
}
module_exit(surface_gpe_exit);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Surface GPE/Lid Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dmi:*:svnMicrosoftCorporation:pnSurface*:*");
