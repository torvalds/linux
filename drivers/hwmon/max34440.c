/*
 * Hardware monitoring driver for Maxim MAX34440/MAX34441
 *
 * Copyright (c) 2011 Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

enum chips { max34440, max34441 };

#define MAX34440_STATUS_OC_WARN		(1 << 0)
#define MAX34440_STATUS_OC_FAULT	(1 << 1)
#define MAX34440_STATUS_OT_FAULT	(1 << 5)
#define MAX34440_STATUS_OT_WARN		(1 << 6)

static int max34440_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;
	int mfg_status;

	ret = pmbus_set_page(client, page);
	if (ret < 0)
		return ret;

	switch (reg) {
	case PMBUS_STATUS_IOUT:
		mfg_status = pmbus_read_word_data(client, 0,
						  PMBUS_STATUS_MFR_SPECIFIC);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX34440_STATUS_OC_WARN)
			ret |= PB_IOUT_OC_WARNING;
		if (mfg_status & MAX34440_STATUS_OC_FAULT)
			ret |= PB_IOUT_OC_FAULT;
		break;
	case PMBUS_STATUS_TEMPERATURE:
		mfg_status = pmbus_read_word_data(client, 0,
						  PMBUS_STATUS_MFR_SPECIFIC);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX34440_STATUS_OT_WARN)
			ret |= PB_TEMP_OT_WARNING;
		if (mfg_status & MAX34440_STATUS_OT_FAULT)
			ret |= PB_TEMP_OT_FAULT;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static struct pmbus_driver_info max34440_info[] = {
	[max34440] = {
		.pages = 14,
		.direct[PSC_VOLTAGE_IN] = true,
		.direct[PSC_VOLTAGE_OUT] = true,
		.direct[PSC_TEMPERATURE] = true,
		.direct[PSC_CURRENT_OUT] = true,
		.m[PSC_VOLTAGE_IN] = 1,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = 3,	    /* R = 0 in datasheet reflects mV */
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,    /* R = 0 in datasheet reflects mV */
		.m[PSC_CURRENT_OUT] = 1,
		.b[PSC_CURRENT_OUT] = 0,
		.R[PSC_CURRENT_OUT] = 3,    /* R = 0 in datasheet reflects mA */
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[4] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[5] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[6] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[7] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[8] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[9] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[10] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[11] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[12] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[13] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_byte_data = max34440_read_byte_data,
	},
	[max34441] = {
		.pages = 12,
		.direct[PSC_VOLTAGE_IN] = true,
		.direct[PSC_VOLTAGE_OUT] = true,
		.direct[PSC_TEMPERATURE] = true,
		.direct[PSC_CURRENT_OUT] = true,
		.direct[PSC_FAN] = true,
		.m[PSC_VOLTAGE_IN] = 1,
		.b[PSC_VOLTAGE_IN] = 0,
		.R[PSC_VOLTAGE_IN] = 3,
		.m[PSC_VOLTAGE_OUT] = 1,
		.b[PSC_VOLTAGE_OUT] = 0,
		.R[PSC_VOLTAGE_OUT] = 3,
		.m[PSC_CURRENT_OUT] = 1,
		.b[PSC_CURRENT_OUT] = 0,
		.R[PSC_CURRENT_OUT] = 3,
		.m[PSC_TEMPERATURE] = 1,
		.b[PSC_TEMPERATURE] = 0,
		.R[PSC_TEMPERATURE] = 2,
		.m[PSC_FAN] = 1,
		.b[PSC_FAN] = 0,
		.R[PSC_FAN] = 0,
		.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[4] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT,
		.func[5] = PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12,
		.func[6] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[7] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[8] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[9] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[10] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.func[11] = PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP,
		.read_byte_data = max34440_read_byte_data,
	},
};

static int max34440_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, id, &max34440_info[id->driver_data]);
}

static int max34440_remove(struct i2c_client *client)
{
	return pmbus_do_remove(client);
}

static const struct i2c_device_id max34440_id[] = {
	{"max34440", max34440},
	{"max34441", max34441},
	{}
};

MODULE_DEVICE_TABLE(i2c, max34440_id);

/* This is the driver that will be inserted */
static struct i2c_driver max34440_driver = {
	.driver = {
		   .name = "max34440",
		   },
	.probe = max34440_probe,
	.remove = max34440_remove,
	.id_table = max34440_id,
};

static int __init max34440_init(void)
{
	return i2c_add_driver(&max34440_driver);
}

static void __exit max34440_exit(void)
{
	i2c_del_driver(&max34440_driver);
}

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX34440/MAX34441");
MODULE_LICENSE("GPL");
module_init(max34440_init);
module_exit(max34440_exit);
