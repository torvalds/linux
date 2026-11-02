// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * acer-wmi-battery.c: Acer battery health control driver
 *
 * This is a driver for the WMI battery health control interface found
 * on some Acer laptops.  This interface allows to enable/disable a
 * battery charge limit ("health mode") and exposes the battery temperature.
 *
 * Based on acer-wmi-battery https://github.com/frederik-h/acer-wmi-battery/
 *
 * Copyright (C) 2022-2025  Frederik Harwath <frederik@harwath.name>
 */

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/byteorder/generic.h>
#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/wmi.h>

#include <acpi/battery.h>

#define DRIVER_NAME	"acer-wmi-battery"

#define ACER_BATTERY_GUID "79772EC5-04B1-4BFD-843C-61E7F77B6CC9"

/*
 * The Acer OEM software seems to always use this battery index,
 * so we emulate this behaviour to not confuse the underlying firmware.
 *
 * However this also means that we only fully support devices with a
 * single battery for now.
 */
#define ACER_BATTERY_INDEX	0x1

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Enable health mode control without checking for supported devices\n");

struct get_battery_health_control_status_input {
	u8 battery_no;
	u8 function_query;
	u8 reserved[2];
} __packed;

struct get_battery_health_control_status_output {
	u8 function_list;
	u8 ret[2];
	u8 function_status[5];
} __packed;

struct set_battery_health_control_input {
	u8 battery_no;
	u8 function_mask;
	u8 function_status;
	u8 reserved_in[5];
} __packed;

struct set_battery_health_control_output {
	__le16 ret;
	__le16 reserved_out;
} __packed;

enum battery_mode {
	HEALTH_MODE = 1,
	CALIBRATION_MODE = 2,
};

struct acer_wmi_battery_data {
	struct acpi_battery_hook hook;
	struct wmi_device *wdev;
	const struct power_supply_ext *battery_ext;
};

static bool health_mode __ro_after_init;

static int acer_wmi_battery_get_information(struct acer_wmi_battery_data *data,
					    u32 index, u32 battery, u32 *result)
{
	u32 args[2] = { index, battery };
	struct wmi_buffer input = { .length = sizeof(args), .data = args };
	struct wmi_buffer output;
	int ret;

	ret = wmidev_invoke_method(data->wdev, 0, 19, &input, &output, sizeof(*result));
	if (ret)
		return ret;

	*result = le32_to_cpup((__le32 *)output.data);

	kfree(output.data);

	return 0;
}

static int acer_wmi_battery_get_health_control_status(struct acer_wmi_battery_data *data,
						      bool *health_mode)
{
	/*
	 * Acer Care Center seems to always call the WMI method
	 * with fixed parameters. This yields information about
	 * the availability and state of both health and
	 * calibration mode. The modes probably apply to
	 * all batteries of the system.
	 */
	struct get_battery_health_control_status_input args = {
		.battery_no = ACER_BATTERY_INDEX,
		.function_query = 0x1,
		.reserved = { 0x0, 0x0 },
	};
	struct wmi_buffer input = { .length = sizeof(args), .data = &args };
	struct wmi_buffer output;
	int ret;

	ret = wmidev_invoke_method(data->wdev, 0, 20, &input, &output,
				   sizeof(struct get_battery_health_control_status_output));
	if (ret)
		return ret;

	struct get_battery_health_control_status_output *status_output __free(kfree) = output.data;

	if (!(status_output->function_list & HEALTH_MODE))
		return -EINVAL;

	*health_mode = status_output->function_status[0] > 0;

	return 0;
}

static int acer_wmi_battery_set_health_control(struct acer_wmi_battery_data *data,
					       u8 function, bool function_status)
{
	struct set_battery_health_control_input args = {
		.battery_no = ACER_BATTERY_INDEX,
		.function_mask = function,
		.function_status = function_status ? 1 : 0,
		.reserved_in = { 0x0, 0x0, 0x0, 0x0, 0x0 },
	};
	struct wmi_buffer input = { .length = sizeof(args), .data = &args };
	struct wmi_buffer output;
	int ret;

	ret = wmidev_invoke_method(data->wdev, 0, 21, &input, &output,
				   sizeof(struct set_battery_health_control_output));
	if (ret)
		return ret;

	struct set_battery_health_control_output *status_output __free(kfree) = output.data;
	if (le16_to_cpu(status_output->ret) != 0)
		return -EIO;

	return 0;
}

