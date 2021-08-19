// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for the STPDDC60 controller
 *
 * Copyright (c) 2021 Flextronics International Sweden AB.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/pmbus.h>
#include "pmbus.h"

#define STPDDC60_MFR_READ_VOUT		0xd2
#define STPDDC60_MFR_OV_LIMIT_OFFSET	0xe5
#define STPDDC60_MFR_UV_LIMIT_OFFSET	0xe6

static const struct i2c_device_id stpddc60_id[] = {
	{"stpddc60", 0},
	{"bmr481", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, stpddc60_id);

static struct pmbus_driver_info stpddc60_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		| PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
		| PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		| PMBUS_HAVE_POUT,
};

/*
 * Calculate the closest absolute offset between commanded vout value
 * and limit value in steps of 50mv in the range 0 (50mv) to 7 (400mv).
 * Return 0 if the upper limit is lower than vout or if the lower limit
 * is higher than vout.
 */
static u8 stpddc60_get_offset(int vout, u16 limit, bool over)
{
	int offset;
	long v, l;

	v = 250 + (vout - 1) * 5; /* Convert VID to mv */
	l = (limit * 1000L) >> 8; /* Convert LINEAR to mv */

	if (over == (l < v))
		return 0;

	offset = DIV_ROUND_CLOSEST(abs(l - v), 50);

	if (offset > 0)
		offset--;

	return clamp_val(offset, 0, 7);
}

/*
 * Adjust the linear format word to use the given fixed exponent.
 */
static u16 stpddc60_adjust_linear(u16 word, s16 fixed)
{
	s16 e, m, d;

	e = ((s16)word) >> 11;
	m = ((s16)((word & 0x7ff) << 5)) >> 5;
	d = e - fixed;

	if (d >= 0)
		m <<= d;
	else
		m >>= -d;

	return clamp_val(m, 0, 0x3ff) | ((fixed << 11) & 0xf800);
}

/*
 * The VOUT_COMMAND register uses the VID format but the vout alarm limit
 * registers use the LINEAR format so we override VOUT_MODE here to force
 * LINEAR format for all registers.
 */
static int stpddc60_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		ret = 0x18;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

/*
 * The vout related registers return values in LINEAR11 format when LINEAR16
 * is expected. Clear the top 5 bits to set the exponent part to zero to
 * convert the value to LINEAR16 format.
 */
static int stpddc60_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	int ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase,
					   STPDDC60_MFR_READ_VOUT);
		if (ret < 0)
			return ret;
		ret &= 0x7ff;
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;
		ret &= 0x7ff;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

/*
 * The vout under- and over-voltage limits are set as an offset relative to
 * the commanded vout voltage. The vin, iout, pout and temp limits must use
 * the same fixed exponent the chip uses to encode the data when read.
 */
static int stpddc60_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	int ret;
	u8 offset;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, 0xff,
					   PMBUS_VOUT_COMMAND);
		if (ret < 0)
			return ret;
		offset = stpddc60_get_offset(ret, word, true);
		ret = pmbus_write_byte_data(client, page,
					    STPDDC60_MFR_OV_LIMIT_OFFSET,
					    offset);
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, 0xff,
					   PMBUS_VOUT_COMMAND);
		if (ret < 0)
			return ret;
		offset = stpddc60_get_offset(ret, word, false);
		ret = pmbus_write_byte_data(client, page,
					    STPDDC60_MFR_UV_LIMIT_OFFSET,
					    offset);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_POUT_OP_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;
		word = stpddc60_adjust_linear(word, ret >> 11);
		ret = pmbus_write_word_data(client, page, reg, word);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int stpddc60_probe(struct i2c_client *client)
{
	int status;
	u8 device_id[I2C_SMBUS_BLOCK_MAX + 1];
	const struct i2c_device_id *mid;
	struct pmbus_driver_info *info = &stpddc60_info;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	status = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, device_id);
	if (status < 0) {
		dev_err(&client->dev, "Failed to read Manufacturer Model\n");
		return status;
	}
	for (mid = stpddc60_id; mid->name[0]; mid++) {
		if (!strncasecmp(mid->name, device_id, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	info->read_byte_data = stpddc60_read_byte_data;
	info->read_word_data = stpddc60_read_word_data;
	info->write_word_data = stpddc60_write_word_data;

	status = pmbus_do_probe(client, info);
	if (status < 0)
		return status;

	pmbus_set_update(client, PMBUS_VOUT_OV_FAULT_LIMIT, true);
	pmbus_set_update(client, PMBUS_VOUT_UV_FAULT_LIMIT, true);

	return 0;
}

static struct i2c_driver stpddc60_driver = {
	.driver = {
		   .name = "stpddc60",
		   },
	.probe_new = stpddc60_probe,
	.id_table = stpddc60_id,
};

module_i2c_driver(stpddc60_driver);

MODULE_AUTHOR("Erik Rosen <erik.rosen@metormote.com>");
MODULE_DESCRIPTION("PMBus driver for ST STPDDC60");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
