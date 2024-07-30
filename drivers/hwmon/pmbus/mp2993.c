// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP2993)
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define MP2993_VOUT_OVUV_UINT	125
#define MP2993_VOUT_OVUV_DIV	64
#define MP2993_VIN_LIMIT_UINT	1
#define MP2993_VIN_LIMIT_DIV	8
#define MP2993_READ_VIN_UINT	1
#define MP2993_READ_VIN_DIV	32

#define MP2993_PAGE_NUM	2

#define MP2993_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
							PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | \
							PMBUS_HAVE_TEMP | PMBUS_HAVE_PIN | \
							PMBUS_HAVE_IIN | \
							PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_TEMP | \
							PMBUS_HAVE_STATUS_INPUT)

#define MP2993_RAIL2_FUNC	(PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | \
							 PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP | \
							 PMBUS_HAVE_STATUS_VOUT | \
							 PMBUS_HAVE_STATUS_IOUT | \
							 PMBUS_HAVE_STATUS_TEMP | \
							 PMBUS_HAVE_STATUS_INPUT)

/* Converts a linear11 data exponent to a specified value */
static u16 mp2993_linear11_exponent_transfer(u16 word, u16 expect_exponent)
{
	s16 exponent, mantissa, target_exponent;

	exponent = ((s16)word) >> 11;
	mantissa = ((s16)((word & 0x7ff) << 5)) >> 5;
	target_exponent = (s16)((expect_exponent & 0x1f) << 11) >> 11;

	if (exponent > target_exponent)
		mantissa = mantissa << (exponent - target_exponent);
	else
		mantissa = mantissa >> (target_exponent - exponent);

	return (mantissa & 0x7ff) | ((expect_exponent << 11) & 0xf800);
}

static int
mp2993_set_vout_format(struct i2c_client *client, int page, int format)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client, PMBUS_VOUT_MODE, format);
}

static int mp2993_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	/* Set vout to direct format for rail1. */
	ret = mp2993_set_vout_format(client, 0, PB_VOUT_MODE_DIRECT);
	if (ret < 0)
		return ret;

	/* Set vout to direct format for rail2. */
	return mp2993_set_vout_format(client, 1, PB_VOUT_MODE_DIRECT);
}

static int mp2993_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(ret * MP2993_VOUT_OVUV_UINT, MP2993_VOUT_OVUV_DIV);
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The MP2993 ot fault limit value and ot warn limit value
		 * per rail are always the same, so only PMBUS_OT_FAULT_LIMIT
		 * and PMBUS_OT_WARN_LIMIT register in page 0 are defined to
		 * indicates the limit value.
		 */
		ret = pmbus_read_word_data(client, 0, phase, reg);
		break;
	case PMBUS_READ_VIN:
		/* The MP2993 vin scale is (1/32V)/Lsb */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(9, 0)) * MP2993_READ_VIN_UINT,
					MP2993_READ_VIN_DIV);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		/* The MP2993 vin limit scale is (1/8V)/Lsb */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(7, 0)) * MP2993_VIN_LIMIT_UINT,
					MP2993_VIN_LIMIT_DIV);
		break;
	case PMBUS_READ_IOUT:
	case PMBUS_READ_IIN:
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_READ_VOUT:
	case PMBUS_READ_PIN:
	case PMBUS_READ_POUT:
	case PMBUS_READ_TEMPERATURE_1:
		ret = -ENODATA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp2993_write_word_data(struct i2c_client *client, int page, int reg,
				  u16 word)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = DIV_ROUND_CLOSEST(word * MP2993_VOUT_OVUV_DIV, MP2993_VOUT_OVUV_UINT);
		ret = pmbus_write_word_data(client, 0, reg, ret);
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The MP2993 ot fault limit value and ot warn limit value
		 * per rail are always the same, so only PMBUS_OT_FAULT_LIMIT
		 * and PMBUS_OT_WARN_LIMIT register in page 0 are defined to
		 * config the ot limit value.
		 */
		ret = pmbus_write_word_data(client, 0, reg, word);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		/* The MP2993 vin limit scale is (1/8V)/Lsb */
		ret = pmbus_write_word_data(client, 0, reg,
					    DIV_ROUND_CLOSEST(word * MP2993_VIN_LIMIT_DIV,
							      MP2993_VIN_LIMIT_UINT));
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		/*
		 * The PMBUS_IIN_OC_WARN_LIMIT of MP2993 is linear11 format,
		 * and the exponent is a constant value(5'b00000)， so the
		 * exponent of word parameter should be converted to 5'b00000.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mp2993_linear11_exponent_transfer(word, 0x00));
		break;
		//
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		/*
		 * The PMBUS_IOUT_OC_FAULT_LIMIT and PMBUS_IOUT_OC_WARN_LIMIT
		 * of MP2993 can be regarded as linear11 format, and the
		 * exponent is a 5'b00001 or 5'b00000. To ensure a larger
		 * range of limit value, so the exponent of word parameter
		 * should be converted to 5'b00001.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mp2993_linear11_exponent_transfer(word, 0x01));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct pmbus_driver_info mp2993_info = {
	.pages = MP2993_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_POWER] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.m[PSC_VOLTAGE_IN] = 1,
	.R[PSC_VOLTAGE_IN] = 0,
	.b[PSC_VOLTAGE_IN] = 0,

	.m[PSC_TEMPERATURE] = 1,
	.R[PSC_TEMPERATURE] = 0,
	.b[PSC_TEMPERATURE] = 0,

	.func[0] = MP2993_RAIL1_FUNC,
	.func[1] = MP2993_RAIL2_FUNC,
	.read_word_data = mp2993_read_word_data,
	.write_word_data = mp2993_write_word_data,
	.identify = mp2993_identify,
};

static int mp2993_probe(struct i2c_client *client)
{
	return pmbus_do_probe(client, &mp2993_info);
}

static const struct i2c_device_id mp2993_id[] = {
	{"mp2993", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp2993_id);

static const struct of_device_id __maybe_unused mp2993_of_match[] = {
	{.compatible = "mps,mp2993"},
	{}
};
MODULE_DEVICE_TABLE(of, mp2993_of_match);

static struct i2c_driver mp2993_driver = {
	.driver = {
		.name = "mp2993",
		.of_match_table = mp2993_of_match,
	},
	.probe = mp2993_probe,
	.id_table = mp2993_id,
};

module_i2c_driver(mp2993_driver);

MODULE_AUTHOR("Noah Wang <noahwang.wang@outlook.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2993");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
