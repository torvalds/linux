// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Board info for Asus X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/gpio/machine.h>
#include <linux/input.h>
#include <linux/platform_device.h>

#include "shared-psy-info.h"
#include "x86-android-tablets.h"

/* Asus ME176C and TF103C tablets shared data */
static struct gpiod_lookup_table int3496_gpo2_pin22_gpios = {
	.dev_id = "intel-int3496",
	.table = {
		GPIO_LOOKUP("INT33FC:02", 22, "id", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static const struct x86_gpio_button asus_me176c_tf103c_lid __initconst = {
	.button = {
		.code = SW_LID,
		.active_low = true,
		.desc = "lid_sw",
		.type = EV_SW,
		.wakeup = true,
		.debounce_interval = 50,
	},
	.chip = "INT33FC:02",
	.pin = 12,
};

/* Asus ME176C tablets have an Android factory img with everything hardcoded */
static const char * const asus_me176c_accel_mount_matrix[] = {
	"-1", "0", "0",
	"0", "1", "0",
	"0", "0", "1"
};

static const struct property_entry asus_me176c_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", asus_me176c_accel_mount_matrix),
	{ }
};

static const struct software_node asus_me176c_accel_node = {
	.properties = asus_me176c_accel_props,
};

static const struct property_entry asus_me176c_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY_LEN("supplied-from", tusb1211_chg_det_psy, 1),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_U32("ti,system-minimum-microvolt", 3600000),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node asus_me176c_bq24190_node = {
	.properties = asus_me176c_bq24190_props,
};

static const struct property_entry asus_me176c_ug3105_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY_LEN("supplied-from", bq24190_psy, 1),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_hv_4v35_battery_node),
	PROPERTY_ENTRY_U32("upisemi,rsns-microohm", 10000),
	{ }
};

static const struct software_node asus_me176c_ug3105_node = {
	.properties = asus_me176c_ug3105_props,
};

