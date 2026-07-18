// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Analog Devices MAX20860A
 *
 * SPDX-FileCopyrightText: Copyright Hewlett Packard Enterprise Development LP
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

#if IS_ENABLED(CONFIG_SENSORS_MAX20860A_REGULATOR)
static const struct regulator_desc max20860a_reg_desc[] = {
	PMBUS_REGULATOR_ONE("vout"),
};
#endif

static struct pmbus_driver_info max20860a_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT |
		PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
		PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_STATUS_INPUT,
#if IS_ENABLED(CONFIG_SENSORS_MAX20860A_REGULATOR)
	.num_regulators = 1,
	.reg_desc = max20860a_reg_desc,
#endif
};

static int max20860a_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &max20860a_info);
}

static const struct i2c_device_id max20860a_id[] = {
	{"max20860a"},
	{}
};
MODULE_DEVICE_TABLE(i2c, max20860a_id);

static const struct of_device_id max20860a_of_match[] = {
	{ .compatible = "adi,max20860a" },
	{}
};
MODULE_DEVICE_TABLE(of, max20860a_of_match);

static struct i2c_driver max20860a_driver = {
	.driver = {
		.name = "max20860a",
		.of_match_table = max20860a_of_match,
	},
	.probe = max20860a_probe,
	.id_table = max20860a_id,
};

module_i2c_driver(max20860a_driver);

MODULE_AUTHOR("Syed Arif <arif.syed@hpe.com>");
MODULE_AUTHOR("Sanman Pradhan <psanman@juniper.net>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices MAX20860A");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
