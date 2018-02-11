/*
 * Hardware monitoring driver for IR35221
 *
 * Copyright (C) IBM Corporation 2017.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define IR35221_MFR_VIN_PEAK		0xc5
#define IR35221_MFR_VOUT_PEAK		0xc6
#define IR35221_MFR_IOUT_PEAK		0xc7
#define IR35221_MFR_TEMP_PEAK		0xc8
#define IR35221_MFR_VIN_VALLEY		0xc9
#define IR35221_MFR_VOUT_VALLEY		0xca
#define IR35221_MFR_IOUT_VALLEY		0xcb
#define IR35221_MFR_TEMP_VALLEY		0xcc

static long ir35221_reg2data(int data, enum pmbus_sensor_classes class)
{
	s16 exponent;
	s32 mantissa;
	long val;

	/* We only modify LINEAR11 formats */
	exponent = ((s16)data) >> 11;
	mantissa = ((s16)((data & 0x7ff) << 5)) >> 5;

	val = mantissa * 1000L;

	/* scale result to micro-units for power sensors */
	if (class == PSC_POWER)
		val = val * 1000L;

	if (exponent >= 0)
		val <<= exponent;
	else
		val >>= -exponent;

	return val;
}

#define MAX_MANTISSA	(1023 * 1000)
#define MIN_MANTISSA	(511 * 1000)

static u16 ir35221_data2reg(long val, enum pmbus_sensor_classes class)
{
	s16 exponent = 0, mantissa;
	bool negative = false;

	if (val == 0)
		return 0;

	if (val < 0) {
		negative = true;
		val = -val;
	}

	/* Power is in uW. Convert to mW before converting. */
	if (class == PSC_POWER)
		val = DIV_ROUND_CLOSEST(val, 1000L);

	/* Reduce large mantissa until it fits into 10 bit */
	while (val >= MAX_MANTISSA && exponent < 15) {
		exponent++;
		val >>= 1;
	}
	/* Increase small mantissa to improve precision */
	while (val < MIN_MANTISSA && exponent > -15) {
		exponent--;
		val <<= 1;
	}

	/* Convert mantissa from milli-units to units */
	mantissa = DIV_ROUND_CLOSEST(val, 1000);

	/* Ensure that resulting number is within range */
	if (mantissa > 0x3ff)
		mantissa = 0x3ff;

	/* restore sign */
	if (negative)
		mantissa = -mantissa;

	/* Convert to 5 bit exponent, 11 bit mantissa */
	return (mantissa & 0x7ff) | ((exponent << 11) & 0xf800);
}

static u16 ir35221_scale_result(s16 data, int shift,
				enum pmbus_sensor_classes class)
{
	long val;

	val = ir35221_reg2data(data, class);

	if (shift < 0)
		val >>= -shift;
	else
		val <<= shift;

	return ir35221_data2reg(val, class);
}

