// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Synchronous Step-Down Converter(MPQ82D00)
 */

#include <linux/bitfield.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "pmbus.h"

#define MPQ82D00_VOUT_DIV	64

#define MPQ82D00_PAGE_NUM	1

#define MPQ82D00_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
							PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP | \
							PMBUS_HAVE_POUT | PMBUS_HAVE_PIN | \
							PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_TEMP | \
							PMBUS_HAVE_STATUS_INPUT)

struct mpq82d00_data {
	struct pmbus_driver_info info;
	int vout_scale;
};

#define to_mpq82d00_data(x) container_of(x, struct mpq82d00_data, info)

static u16 mpq82d00_linear_exp_transfer(u16 word, u16 expect_exponent)
{
	s16 exponent, mantissa, target_exponent;

	exponent = ((s16)word) >> 11;
	mantissa = ((s16)((word & 0x7ff) << 5)) >> 5;
	target_exponent = (s16)((expect_exponent & 0x1f) << 11) >> 11;

	/*
	 * The MPQ82D00 does not support negtive limit value, if a negtive
	 * limit value is written, the limit value will become to 0. And
	 * the maximum positive limit value is limitted to 0x3FF.
	 */
	if (mantissa < 0) {
		mantissa = 0;
	} else {
		if (exponent > target_exponent) {
			mantissa = (1023 >> (exponent - target_exponent)) >= mantissa ?
						mantissa << (exponent - target_exponent) :
						0x3FF;
		} else {
			mantissa = clamp_val(mantissa >> (target_exponent - exponent),
					     0, 0x3FF);
		}
	}

	return mantissa | ((expect_exponent << 11) & 0xf800);
}

static int mpq82d00_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * The MPQ82D00 does not follow standard PMBus protocol completely,
		 * and the calculation of vout in this driver is based on direct
		 * format. As a result, the format of vout is enforced to direct.
		 */
		ret = PB_VOUT_MODE_DIRECT;
		break;
	default:
		/*
		 * These registers are not explicitly handled by the driver,
		 * as a result, return -ENODATA directly.
		 */
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int mpq82d00_read_word_data(struct i2c_client *client, int page,
				   int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mpq82d00_data *data = to_mpq82d00_data(info);
	int ret;

	switch (reg) {
	case PMBUS_STATUS_WORD:
	case PMBUS_READ_VIN:
	case PMBUS_READ_IOUT:
	case PMBUS_READ_POUT:
	case PMBUS_READ_PIN:
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
		/*
		 * These registers are not explicitly handled by the driver,
		 * as a result, return -ENODATA directly.
		 */
		ret = -ENODATA;
		break;
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(11, 0)) * data->vout_scale,
					MPQ82D00_VOUT_DIV);
		break;
	default:
		/*
		 * The MPQ82D00 do not support other telemetry and limit
		 * value reading, so, return -EINVAL directly.
		 */
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mpq82d00_write_word_data(struct i2c_client *client, int page, int reg,
				    u16 word)
{
	int ret;

	switch (reg) {
	case PMBUS_OT_FAULT_LIMIT:
		/*
		 * The PMBUS_OT_FAULT_LIMIT of MPQ82D00 is linear11 format,
		 * and the exponent is a constant value(5'b00000), so the
		 * exponent of word parameter should be converted to 5'b00000.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mpq82d00_linear_exp_transfer(word, 0x00));
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		/*
		 * The PMBUS_IOUT_OC_FAULT_LIMIT of MPQ82D00 is linear11 format,
		 * and the exponent is a constant value(5'b11100), so the
		 * exponent of word parameter should be converted to 5'b11100.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mpq82d00_linear_exp_transfer(word, 0x1C));
		break;
	default:
		/*
		 * The MPQ82D00 do not support other limit value configuration,
		 * so, return -EINVAL directly.
		 */
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mpq82d00_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	struct mpq82d00_data *data = to_mpq82d00_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, PMBUS_VOUT_MODE);
	if (ret < 0)
		return ret;

	/*
	 * The MPQ82D00 supports three vout mode. If PMBUS_VOUT_MODE
	 * bit5 is 1, the vout scale is 5mv/LSB.If PMBUS_VOUT_MODE bit5
	 * is 0, it is linear mode, the vout scale is 1.953125mv/LSB.
	 */
	if (FIELD_GET(GENMASK(5, 5), ret))
		data->vout_scale = 320;
	else
		data->vout_scale = 125;

	return 0;
}

static struct pmbus_driver_info mpq82d00_info = {
	.pages = MPQ82D00_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_POWER] = linear,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.func[0] = MPQ82D00_RAIL1_FUNC,
	.read_word_data = mpq82d00_read_word_data,
	.read_byte_data = mpq82d00_read_byte_data,
	.write_word_data = mpq82d00_write_word_data,
	.identify = mpq82d00_identify,
};

static int mpq82d00_probe(struct i2c_client *client)
{
	struct mpq82d00_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct mpq82d00_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mpq82d00_info, sizeof(mpq82d00_info));

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id mpq82d00_id[] = {
	{.name = "mpq82d00"},
	{}
};
MODULE_DEVICE_TABLE(i2c, mpq82d00_id);

static const struct of_device_id __maybe_unused mpq82d00_of_match[] = {
	{.compatible = "mps,mpq82d00"},
	{}
};
MODULE_DEVICE_TABLE(of, mpq82d00_of_match);

static struct i2c_driver mpq82d00_driver = {
	.driver = {
		.name = "mpq82d00",
		.of_match_table = mpq82d00_of_match,
	},
	.probe = mpq82d00_probe,
	.id_table = mpq82d00_id,
};

module_i2c_driver(mpq82d00_driver);

MODULE_AUTHOR("Wensheng Wang <wenswang@yeah.net");
MODULE_DESCRIPTION("PMBus driver for MPS MPQ82D00 device");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