static int acer_battery_ext_property_get(struct power_supply *psy,
					 const struct power_supply_ext *ext,
					 void *ext_data,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct acer_wmi_battery_data *data = ext_data;
	bool health_mode;
	u32 value;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPES:
		ret = acer_wmi_battery_get_health_control_status(data, &health_mode);
		if (ret)
			return ret;

		val->intval = health_mode ? POWER_SUPPLY_CHARGE_TYPE_LONGLIFE
					  : POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = acer_wmi_battery_get_information(data, 0x8, ACER_BATTERY_INDEX, &value);
		if (ret)
			return ret;

		if (value > U16_MAX)
			return -ERANGE;

		val->intval = value + ABSOLUTE_ZERO_MILLICELSIUS / MILLIDEGREE_PER_DECIDEGREE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int acer_battery_ext_property_set(struct power_supply *psy,
					 const struct power_supply_ext *ext,
					 void *ext_data,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct acer_wmi_battery_data *data = ext_data;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPES:
		return acer_wmi_battery_set_health_control(data, HEALTH_MODE,
				val->intval == POWER_SUPPLY_CHARGE_TYPE_LONGLIFE);
	default:
		return -EINVAL;
	}
}

static int acer_battery_ext_property_is_writeable(struct power_supply *psy,
						  const struct power_supply_ext *ext,
						  void *ext_data,
						  enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_TYPES:
		return true;
	default:
		return false;
	}
}

static const enum power_supply_property acer_battery_properties_v1[] = {
	POWER_SUPPLY_PROP_TEMP,
};

static const enum power_supply_property acer_battery_properties_v2[] = {
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_TYPES,
};

static const struct power_supply_ext acer_wmi_battery_extension_v1 = {
	.name			= DRIVER_NAME,
	.properties		= acer_battery_properties_v1,
	.num_properties		= ARRAY_SIZE(acer_battery_properties_v1),
	.get_property		= acer_battery_ext_property_get,
};

static const struct power_supply_ext acer_wmi_battery_extension_v2 = {
	.name			= DRIVER_NAME,
	.properties		= acer_battery_properties_v2,
	.num_properties		= ARRAY_SIZE(acer_battery_properties_v2),
	.charge_types           = BIT(POWER_SUPPLY_CHARGE_TYPE_STANDARD) |
				  BIT(POWER_SUPPLY_CHARGE_TYPE_LONGLIFE),
	.get_property		= acer_battery_ext_property_get,
	.set_property		= acer_battery_ext_property_set,
	.property_is_writeable	= acer_battery_ext_property_is_writeable,
};

static int acer_battery_add(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct acer_wmi_battery_data *data = container_of(hook, struct acer_wmi_battery_data, hook);

	return power_supply_register_extension(battery, data->battery_ext,
					       &data->wdev->dev, data);
}

static int acer_battery_remove(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct acer_wmi_battery_data *data = container_of(hook, struct acer_wmi_battery_data, hook);

	power_supply_unregister_extension(battery, data->battery_ext);

	return 0;
}

static int acer_wmi_battery_probe(struct wmi_device *wdev, const void *context)
{
	struct acer_wmi_battery_data *data;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, data);
	data->wdev = wdev;
	data->battery_ext = health_mode ? &acer_wmi_battery_extension_v2
					: &acer_wmi_battery_extension_v1;
	data->hook.name = "Acer Battery Extension";
	data->hook.add_battery = acer_battery_add;
	data->hook.remove_battery = acer_battery_remove;

	return devm_battery_hook_register(&data->wdev->dev, &data->hook);
}

static const struct wmi_device_id acer_wmi_battery_id_table[] = {
	{ ACER_BATTERY_GUID, NULL },
	{ }
};
MODULE_DEVICE_TABLE(wmi, acer_wmi_battery_id_table);

static struct wmi_driver acer_wmi_battery_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = acer_wmi_battery_id_table,
	.probe = acer_wmi_battery_probe,
	.no_singleton = true,
};

static const struct dmi_system_id acer_wmi_battery_health_mode_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A315-510P"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A315-24PT"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A315-44P"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A315-58G"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A315-59"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A315-59"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A715-42G"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Nitro ANV15-51"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Nitro AN515-57"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Nitro AN515-58"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Nitro AN517-54"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Nitro AN517-54"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Predator PHN16-71"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Swift SF314-34"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Swift SF314-43"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Swift SFE16-44"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Swift SFG16-72"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Swift SFX14-71G"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Swift SFX16-61G"),
		},
	},
	{}
};

static int __init acer_wmi_battery_module_init(void)
{
	if (dmi_check_system(acer_wmi_battery_health_mode_table) || force)
		health_mode = true;
	else
		health_mode = false;

	return wmi_driver_register(&acer_wmi_battery_driver);
}

static void __exit acer_wmi_battery_module_exit(void)
{
	wmi_driver_unregister(&acer_wmi_battery_driver);
}

module_init(acer_wmi_battery_module_init);
module_exit(acer_wmi_battery_module_exit);

MODULE_AUTHOR("Frederik Harwath <frederik@harwath.name>");
MODULE_AUTHOR("Jelle van der Waa <jelle@vdwaa.nl>");
MODULE_DESCRIPTION("Acer battery health control WMI driver");
MODULE_LICENSE("GPL");
