// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for E50SN12051
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include "pmbus.h"

static struct pmbus_driver_info e50sn12051_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT |
		   PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		   PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		   PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
};

static const struct i2c_device_id e50sn12051_id[] = { { "e50sn12051", 0 }, {} };
MODULE_DEVICE_TABLE(i2c, e50sn12051_id);

static const struct of_device_id e50sn12051_of_match[] = {
	{ .compatible = "delta,e50sn12051" },
	{},
};
MODULE_DEVICE_TABLE(of, e50sn12051_of_match);

static int e50sn12051_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &e50sn12051_info);
}

static struct i2c_driver e50sn12051_driver = {
	.driver = {
		.name = "e50sn12051",
		.of_match_table = e50sn12051_of_match,
	},
	.probe = e50sn12051_probe,

	.id_table = e50sn12051_id,
};

module_i2c_driver(e50sn12051_driver);

MODULE_AUTHOR("Kevin Chang <kevin.chang2@amd.com>");
MODULE_DESCRIPTION("PMBus driver for E50SN12051");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