static const struct x86_i2c_client_info asus_me176c_i2c_clients[] __initconst = {
	{
		/* bq24297 battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24297",
			.swnode = &asus_me176c_bq24190_node,
			.platform_data = &bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_PMIC,
			.chip = "\\_SB_.I2C7.PMIC",
			.domain = DOMAIN_BUS_WAKEUP,
			.index = 0,
		},
	}, {
		/* ug3105 battery monitor */
		.board_info = {
			.type = "ug3105",
			.addr = 0x70,
			.dev_name = "ug3105",
			.swnode = &asus_me176c_ug3105_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* ak09911 compass */
		.board_info = {
			.type = "ak09911",
			.addr = 0x0c,
			.dev_name = "ak09911",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* kxtj21009 accel */
		.board_info = {
			.type = "kxtj21009",
			.addr = 0x0f,
			.dev_name = "kxtj21009",
			.swnode = &asus_me176c_accel_node,
		},
		.adapter_path = "\\_SB_.I2C5",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x44,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	}, {
		/* goodix touchscreen */
		.board_info = {
			.type = "GDIX1001:00",
			.addr = 0x14,
			.dev_name = "goodix_ts",
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_APIC,
			.index = 0x45,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	},
};

static const struct x86_serdev_info asus_me176c_serdevs[] __initconst = {
	{
		.ctrl_hid = "80860F0A",
		.ctrl_uid = "2",
		.ctrl_devname = "serial0",
		.serdev_hid = "BCM2E3A",
	},
};

static struct gpiod_lookup_table asus_me176c_goodix_gpios = {
	.dev_id = "i2c-goodix_ts",
	.table = {
		GPIO_LOOKUP("INT33FC:00", 60, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FC:02", 28, "irq", GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct gpiod_lookup_table * const asus_me176c_gpios[] = {
	&int3496_gpo2_pin22_gpios,
	&asus_me176c_goodix_gpios,
	NULL
};

const struct x86_dev_info asus_me176c_info __initconst = {
	.i2c_client_info = asus_me176c_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(asus_me176c_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = 1,
	.serdev_info = asus_me176c_serdevs,
	.serdev_count = ARRAY_SIZE(asus_me176c_serdevs),
	.gpio_button = &asus_me176c_tf103c_lid,
	.gpio_button_count = 1,
	.gpiod_lookup_tables = asus_me176c_gpios,
	.bat_swnode = &generic_lipo_hv_4v35_battery_node,
	.modules = bq24190_modules,
};

/* Asus TF103C tablets have an Android factory img with everything hardcoded */
static const char * const asus_tf103c_accel_mount_matrix[] = {
	"0", "-1", "0",
	"-1", "0", "0",
	"0", "0", "1"
};

static const struct property_entry asus_tf103c_accel_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", asus_tf103c_accel_mount_matrix),
	{ }
};

static const struct software_node asus_tf103c_accel_node = {
	.properties = asus_tf103c_accel_props,
};

static const struct property_entry asus_tf103c_touchscreen_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "atmel,atmel_mxt_ts"),
	{ }
};

static const struct software_node asus_tf103c_touchscreen_node = {
	.properties = asus_tf103c_touchscreen_props,
};

static const struct property_entry asus_tf103c_battery_props[] = {
	PROPERTY_ENTRY_STRING("compatible", "simple-battery"),
	PROPERTY_ENTRY_STRING("device-chemistry", "lithium-ion-polymer"),
	PROPERTY_ENTRY_U32("precharge-current-microamp", 256000),
	PROPERTY_ENTRY_U32("charge-term-current-microamp", 128000),
	PROPERTY_ENTRY_U32("constant-charge-current-max-microamp", 2048000),
	PROPERTY_ENTRY_U32("constant-charge-voltage-max-microvolt", 4208000),
	PROPERTY_ENTRY_U32("factory-internal-resistance-micro-ohms", 150000),
	{ }
};

static const struct software_node asus_tf103c_battery_node = {
	.properties = asus_tf103c_battery_props,
};

static const struct property_entry asus_tf103c_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY_LEN("supplied-from", tusb1211_chg_det_psy, 1),
	PROPERTY_ENTRY_REF("monitored-battery", &asus_tf103c_battery_node),
	PROPERTY_ENTRY_U32("ti,system-minimum-microvolt", 3600000),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node asus_tf103c_bq24190_node = {
	.properties = asus_tf103c_bq24190_props,
};

static const struct property_entry asus_tf103c_ug3105_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY_LEN("supplied-from", bq24190_psy, 1),
	PROPERTY_ENTRY_REF("monitored-battery", &asus_tf103c_battery_node),
	PROPERTY_ENTRY_U32("upisemi,rsns-microohm", 5000),
	{ }
};

static const struct software_node asus_tf103c_ug3105_node = {
	.properties = asus_tf103c_ug3105_props,
};

static const struct x86_i2c_client_info asus_tf103c_i2c_clients[] __initconst = {
	{
		/* bq24297 battery charger */
		.board_info = {
			.type = "bq24190",
			.addr = 0x6b,
			.dev_name = "bq24297",
			.swnode = &asus_tf103c_bq24190_node,
			.platform_data = &bq24190_pdata,
		},
		.adapter_path = "\\_SB_.I2C1",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_PMIC,
			.chip = "\\_SB_.I2C7.PMIC",
			.domain = DOMAIN_BUS_WAKEUP,
			.index = 0,
		},
	}, {
		/* ug3105 battery monitor */
		.board_info = {
			.type = "ug3105",
			.addr = 0x70,
			.dev_name = "ug3105",
			.swnode = &asus_tf103c_ug3105_node,
		},
		.adapter_path = "\\_SB_.I2C1",
	}, {
		/* ak09911 compass */
		.board_info = {
			.type = "ak09911",
			.addr = 0x0c,
			.dev_name = "ak09911",
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* kxtj21009 accel */
		.board_info = {
			.type = "kxtj21009",
			.addr = 0x0f,
			.dev_name = "kxtj21009",
			.swnode = &asus_tf103c_accel_node,
		},
		.adapter_path = "\\_SB_.I2C5",
	}, {
		/* atmel touchscreen */
		.board_info = {
			.type = "atmel_mxt_ts",
			.addr = 0x4a,
			.dev_name = "atmel_mxt_ts",
			.swnode = &asus_tf103c_touchscreen_node,
		},
		.adapter_path = "\\_SB_.I2C6",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 28,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
		},
	},
};

static struct gpiod_lookup_table * const asus_tf103c_gpios[] = {
	&int3496_gpo2_pin22_gpios,
	NULL
};

const struct x86_dev_info asus_tf103c_info __initconst = {
	.i2c_client_info = asus_tf103c_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(asus_tf103c_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = 1,
	.gpio_button = &asus_me176c_tf103c_lid,
	.gpio_button_count = 1,
	.gpiod_lookup_tables = asus_tf103c_gpios,
	.bat_swnode = &asus_tf103c_battery_node,
	.modules = bq24190_modules,
};
