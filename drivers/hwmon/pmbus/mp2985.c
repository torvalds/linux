// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP2985)
 *
 * Copyright (C) 2026 MPS
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

/*
 * Vender specific register READ_PIN_EST(0x93), READ_IIN_EST(0x8E),
 * MFR_VR_MULTI_CONFIG_R1(0x0D) and MFR_VR_MULTI_CONFIG_R2(0x1D).
 * The READ_PIN_EST is used to read pin telemetry, the READ_IIN_EST
 * is used to read iin telemetry and the MFR_VR_MULTI_CONFIG_R1,
 * MFR_VR_MULTI_CONFIG_R2 are used to obtain vid scale.
 */
#define READ_PIN_EST	0x93
#define READ_IIN_EST	0x8E
#define MFR_VR_MULTI_CONFIG_R1	0x0D
#define MFR_VR_MULTI_CONFIG_R2	0x1D

#define MP2985_VOUT_DIV	64
#define MP2985_VOUT_OVUV_UINT	125
#define MP2985_VOUT_OVUV_DIV	64

#define MP2985_PAGE_NUM	2

#define MP2985_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_PIN | \
							 PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | \
							 PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP | \
							 PMBUS_HAVE_STATUS_VOUT | \
							 PMBUS_HAVE_STATUS_IOUT | \
							 PMBUS_HAVE_STATUS_TEMP | \
							 PMBUS_HAVE_STATUS_INPUT)

#define MP2985_RAIL2_FUNC	(PMBUS_HAVE_PIN | PMBUS_HAVE_VOUT | \
							 PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | \
							 PMBUS_HAVE_TEMP | PMBUS_HAVE_IIN | \
							 PMBUS_HAVE_STATUS_VOUT | \
							 PMBUS_HAVE_STATUS_IOUT | \
							 PMBUS_HAVE_STATUS_TEMP | \
							 PMBUS_HAVE_STATUS_INPUT)

struct mp2985_data {
	struct pmbus_driver_info info;
	int vout_scale[MP2985_PAGE_NUM];
	int vid_offset[MP2985_PAGE_NUM];
};

#define to_mp2985_data(x) container_of(x, struct mp2985_data, info)