static int ir35221_read_word_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, reg);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, 1, PSC_CURRENT_OUT);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, reg);
		ret = ir35221_scale_result(ret, -4, PSC_VOLTAGE_IN);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, reg);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, -1, PSC_CURRENT_IN);
		break;
	case PMBUS_READ_VIN:
		ret = pmbus_read_word_data(client, page, PMBUS_READ_VIN);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, -5, PSC_VOLTAGE_IN);
		break;
	case PMBUS_READ_IIN:
		ret = pmbus_read_word_data(client, page, PMBUS_READ_IIN);
		if (ret < 0)
			break;
		if (page == 0)
			ret = ir35221_scale_result(ret, -4, PSC_CURRENT_IN);
		else
			ret = ir35221_scale_result(ret, -5, PSC_CURRENT_IN);
		break;
	case PMBUS_READ_POUT:
		ret = pmbus_read_word_data(client, page, PMBUS_READ_POUT);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, -1, PSC_POWER);
		break;
	case PMBUS_READ_PIN:
		ret = pmbus_read_word_data(client, page, PMBUS_READ_PIN);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, -1, PSC_POWER);
		break;
	case PMBUS_READ_IOUT:
		ret = pmbus_read_word_data(client, page, PMBUS_READ_IOUT);
		if (ret < 0)
			break;
		if (page == 0)
			ret = ir35221_scale_result(ret, -1, PSC_CURRENT_OUT);
		else
			ret = ir35221_scale_result(ret, -2, PSC_CURRENT_OUT);
		break;
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, page, IR35221_MFR_VIN_PEAK);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, -5, PSC_VOLTAGE_IN);
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, page, IR35221_MFR_VOUT_PEAK);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, IR35221_MFR_IOUT_PEAK);
		if (ret < 0)
			break;
		if (page == 0)
			ret = ir35221_scale_result(ret, -1, PSC_CURRENT_IN);
		else
			ret = ir35221_scale_result(ret, -2, PSC_CURRENT_IN);
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page, IR35221_MFR_TEMP_PEAK);
		break;
	case PMBUS_VIRT_READ_VIN_MIN:
		ret = pmbus_read_word_data(client, page,
					   IR35221_MFR_VIN_VALLEY);
		if (ret < 0)
			break;
		ret = ir35221_scale_result(ret, -5, PSC_VOLTAGE_IN);
		break;
	case PMBUS_VIRT_READ_VOUT_MIN:
		ret = pmbus_read_word_data(client, page,
					   IR35221_MFR_VOUT_VALLEY);
		break;
	case PMBUS_VIRT_READ_IOUT_MIN:
		ret = pmbus_read_word_data(client, page,
					   IR35221_MFR_IOUT_VALLEY);
		if (ret < 0)
			break;
		if (page == 0)
			ret = ir35221_scale_result(ret, -1, PSC_CURRENT_IN);
		else
			ret = ir35221_scale_result(ret, -2, PSC_CURRENT_IN);
		break;
	case PMBUS_VIRT_READ_TEMP_MIN:
		ret = pmbus_read_word_data(client, page,
					   IR35221_MFR_TEMP_VALLEY);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int ir35221_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	int ret;
	u16 val;

	switch (reg) {
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		val = ir35221_scale_result(word, -1, PSC_CURRENT_OUT);
		ret = pmbus_write_word_data(client, page, reg, val);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
		val = ir35221_scale_result(word, 4, PSC_VOLTAGE_IN);
		ret = pmbus_write_word_data(client, page, reg, val);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		val = ir35221_scale_result(word, 1, PSC_CURRENT_IN);
		ret = pmbus_write_word_data(client, page, reg, val);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int ir35221_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct pmbus_driver_info *info;
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				| I2C_FUNC_SMBUS_READ_WORD_DATA
				| I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read PMBUS_MFR_ID\n");
		return ret;
	}
	if (ret != 2 || strncmp(buf, "RI", strlen("RI"))) {
		dev_err(&client->dev, "MFR_ID unrecognised\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, buf);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read PMBUS_MFR_MODEL\n");
		return ret;
	}
	if (ret != 2 || !(buf[0] == 0x6c && buf[1] == 0x00)) {
		dev_err(&client->dev, "MFR_MODEL unrecognised\n");
		return -ENODEV;
	}

	info = devm_kzalloc(&client->dev, sizeof(struct pmbus_driver_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->write_word_data = ir35221_write_word_data;
	info->read_word_data = ir35221_read_word_data;

	info->pages = 2;
	info->format[PSC_VOLTAGE_IN] = linear;
	info->format[PSC_VOLTAGE_OUT] = linear;
	info->format[PSC_CURRENT_IN] = linear;
	info->format[PSC_CURRENT_OUT] = linear;
	info->format[PSC_POWER] = linear;
	info->format[PSC_TEMPERATURE] = linear;

	info->func[0] = PMBUS_HAVE_VIN
		| PMBUS_HAVE_VOUT | PMBUS_HAVE_IIN
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_PIN
		| PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_STATUS_TEMP;
	info->func[1] = info->func[0];

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id ir35221_id[] = {
	{"ir35221", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ir35221_id);

static struct i2c_driver ir35221_driver = {
	.driver = {
		.name	= "ir35221",
	},
	.probe		= ir35221_probe,
	.remove		= pmbus_do_remove,
	.id_table	= ir35221_id,
};

module_i2c_driver(ir35221_driver);

MODULE_AUTHOR("Samuel Mendoza-Jonas <sam@mendozajonas.com");
MODULE_DESCRIPTION("PMBus driver for IR35221");
MODULE_LICENSE("GPL");
