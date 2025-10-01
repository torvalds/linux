// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP29502)
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

#define MFR_VOUT_SCALE_LOOP	0x29
#define MFR_SVI3_IOUT_PRT	0x67
#define MFR_READ_PIN_EST	0x94
#define MFR_READ_IIN_EST	0x95
#define MFR_VOUT_PROT1	0x3D
#define MFR_VOUT_PROT2	0x51
#define MFR_SLOPE_CNT_SET	0xA8
#define MFR_TSNS_FLT_SET	0xBB

#define MP29502_VIN_OV_GAIN	4
#define MP29502_TEMP_LIMIT_OFFSET	40
#define MP29502_READ_VOUT_DIV	1024
#define MP29502_READ_IOUT_DIV	32
#define MP29502_IOUT_LIMIT_UINT	8
#define MP29502_OVUV_LIMIT_SCALE	10
#define MP28502_VOUT_OV_GAIN	512
#define MP28502_VOUT_OV_SCALE	40
#define MP29502_VOUT_UV_OFFSET	36
#define MP29502_PIN_GAIN	2
#define MP29502_IIN_DIV	2

#define MP29502_PAGE_NUM	1

#define MP29502_RAIL_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
							PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | \
							PMBUS_HAVE_TEMP | PMBUS_HAVE_PIN | \
							PMBUS_HAVE_IIN | \
							PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_TEMP | \
							PMBUS_HAVE_STATUS_INPUT)

struct mp29502_data {
	struct pmbus_driver_info info;
	int vout_scale;
	int vout_bottom_div;
	int vout_top_div;
	int ovp_div;
	int iout_scale;
};

#define to_mp29502_data(x)	container_of(x, struct mp29502_data, info)

static u16 mp29502_reg2data_linear11(u16 word)
{
	s16 exponent;
	s32 mantissa;
	s64 val;

	exponent = ((s16)word) >> 11;
	mantissa = ((s16)((word & 0x7ff) << 5)) >> 5;
	val = mantissa;

	if (exponent >= 0)
		val <<= exponent;
	else
		val >>= -exponent;

	return val;
}

static int
mp29502_identify_vout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			    int page)
{
	struct mp29502_data *data = to_mp29502_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_VOUT_SCALE_LOOP);
	if (ret < 0)
		return ret;

	switch (FIELD_GET(GENMASK(12, 10), ret)) {
	case 0:
		data->vout_scale = 6400;
		break;
	case 1:
		data->vout_scale = 5120;
		break;
	case 2:
		data->vout_scale = 2560;
		break;
	case 3:
		data->vout_scale = 2048;
		break;
	case 4:
		data->vout_scale = 1024;
		break;
	case 5:
		data->vout_scale = 4;
		break;
	case 6:
		data->vout_scale = 2;
		break;
	case 7:
		data->vout_scale = 1;
		break;
	default:
		data->vout_scale = 1;
		break;
	}

	return 0;
}

static int
mp29502_identify_vout_divider(struct i2c_client *client, struct pmbus_driver_info *info,
			      int page)
{
	struct mp29502_data *data = to_mp29502_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_VOUT_PROT1);
	if (ret < 0)
		return ret;

	data->vout_bottom_div = FIELD_GET(GENMASK(11, 0), ret);

	ret = i2c_smbus_read_word_data(client, MFR_VOUT_PROT2);
	if (ret < 0)
		return ret;

	data->vout_top_div = FIELD_GET(GENMASK(14, 0), ret);

	return 0;
}

static int
mp29502_identify_ovp_divider(struct i2c_client *client, struct pmbus_driver_info *info,
			     int page)
{
	struct mp29502_data *data = to_mp29502_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_SLOPE_CNT_SET);
	if (ret < 0)
		return ret;

	data->ovp_div = FIELD_GET(GENMASK(9, 0), ret);

	return 0;
}

static int
mp29502_identify_iout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			    int page)
{
	struct mp29502_data *data = to_mp29502_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_SVI3_IOUT_PRT);
	if (ret < 0)
		return ret;

	switch (ret & GENMASK(2, 0)) {
	case 0:
	case 6:
		data->iout_scale = 32;
		break;
	case 1:
		data->iout_scale = 1;
		break;
	case 2:
		data->iout_scale = 2;
		break;
	case 3:
		data->iout_scale = 4;
		break;
	case 4:
		data->iout_scale = 8;
		break;
	case 5:
		data->iout_scale = 16;
		break;
	default:
		data->iout_scale = 64;
		break;
	}

	return 0;
}

static int mp29502_read_vout_ov_limit(struct i2c_client *client, struct mp29502_data *data)
{
	int ret;
	int ov_value;

	/*
	 * This is because the vout ov fault limit value comes from
	 * page1 MFR_TSNS_FLT_SET reg, and other telemetry and limit
	 * value comes from page0 reg. So the page should be set to
	 * 0 after the reading of vout ov limit.
	 */
	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 1);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_TSNS_FLT_SET);
	if (ret < 0)
		return ret;

	ov_value = DIV_ROUND_CLOSEST(FIELD_GET(GENMASK(12, 7), ret) *
						   MP28502_VOUT_OV_GAIN * MP28502_VOUT_OV_SCALE,
						   data->ovp_div);

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	return ov_value;
}

