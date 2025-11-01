// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Board info for Asus X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hansg@kernel.org>
 */

#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/input-event-codes.h>
#include <linux/platform_device.h>

#include "shared-psy-info.h"
#include "x86-android-tablets.h"

/* Asus ME176C and TF103C tablets shared data */
static const struct property_entry asus_me176c_tf103c_int3496_props[] __initconst = {
	PROPERTY_ENTRY_GPIO("id-gpios", &baytrail_gpiochip_nodes[2], 22, GPIO_ACTIVE_HIGH),
	{ }
};

static const struct platform_device_info asus_me176c_tf103c_pdevs[] __initconst = {
	{
		/* For micro USB ID pin handling */
		.name = "intel-int3496",
		.id = PLATFORM_DEVID_NONE,
		.properties = asus_me176c_tf103c_int3496_props,
	},
};

static const struct software_node asus_me176c_tf103c_gpio_keys_node = {
	.name = "lid_sw",
};

static const struct property_entry asus_me176c_tf103c_lid_props[] = {
	PROPERTY_ENTRY_U32("linux,input-type", EV_SW),
	PROPERTY_ENTRY_U32("linux,code", SW_LID),
	PROPERTY_ENTRY_STRING("label", "lid_sw"),
	PROPERTY_ENTRY_GPIO("gpios", &baytrail_gpiochip_nodes[2], 12, GPIO_ACTIVE_LOW),
	PROPERTY_ENTRY_U32("debounce-interval", 50),
	PROPERTY_ENTRY_BOOL("wakeup-source"),
	{ }
};

static const struct software_node asus_me176c_tf103c_lid_node = {
	.parent = &asus_me176c_tf103c_gpio_keys_node,
	.properties = asus_me176c_tf103c_lid_props,
};

static const struct software_node *asus_me176c_tf103c_lid_swnodes[] = {
	&asus_me176c_tf103c_gpio_keys_node,
	&asus_me176c_tf103c_lid_node,
	NULL
};

/* Asus ME176C tablets have an Android factory image with everything hardcoded */
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

static const struct property_entry asus_me176c_touchscreen_props[] = {
	PROPERTY_ENTRY_GPIO("reset-gpios", &baytrail_gpiochip_nodes[0], 60, GPIO_ACTIVE_HIGH),
	PROPERTY_ENTRY_GPIO("irq-gpios", &baytrail_gpiochip_nodes[2], 28, GPIO_ACTIVE_HIGH),
	{ }
};

static const struct software_node asus_me176c_touchscreen_node = {
	.properties = asus_me176c_touchscreen_props,
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
		/* kxtj21009 accelerometer */
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
			.swnode = &asus_me176c_touchscreen_node,
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
		.ctrl.acpi.hid = "80860F0A",
		.ctrl.acpi.uid = "2",
		.ctrl_devname = "serial0",
		.serdev_hid = "BCM2E3A",
	},
};

const struct x86_dev_info asus_me176c_info __initconst = {
	.i2c_client_info = asus_me176c_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(asus_me176c_i2c_clients),
	.pdev_info = asus_me176c_tf103c_pdevs,
	.pdev_count = ARRAY_SIZE(asus_me176c_tf103c_pdevs),
	.serdev_info = asus_me176c_serdevs,
	.serdev_count = ARRAY_SIZE(asus_me176c_serdevs),
	.gpio_button_swnodes = asus_me176c_tf103c_lid_swnodes,
	.swnode_group = generic_lipo_hv_4v35_battery_swnodes,
	.modules = bq24190_modules,
	.gpiochip_type = X86_GPIOCHIP_BAYTRAIL,
};

/* Asus TF103C tablets have an Android factory image with everything hardcoded */
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

static const struct property_entry asus_tf103c_bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY_LEN("supplied-from", tusb1211_chg_det_psy, 1),
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_4v2_battery_node),
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
	PROPERTY_ENTRY_REF("monitored-battery", &generic_lipo_4v2_battery_node),
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
		/* kxtj21009 accelerometer */
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
			.con_id = "atmel_mxt_ts_irq",
		},
	},
};

const struct x86_dev_info asus_tf103c_info __initconst = {
	.i2c_client_info = asus_tf103c_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(asus_tf103c_i2c_clients),
	.pdev_info = asus_me176c_tf103c_pdevs,
	.pdev_count = ARRAY_SIZE(asus_me176c_tf103c_pdevs),
	.gpio_button_swnodes = asus_me176c_tf103c_lid_swnodes,
	.swnode_group = generic_lipo_4v2_battery_swnodes,
	.modules = bq24190_modules,
	.gpiochip_type = X86_GPIOCHIP_BAYTRAIL,
};