static u16 mp2985_linear_exp_transfer(u16 word, u16 expect_exponent)
{
	s16 exponent, mantissa, target_exponent;

	exponent = ((s16)word) >> 11;
	mantissa = ((s16)((word & 0x7ff) << 5)) >> 5;
	target_exponent = (s16)((expect_exponent & 0x1f) << 11) >> 11;

	/*
	 * The MP2985 does not support negtive limit value, if a negtive
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

static int mp2985_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * The MP2985 does not follow standard PMBus protocol completely,
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

static int mp2985_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2985_data *data = to_mp2985_data(info);
	int ret;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		/*
		 * The MP2985 supports three vout mode, direct, linear11 and vid mode.
		 * In vid mode, the MP2985 vout telemetry has 49 vid step offset, but
		 * PMBUS_VOUT_OV_FAULT_LIMIT and PMBUS_VOUT_UV_FAULT_LIMIT do not take
		 * this into consideration, their resolution are 1.953125mV/LSB, as a
		 * result, format[PSC_VOLTAGE_OUT] can not be set to vid mode directly.
		 * Adding extra vid_offset variable for vout telemetry.
		 */
		ret = clamp_val(DIV_ROUND_CLOSEST(((ret & GENMASK(11, 0)) +
									data->vid_offset[page]) *
							data->vout_scale[page], MP2985_VOUT_DIV),
							0, 0x7FFF);
		break;
	case PMBUS_READ_IIN:
		/*
		 * The MP2985 has standard PMBUS_READ_IIN register(0x89), but this is
		 * not used to read the input current of per rail. The input current
		 * is read through the vender redefined register READ_IIN_EST(0x8E).
		 */
		ret = pmbus_read_word_data(client, page, phase, READ_IIN_EST);
		break;
	case PMBUS_READ_PIN:
		/*
		 * The MP2985 has standard PMBUS_READ_PIN register(0x97), but this
		 * is not used to read the input power of per rail. The input power
		 * of per rail is read through the vender redefined register
		 * READ_PIN_EST(0x93).
		 */
		ret = pmbus_read_word_data(client, page, phase, READ_PIN_EST);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(11, 0)) * MP2985_VOUT_OVUV_UINT,
					MP2985_VOUT_OVUV_DIV);
		break;
	case PMBUS_STATUS_WORD:
	case PMBUS_READ_VIN:
	case PMBUS_READ_IOUT:
	case PMBUS_READ_POUT:
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * These register is not explicitly handled by the driver,
		 * as a result, return -ENODATA directly.
		 */
		ret = -ENODATA;
		break;
	default:
		/*
		 * The MP2985 do not support other telemetry and limit value
		 * reading, so, return -EINVAL directly.
		 */
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp2985_write_word_data(struct i2c_client *client, int page, int reg,
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
		 * of MP2985 is linear11 format, and the exponent is a
		 * constant value(5'b11101), so the exponent of word
		 * parameter should be converted to 5'b11101(0x1D).
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mp2985_linear_exp_transfer(word, 0x1D));
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
				clamp_val(DIV_ROUND_CLOSEST(word * MP2985_VOUT_OVUV_DIV,
							    MP2985_VOUT_OVUV_UINT), 0, 0xFFF));
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT of
		 * MP2985 is linear11 format, and the exponent is a
		 * constant value(5'b00000), so the exponent of word
		 * parameter should be converted to 5'b00000.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    mp2985_linear_exp_transfer(word, 0x00));
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		/*
		 * The PMBUS_IOUT_OC_FAULT_LIMIT and PMBUS_IOUT_OC_WARN_LIMIT
		 * of MP2985 is linear11 format, and the exponent can not be
		 * changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    mp2985_linear_exp_transfer(word,
								       FIELD_GET(GENMASK(15, 11),
										 ret)));
		break;
	default:
		/*
		 * The MP2985 do not support other limit value configuration,
		 * so, return -EINVAL directly.
		 */
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
mp2985_identify_vout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			   int page)
{
	struct mp2985_data *data = to_mp2985_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, PMBUS_VOUT_MODE);
	if (ret < 0)
		return ret;

	/*
	 * The MP2985 supports three vout mode. If PMBUS_VOUT_MODE
	 * bit5 is 1, it is vid mode. If PMBUS PMBUS_VOUT_MODE bit4
	 * is 1, it is linear11 mode, the vout scale is 1.953125mv/LSB.
	 * If PMBUS PMBUS_VOUT_MODE bit6 is 1, it is direct mode, the
	 * vout scale is 1mv/LSB. In vid mode, the MP2985 vout telemetry
	 * has 49 vid step offset.
	 */
	if (FIELD_GET(BIT(5), ret)) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
		if (ret < 0)
			return ret;

		ret = i2c_smbus_read_word_data(client, page == 0 ?
						MFR_VR_MULTI_CONFIG_R1 :
						MFR_VR_MULTI_CONFIG_R2);
		if (ret < 0)
			return ret;

		if (page == 0) {
			if (FIELD_GET(BIT(4), ret))
				data->vout_scale[page] = 320;
			else
				data->vout_scale[page] = 640;
		} else {
			if (FIELD_GET(BIT(3), ret))
				data->vout_scale[page] = 320;
			else
				data->vout_scale[page] = 640;
		}

		data->vid_offset[page] = 49;

		/*
		 * For vid mode, the MP2985 should be changed to page 2
		 * to obtain vout scale value, this may confuse the PMBus
		 * core. To avoid this, switch back to the previous page
		 * again.
		 */
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
		if (ret < 0)
			return ret;
	} else if (FIELD_GET(BIT(4), ret)) {
		data->vout_scale[page] = 125;
		data->vid_offset[page] = 0;
	} else {
		data->vout_scale[page] = 64;
		data->vid_offset[page] = 0;
	}

	return 0;
}

static int mp2985_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	ret = mp2985_identify_vout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	return mp2985_identify_vout_scale(client, info, 1);
}

static struct pmbus_driver_info mp2985_info = {
	.pages = MP2985_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.func[0] = MP2985_RAIL1_FUNC,
	.func[1] = MP2985_RAIL2_FUNC,
	.read_word_data = mp2985_read_word_data,
	.read_byte_data = mp2985_read_byte_data,
	.write_word_data = mp2985_write_word_data,
	.identify = mp2985_identify,
};

static int mp2985_probe(struct i2c_client *client)
{
	struct mp2985_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct mp2985_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp2985_info, sizeof(mp2985_info));

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id mp2985_id[] = {
	{"mp2985", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp2985_id);

static const struct of_device_id __maybe_unused mp2985_of_match[] = {
	{.compatible = "mps,mp2985"},
	{}
};
MODULE_DEVICE_TABLE(of, mp2985_of_match);

static struct i2c_driver mp2985_driver = {
	.driver = {
		.name = "mp2985",
		.of_match_table = mp2985_of_match,
	},
	.probe = mp2985_probe,
	.id_table = mp2985_id,
};

module_i2c_driver(mp2985_driver);

MODULE_AUTHOR("Wensheng Wang <wenswang@yeah.net>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2985 device");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