static int mp29502_write_vout_ov_limit(struct i2c_client *client, u16 word,
				       struct mp29502_data *data)
{
	int ret;

	/*
	 * This is because the vout ov fault limit value comes from
	 * page1 MFR_TSNS_FLT_SET reg, and other telemetry and limit
	 * value comes from page0 reg. So the page should be set to
	 * 0 after the writing of vout ov limit.
	 */
	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 1);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_TSNS_FLT_SET);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_word_data(client, MFR_TSNS_FLT_SET,
					(ret & ~GENMASK(12, 7)) |
		FIELD_PREP(GENMASK(12, 7),
			   DIV_ROUND_CLOSEST(word * data->ovp_div,
					     MP28502_VOUT_OV_GAIN * MP28502_VOUT_OV_SCALE)));

	return i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
}

static int mp29502_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		ret = PB_VOUT_MODE_DIRECT;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int mp29502_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp29502_data *data = to_mp29502_data(info);
	int ret;

	switch (reg) {
	case PMBUS_STATUS_WORD:
		ret = -ENODATA;
		break;
	case PMBUS_READ_VIN:
		/*
		 * The MP29502 PMBUS_READ_VIN[10:0] is the vin value, the vin scale is
		 * 125mV/LSB. And the vin scale is set to 125mV/Lsb(using r/m/b scale)
		 * in MP29502 pmbus_driver_info struct, so the word data bit0-bit10 can
		 * be returned to pmbus core directly.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(10, 0), ret);
		break;
	case PMBUS_READ_VOUT:
		/*
		 * The MP29502 PMBUS_READ_VOUT[11:0] is the vout value, and vout
		 * value is calculated based on vout scale and vout divider.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret &  GENMASK(11, 0)) *
								data->vout_scale *
								(data->vout_bottom_div +
								4 * data->vout_top_div),
								MP29502_READ_VOUT_DIV *
								data->vout_bottom_div);
		break;
	case PMBUS_READ_IIN:
		/*
		 * The MP29502 MFR_READ_IIN_EST register is linear11 format, and the
		 * exponent is not a constant value. But the iin scale is set to
		 * 1A/Lsb(using r/m/b scale). As a result, the iin read from MP29502
		 * should be calculated to A, then return the result to pmbus core.
		 */
		ret = pmbus_read_word_data(client, page, phase, MFR_READ_IIN_EST);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(mp29502_reg2data_linear11(ret),
					MP29502_IIN_DIV);
		break;
	case PMBUS_READ_PIN:
		/*
		 * The MP29502 MFR_READ_PIN_EST register is linear11 format, and the
		 * exponent is not a constant value. But the pin scale is set to
		 * 1W/Lsb(using r/m/b scale). As a result, the pout read from MP29502
		 * should be calculated to W, then return the result to pmbus core.
		 */
		ret = pmbus_read_word_data(client, page, phase, MFR_READ_PIN_EST);
		if (ret < 0)
			return ret;

		ret = mp29502_reg2data_linear11(ret) * MP29502_PIN_GAIN;
		break;
	case PMBUS_READ_POUT:
		/*
		 * The MP29502 PMBUS_READ_POUT register is linear11 format, and the
		 * exponent is not a constant value. But the pout scale is set to
		 * 1W/Lsb(using r/m/b scale). As a result, the pout read from MP29502
		 * should be calculated to W, then return the result to pmbus core.
		 * And the pout is calculated based on vout divider.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(mp29502_reg2data_linear11(ret) *
					(data->vout_bottom_div +
					4 * data->vout_top_div),
					data->vout_bottom_div);
		break;
	case PMBUS_READ_IOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(10, 0)) * data->iout_scale,
					MP29502_READ_IOUT_DIV);
		break;
	case PMBUS_READ_TEMPERATURE_1:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(10, 0), ret);
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/*
		 * The MP29502 PMBUS_VIN_OV_FAULT_LIMIT is 500mV/Lsb, but
		 * the vin  scale is set to 125mV/Lsb(using r/m/b scale),
		 * so the word data should multiply by 4.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(7, 0), ret) * MP29502_VIN_OV_GAIN;
		break;
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		/*
		 * The MP29502 PMBUS_VIN_UV_WARN_LIMIT and PMBUS_VIN_UV_FAULT_LIMIT
		 * scale is 125mV/Lsb, but the vin scale is set to 125mV/Lsb(using
		 * r/m/b scale), so the word data bit0-bit9 can be returned to pmbus
		 * core directly.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(9, 0), ret);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/*
		 * The MP29502 vout ov fault limit value comes from
		 * page1 MFR_TSNS_FLT_SET[12:7].
		 */
		ret = mp29502_read_vout_ov_limit(client, data);
		if (ret < 0)
			return ret;

		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((FIELD_GET(GENMASK(8, 0), ret) *
								MP29502_OVUV_LIMIT_SCALE -
								MP29502_VOUT_UV_OFFSET) *
								(data->vout_bottom_div +
								4 * data->vout_top_div),
								data->vout_bottom_div);
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(7, 0)) *
								data->iout_scale *
								MP29502_IOUT_LIMIT_UINT,
								MP29502_READ_IOUT_DIV);
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The scale of MP29502 PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT
		 * is 1°C/LSB and they have 40°C offset.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(7, 0)) - MP29502_TEMP_LIMIT_OFFSET;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp29502_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp29502_data *data = to_mp29502_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);
	if (ret < 0)
		return ret;

	switch (reg) {
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/*
		 * The PMBUS_VIN_OV_FAULT_LIMIT[7:0] is the limit value,
		 * and bit8-bit15 should not be changed. The scale of
		 * PMBUS_VIN_OV_FAULT_LIMIT is 500mV/Lsb, but the vin
		 * scale is set to 125mV/Lsb(using r/m/b scale), so
		 * the word data should divide by 4.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(7, 0)) |
				FIELD_PREP(GENMASK(7, 0),
					   DIV_ROUND_CLOSEST(word,
							     MP29502_VIN_OV_GAIN)));
		break;
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		/*
		 * The PMBUS_VIN_UV_WARN_LIMIT[9:0] and PMBUS_VIN_UV_FAULT_LIMIT[9:0]
		 * are the limit value, and bit10-bit15 should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(9, 0)) |
							FIELD_PREP(GENMASK(9, 0),
								   word));
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		ret = mp29502_write_vout_ov_limit(client, word, data);
		if (ret < 0)
			return ret;

		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(8, 0)) |
						FIELD_PREP(GENMASK(8, 0),
							   DIV_ROUND_CLOSEST(word *
									data->vout_bottom_div +
									MP29502_VOUT_UV_OFFSET *
									(data->vout_bottom_div +
									4 * data->vout_top_div),
									MP29502_OVUV_LIMIT_SCALE *
									(data->vout_bottom_div +
									4 * data->vout_top_div))));
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word *
							MP29502_READ_IOUT_DIV,
							MP29502_IOUT_LIMIT_UINT *
							data->iout_scale));
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The PMBUS_OT_FAULT_LIMIT[7:0] and PMBUS_OT_WARN_LIMIT[7:0]
		 * are the limit value, and bit8-bit15 should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(7, 0)) |
					FIELD_PREP(GENMASK(7, 0),
						   word + MP29502_TEMP_LIMIT_OFFSET));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp29502_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	/* Identify vout scale */
	ret = mp29502_identify_vout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	/* Identify vout divider. */
	ret = mp29502_identify_vout_divider(client, info, 1);
	if (ret < 0)
		return ret;

	/* Identify ovp divider. */
	ret = mp29502_identify_ovp_divider(client, info, 1);
	if (ret < 0)
		return ret;

	/* Identify iout scale */
	return mp29502_identify_iout_scale(client, info, 0);
}

