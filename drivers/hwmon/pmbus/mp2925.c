// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP2925)
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

/*
 * Vender specific register MFR_VR_MULTI_CONFIG(0x08).
 * This register is used to obtain vid scale.
 */
#define MFR_VR_MULTI_CONFIG	0x08

#define MP2925_VOUT_DIV	512
#define MP2925_VOUT_OVUV_UINT	195
#define MP2925_VOUT_OVUV_DIV	100

#define MP2925_PAGE_NUM	2

#define MP2925_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_PIN | \
							 PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | \
							 PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP | \
							 PMBUS_HAVE_STATUS_VOUT | \
							 PMBUS_HAVE_STATUS_IOUT | \
							 PMBUS_HAVE_STATUS_TEMP | \
							 PMBUS_HAVE_STATUS_INPUT)

#define MP2925_RAIL2_FUNC	(PMBUS_HAVE_PIN | PMBUS_HAVE_VOUT | \
							 PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | \
							 PMBUS_HAVE_TEMP | PMBUS_HAVE_IIN | \
							 PMBUS_HAVE_STATUS_VOUT | \
							 PMBUS_HAVE_STATUS_IOUT | \
							 PMBUS_HAVE_STATUS_TEMP | \
							 PMBUS_HAVE_STATUS_INPUT)

struct mp2925_data {
	struct pmbus_driver_info info;
	int vout_scale[MP2925_PAGE_NUM];
};

#define to_mp2925_data(x) container_of(x, struct mp2925_data, info)

static u16 mp2925_linear_exp_transfer(u16 word, u16 expect_exponent)
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

static int mp2925_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * The MP2925 does not follow standard PMBus protocol completely,
		 * and the calculation of vout in this driver is based on direct
		 * format. As a result, the format of vout is enforced to direct.
		 */
		ret = PB_VOUT_MODE_DIRECT;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int mp2925_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2925_data *data = to_mp2925_data(info);
	int ret;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(11, 0)) * data->vout_scale[page],
					MP2925_VOUT_DIV);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(11, 0)) * MP2925_VOUT_OVUV_UINT,
					MP2925_VOUT_OVUV_DIV);
		break;
	case PMBUS_STATUS_WORD:
	case PMBUS_READ_VIN:
	case PMBUS_READ_IOUT:
	case PMBUS_READ_POUT:
	case PMBUS_READ_PIN:
	case PMBUS_READ_IIN:
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		ret = -ENODATA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp2925_write_word_data(struct i2c_client *client, int page, int reg,
				  u16 word)
{
	int ret;

	switch (reg) {
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		/*
		 * The PMBUS_VIN_OV_FAULT_LIMIT, PMBUS_VIN_OV_WARN_LIMIT,
		 * PMBUS_VIN_UV_WARN_LIMIT and PMBUS_VIN_UV_FAULT_LIMIT
		 * of MP2925 is linear11 format, and the exponent is a
		 * constant value(5'b11100)， so the exponent of word
		 * parameter should be converted to 5'b11100(0x1C).
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mp2925_linear_exp_transfer(word, 0x1C));
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		/*
		 * The bit0-bit11 is the limit value, and bit12-bit15
		 * should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(11, 0)) |
				FIELD_PREP(GENMASK(11, 0),
					   DIV_ROUND_CLOSEST(word * MP2925_VOUT_OVUV_DIV,
							     MP2925_VOUT_OVUV_UINT)));
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT of
		 * MP2925 is linear11 format, and the exponent is a
		 * constant value(5'b00000), so the exponent of word
		 * parameter should be converted to 5'b00000.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mp2925_linear_exp_transfer(word, 0x00));
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		/*
		 * The PMBUS_IOUT_OC_FAULT_LIMIT and PMBUS_IOUT_OC_WARN_LIMIT
		 * of MP2925 is linear11 format, and the exponent can not be
		 * changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    mp2925_linear_exp_transfer(word,
								       FIELD_GET(GENMASK(15, 11),
										 ret)));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
mp2925_identify_vout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			   int page)
{
	struct mp2925_data *data = to_mp2925_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, PMBUS_VOUT_MODE);
	if (ret < 0)
		return ret;

	if (FIELD_GET(GENMASK(5, 5), ret)) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE,
						page == 0 ? 3 : 4);
		if (ret < 0)
			return ret;

		ret = i2c_smbus_read_word_data(client, MFR_VR_MULTI_CONFIG);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(5, 5), ret))
			data->vout_scale[page] = 2560;
		else
			data->vout_scale[page] = 5120;
	} else if (FIELD_GET(GENMASK(4, 4), ret)) {
		data->vout_scale[page] = 1;
	} else {
		data->vout_scale[page] = 512;
	}

	return 0;
}

static int mp2925_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	ret = mp2925_identify_vout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	return mp2925_identify_vout_scale(client, info, 1);
}

static const struct pmbus_driver_info mp2925_info = {
	.pages = MP2925_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.func[0] = MP2925_RAIL1_FUNC,
	.func[1] = MP2925_RAIL2_FUNC,
	.read_word_data = mp2925_read_word_data,
	.read_byte_data = mp2925_read_byte_data,
	.write_word_data = mp2925_write_word_data,
	.identify = mp2925_identify,
};

static int mp2925_probe(struct i2c_client *client)
{
	struct mp2925_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct mp2925_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp2925_info, sizeof(mp2925_info));

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id mp2925_id[] = {
	{"mp2925"},
	{"mp2929"},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp2925_id);

static const struct of_device_id __maybe_unused mp2925_of_match[] = {
	{.compatible = "mps,mp2925"},
	{.compatible = "mps,mp2929"},
	{}
};
MODULE_DEVICE_TABLE(of, mp2925_of_match);

static struct i2c_driver mp2925_driver = {
	.driver = {
		.name = "mp2925",
		.of_match_table = mp2925_of_match,
	},
	.probe = mp2925_probe,
	.id_table = mp2925_id,
};

module_i2c_driver(mp2925_driver);

MODULE_AUTHOR("Wensheng Wang <wenswang@yeah.net>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2925");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
