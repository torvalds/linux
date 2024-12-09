// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for LTC3815
 *
 * Copyright (c) 2015 Linear Technology
 * Copyright (c) 2015 Guenter Roeck
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define LTC3815_MFR_IOUT_PEAK	0xd7
#define LTC3815_MFR_VOUT_PEAK	0xdd
#define LTC3815_MFR_VIN_PEAK	0xde
#define LTC3815_MFR_TEMP_PEAK	0xdf
#define LTC3815_MFR_IIN_PEAK	0xe1
#define LTC3815_MFR_SPECIAL_ID	0xe7

#define LTC3815_ID		0x8000
#define LTC3815_ID_MASK		0xff00

static int ltc3815_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * The chip returns 0x3e, suggesting VID mode with manufacturer
		 * specific VID codes. Since the output voltage is reported
		 * with a LSB of 0.5mV, override and report direct mode with
		 * appropriate coefficients.
		 */
		ret = 0x40;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int ltc3815_write_byte(struct i2c_client *client, int page, u8 reg)
{
	int ret;

	switch (reg) {
	case PMBUS_CLEAR_FAULTS:
		/*
		 * LTC3815 does not support the CLEAR_FAULTS command.
		 * Emulate it by clearing the status register.
		 */
		ret = pmbus_read_word_data(client, 0, 0xff, PMBUS_STATUS_WORD);
		if (ret > 0) {
			pmbus_write_word_data(client, 0, PMBUS_STATUS_WORD,
					      ret);
			ret = 0;
		}
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int ltc3815_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   LTC3815_MFR_VIN_PEAK);
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   LTC3815_MFR_VOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   LTC3815_MFR_TEMP_PEAK);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   LTC3815_MFR_IOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_IIN_MAX:
		ret = pmbus_read_word_data(client, page, phase,
					   LTC3815_MFR_IIN_PEAK);
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_IIN_HISTORY:
		ret = 0;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int ltc3815_write_word_data(struct i2c_client *client, int page,
				   int reg, u16 word)
{
	int ret;

	switch (reg) {
	case PMBUS_VIRT_RESET_IIN_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    LTC3815_MFR_IIN_PEAK, 0);
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    LTC3815_MFR_IOUT_PEAK, 0);
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    LTC3815_MFR_VOUT_PEAK, 0);
		break;
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    LTC3815_MFR_VIN_PEAK, 0);
		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = pmbus_write_word_data(client, page,
					    LTC3815_MFR_TEMP_PEAK, 0);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static const struct i2c_device_id ltc3815_id[] = {
	{"ltc3815"},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc3815_id);

static struct pmbus_driver_info ltc3815_info = {
	.pages = 1,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 250,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 0,
	.m[PSC_VOLTAGE_OUT] = 2,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 3,
	.m[PSC_CURRENT_IN] = 1,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = 2,
	.m[PSC_CURRENT_OUT] = 1,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 2,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_VOUT |
		PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP,
	.read_byte_data = ltc3815_read_byte_data,
	.read_word_data = ltc3815_read_word_data,
	.write_byte = ltc3815_write_byte,
	.write_word_data = ltc3815_write_word_data,
};

static int ltc3815_probe(struct i2c_client *client)
{
	int chip_id;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	chip_id = i2c_smbus_read_word_data(client, LTC3815_MFR_SPECIAL_ID);
	if (chip_id < 0)
		return chip_id;
	if ((chip_id & LTC3815_ID_MASK) != LTC3815_ID)
		return -ENODEV;

	return pmbus_do_probe(client, &ltc3815_info);
}

static struct i2c_driver ltc3815_driver = {
	.driver = {
		   .name = "ltc3815",
		   },
	.probe = ltc3815_probe,
	.id_table = ltc3815_id,
};

module_i2c_driver(ltc3815_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for LTC3815");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