static const struct pmbus_driver_info mp29502_info = {
	.pages = MP29502_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_POWER] = direct,

	.m[PSC_VOLTAGE_IN] = 8,
	.R[PSC_VOLTAGE_IN] = 0,
	.b[PSC_VOLTAGE_IN] = 0,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.m[PSC_TEMPERATURE] = 1,
	.R[PSC_TEMPERATURE] = 0,
	.b[PSC_TEMPERATURE] = 0,

	.m[PSC_CURRENT_IN] = 1,
	.R[PSC_CURRENT_IN] = 0,
	.b[PSC_CURRENT_IN] = 0,

	.m[PSC_CURRENT_OUT] = 1,
	.R[PSC_CURRENT_OUT] = 0,
	.b[PSC_CURRENT_OUT] = 0,

	.m[PSC_POWER] = 1,
	.R[PSC_POWER] = 0,
	.b[PSC_POWER] = 0,

	.func[0] = MP29502_RAIL_FUNC,
	.read_word_data = mp29502_read_word_data,
	.read_byte_data = mp29502_read_byte_data,
	.write_word_data = mp29502_write_word_data,
	.identify = mp29502_identify,
};

static int mp29502_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	struct mp29502_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct mp29502_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp29502_info, sizeof(*info));
	info = &data->info;

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id mp29502_id[] = {
	{"mp29502", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp29502_id);

static const struct of_device_id __maybe_unused mp29502_of_match[] = {
	{.compatible = "mps,mp29502"},
	{}
};
MODULE_DEVICE_TABLE(of, mp29502_of_match);

static struct i2c_driver mp29502_driver = {
	.driver = {
		.name = "mp29502",
		.of_match_table = mp29502_of_match,
	},
	.probe = mp29502_probe,
	.id_table = mp29502_id,
};

module_i2c_driver(mp29502_driver);

MODULE_AUTHOR("Wensheng Wang <wenswang@yeah.net");
MODULE_DESCRIPTION("PMBus driver for MPS MP29502");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
