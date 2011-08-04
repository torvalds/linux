/*
 * Hardware monitoring driver for Maxim MAX8688
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

#define MAX8688_MFR_VOUT_PEAK		0xd4
#define MAX8688_MFR_IOUT_PEAK		0xd5
#define MAX8688_MFR_TEMPERATURE_PEAK	0xd6
#define MAX8688_MFG_STATUS		0xd8

#define MAX8688_STATUS_OC_FAULT		(1 << 4)
#define MAX8688_STATUS_OV_FAULT		(1 << 5)
#define MAX8688_STATUS_OV_WARNING	(1 << 8)
#define MAX8688_STATUS_UV_FAULT		(1 << 9)
#define MAX8688_STATUS_UV_WARNING	(1 << 10)
#define MAX8688_STATUS_UC_FAULT		(1 << 11)
#define MAX8688_STATUS_OC_WARNING	(1 << 12)
#define MAX8688_STATUS_OT_FAULT		(1 << 13)
#define MAX8688_STATUS_OT_WARNING	(1 << 14)

static int max8688_read_word_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	if (page)
		return -EINVAL;

	switch (reg) {
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, 0, MAX8688_MFR_VOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, 0, MAX8688_MFR_IOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, 0,
					   MAX8688_MFR_TEMPERATURE_PEAK);
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = 0;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int max8688_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		ret = pmbus_write_word_data(client, 0, MAX8688_MFR_VOUT_PEAK,
					    0);
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
		ret = pmbus_write_word_data(client, 0, MAX8688_MFR_IOUT_PEAK,
					    0);
		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = pmbus_write_word_data(client, 0,
					    MAX8688_MFR_TEMPERATURE_PEAK,
					    0xffff);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int max8688_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret = 0;
	int mfg_status;

	if (page)
		return -EINVAL;

	switch (reg) {
	case PMBUS_STATUS_VOUT:
		mfg_status = pmbus_read_word_data(client, 0,
						  MAX8688_MFG_STATUS);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX8688_STATUS_UV_WARNING)
			ret |= PB_VOLTAGE_UV_WARNING;
		if (mfg_status & MAX8688_STATUS_UV_FAULT)
			ret |= PB_VOLTAGE_UV_FAULT;
		if (mfg_status & MAX8688_STATUS_OV_WARNING)
			ret |= PB_VOLTAGE_OV_WARNING;
		if (mfg_status & MAX8688_STATUS_OV_FAULT)
			ret |= PB_VOLTAGE_OV_FAULT;
		break;
	case PMBUS_STATUS_IOUT:
		mfg_status = pmbus_read_word_data(client, 0,
						  MAX8688_MFG_STATUS);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX8688_STATUS_UC_FAULT)
			ret |= PB_IOUT_UC_FAULT;
		if (mfg_status & MAX8688_STATUS_OC_WARNING)
			ret |= PB_IOUT_OC_WARNING;
		if (mfg_status & MAX8688_STATUS_OC_FAULT)
			ret |= PB_IOUT_OC_FAULT;
		break;
	case PMBUS_STATUS_TEMPERATURE:
		mfg_status = pmbus_read_word_data(client, 0,
						  MAX8688_MFG_STATUS);
		if (mfg_status < 0)
			return mfg_status;
		if (mfg_status & MAX8688_STATUS_OT_WARNING)
			ret |= PB_TEMP_OT_WARNING;
		if (mfg_status & MAX8688_STATUS_OT_FAULT)
			ret |= PB_TEMP_OT_FAULT;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static struct pmbus_driver_info max8688_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.m[PSC_VOLTAGE_IN] = 19995,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = -1,
	.m[PSC_VOLTAGE_OUT] = 19995,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = -1,
	.m[PSC_CURRENT_OUT] = 23109,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = -2,
	.m[PSC_TEMPERATURE] = -7612,
	.b[PSC_TEMPERATURE] = 335,
	.R[PSC_TEMPERATURE] = -3,
	.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_STATUS_TEMP,
	.read_byte_data = max8688_read_byte_data,
	.read_word_data = max8688_read_word_data,
	.write_word_data = max8688_write_word_data,
};

static int max8688_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, id, &max8688_info);
}

static int max8688_remove(struct i2c_client *client)
{
	return pmbus_do_remove(client);
}

static const struct i2c_device_id max8688_id[] = {
	{"max8688", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, max8688_id);

/* This is the driver that will be inserted */
static struct i2c_driver max8688_driver = {
	.driver = {
		   .name = "max8688",
		   },
	.probe = max8688_probe,
	.remove = max8688_remove,
	.id_table = max8688_id,
};

static int __init max8688_init(void)
{
	return i2c_add_driver(&max8688_driver);
}

static void __exit max8688_exit(void)
{
	i2c_del_driver(&max8688_driver);
}

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX8688");
MODULE_LICENSE("GPL");
module_init(max8688_init);
module_exit(max8688_exit);
