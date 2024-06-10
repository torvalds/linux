// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for TEXAS TPS546D24 buck converter
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include "pmbus.h"

static struct pmbus_driver_info tps546d24_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN
			| PMBUS_HAVE_IOUT | PMBUS_HAVE_VOUT
			| PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_VOUT
			| PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
};

static int tps546d24_probe(struct i2c_client *client)
{
	int reg;

	reg = i2c_smbus_read_byte_data(client, PMBUS_VOUT_MODE);
	if (reg < 0)
		return reg;

	if (reg & 0x80) {
		int err;

		err = i2c_smbus_write_byte_data(client, PMBUS_VOUT_MODE, reg & 0x7f);
		if (err < 0)
			return err;
	}
	return pmbus_do_probe(client, &tps546d24_info);
}

static const struct i2c_device_id tps546d24_id[] = {
	{"tps546d24"},
	{}
};
MODULE_DEVICE_TABLE(i2c, tps546d24_id);

static const struct of_device_id __maybe_unused tps546d24_of_match[] = {
	{.compatible = "ti,tps546d24"},
	{}
};
MODULE_DEVICE_TABLE(of, tps546d24_of_match);

/* This is the driver that will be inserted */
static struct i2c_driver tps546d24_driver = {
	.driver = {
		   .name = "tps546d24",
		   .of_match_table = of_match_ptr(tps546d24_of_match),
	   },
	.probe = tps546d24_probe,
	.id_table = tps546d24_id,
};

module_i2c_driver(tps546d24_driver);

MODULE_AUTHOR("Duke Du <dukedu83@gmail.com>");
MODULE_DESCRIPTION("PMBus driver for TI tps546d24");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
