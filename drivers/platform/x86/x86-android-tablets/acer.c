// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Board info for Acer X86 tablets which ship with Android as the factory image
 * and which have broken DSDT tables. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2025 Hans de Goede <hansg@kernel.org>
 */

#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#include "shared-psy-info.h"
#include "x86-android-tablets.h"

/* Acer Iconia One 7 B1-750 has an Android factory image with everything hardcoded */
static const char * const acer_b1_750_mount_matrix[] = {
	"-1", "0", "0",
	"0", "1", "0",
	"0", "0", "1"
};

static const struct property_entry acer_b1_750_bma250e_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("mount-matrix", acer_b1_750_mount_matrix),
	{ }
};

static const struct software_node acer_b1_750_bma250e_node = {
	.properties = acer_b1_750_bma250e_props,
};

static const struct property_entry acer_b1_750_novatek_props[] = {
	PROPERTY_ENTRY_GPIO("reset-gpios", &baytrail_gpiochip_nodes[1], 26, GPIO_ACTIVE_LOW),
	{ }
};

static const struct software_node acer_b1_750_novatek_node = {
	.properties = acer_b1_750_novatek_props,
};

static const struct x86_i2c_client_info acer_b1_750_i2c_clients[] __initconst = {
	{
		/* Novatek NVT-ts touchscreen */
		.board_info = {
			.type = "nt11205-ts",
			.addr = 0x34,
			.dev_name = "NVT-ts",
			.swnode = &acer_b1_750_novatek_node,
		},
		.adapter_path = "\\_SB_.I2C4",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 3,
			.trigger = ACPI_EDGE_SENSITIVE,
			.polarity = ACPI_ACTIVE_LOW,
			.con_id = "NVT-ts_irq",
		},
	}, {
		/* BMA250E accelerometer */
		.board_info = {
			.type = "bma250e",
			.addr = 0x18,
			.swnode = &acer_b1_750_bma250e_node,
		},
		.adapter_path = "\\_SB_.I2C3",
		.irq_data = {
			.type = X86_ACPI_IRQ_TYPE_GPIOINT,
			.chip = "INT33FC:02",
			.index = 25,
			.trigger = ACPI_LEVEL_SENSITIVE,
			.polarity = ACPI_ACTIVE_HIGH,
			.con_id = "bma250e_irq",
		},
	},
};

const struct x86_dev_info acer_b1_750_info __initconst = {
	.i2c_client_info = acer_b1_750_i2c_clients,
	.i2c_client_count = ARRAY_SIZE(acer_b1_750_i2c_clients),
	.pdev_info = int3496_pdevs,
	.pdev_count = 1,
	.gpiochip_type = X86_GPIOCHIP_BAYTRAIL,
};
