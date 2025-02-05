// SPDX-License-Identifier: GPL-2.0+
/*
 * Hardware monitoring driver for STMicroelectronics digital controller PM6764TR
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include "pmbus.h"

#define PM6764TR_PMBUS_READ_VOUT	0xD4

static int pm6764tr_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VMON:
		ret = pmbus_read_word_data(client, page, phase, PM6764TR_PMBUS_READ_VOUT);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static struct pmbus_driver_info pm6764tr_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = vid,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN |  PMBUS_HAVE_PIN |
	    PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | PMBUS_HAVE_VMON |
		PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
	.read_word_data = pm6764tr_read_word_data,
};

static int pm6764tr_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &pm6764tr_info);
}

static const struct i2c_device_id pm6764tr_id[] = {
	{"pm6764tr"},
	{}
};
MODULE_DEVICE_TABLE(i2c, pm6764tr_id);

static const struct of_device_id __maybe_unused pm6764tr_of_match[] = {
	{.compatible = "st,pm6764tr"},
	{}
};

/* This is the driver that will be inserted */
static struct i2c_driver pm6764tr_driver = {
	.driver = {
		   .name = "pm6764tr",
		   .of_match_table = of_match_ptr(pm6764tr_of_match),
		   },
	.probe = pm6764tr_probe,
	.id_table = pm6764tr_id,
};

module_i2c_driver(pm6764tr_driver);

MODULE_AUTHOR("Charles Hsu");
MODULE_DESCRIPTION("PMBus driver for  ST PM6764TR");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
