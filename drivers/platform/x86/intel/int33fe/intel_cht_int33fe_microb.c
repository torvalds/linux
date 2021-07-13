// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Cherry Trail ACPI INT33FE pseudo device driver for devices with
 * USB Micro-B connector (e.g. without of FUSB302 USB Type-C controller)
 *
 * Copyright (C) 2019 Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * At least one Intel Cherry Trail based device which ship with Windows 10
 * (Lenovo YogaBook YB1-X91L/F tablet), have this weird INT33FE ACPI device
 * with a CRS table with 2 I2cSerialBusV2 resources, for 2 different chips
 * attached to various i2c busses:
 * 1. The Whiskey Cove PMIC, which is also described by the INT34D3 ACPI device
 * 2. TI BQ27542 Fuel Gauge Controller
 *
 * So this driver is a stub / pseudo driver whose only purpose is to
 * instantiate i2c-client for battery fuel gauge, so that standard i2c driver
 * for these chip can bind to the it.
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/pd.h>

#include "intel_cht_int33fe_common.h"

static const char * const bq27xxx_suppliers[] = { "bq25890-charger" };

static const struct property_entry bq27xxx_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq27xxx_suppliers),
	{ }
};

static const struct software_node bq27xxx_node = {
	.properties = bq27xxx_props,
};

int cht_int33fe_microb_probe(struct cht_int33fe_data *data)
{
	struct device *dev = data->dev;
	struct i2c_board_info board_info;

	memset(&board_info, 0, sizeof(board_info));
	strscpy(board_info.type, "bq27542", ARRAY_SIZE(board_info.type));
	board_info.dev_name = "bq27542";
	board_info.swnode = &bq27xxx_node;
	data->battery_fg = i2c_acpi_new_device(dev, 1, &board_info);

	return PTR_ERR_OR_ZERO(data->battery_fg);
}

int cht_int33fe_microb_remove(struct cht_int33fe_data *data)
{
	i2c_unregister_device(data->battery_fg);

	return 0;
}
