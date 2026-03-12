// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for STMicroelectronics digital controller stef48h28
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "pmbus.h"

static struct pmbus_driver_info stef48h28_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 50,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 0,
	.m[PSC_VOLTAGE_OUT] = 50,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 0,
	.m[PSC_CURRENT_IN] = 100,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = 0,
	.m[PSC_CURRENT_OUT] = 100,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 0,
	.m[PSC_POWER] = 9765,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -3,
	.m[PSC_TEMPERATURE] = 25,
	.b[PSC_TEMPERATURE] = 500,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN
		| PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2
		| PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT
};

static int stef48h28_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &stef48h28_info);
}

static const struct i2c_device_id stef48h28_id[] = {
	{"stef48h28"},
	{}
};
MODULE_DEVICE_TABLE(i2c, stef48h28_id);

static const struct of_device_id __maybe_unused stef48h28_of_match[] = {
	{.compatible = "st,stef48h28"},
	{}
};

static struct i2c_driver stef48h28_driver = {
	.driver = {
			.name = "stef48h28",
			.of_match_table = of_match_ptr(stef48h28_of_match),
			},
	.probe = stef48h28_probe,
	.id_table = stef48h28_id,
};

module_i2c_driver(stef48h28_driver);

MODULE_AUTHOR("Charles Hsu <hsu.yungteng@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for ST stef48h28");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
