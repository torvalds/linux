// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP2891)
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

/*
 * Vender specific registers, the register MFR_SVI3_IOUT_PRT(0x65),
 * MFR_VOUT_LOOP_CTRL(0xBD), READ_PIN_EST(0x94)and READ_IIN_EST(0x95)
 * redefine the standard PMBUS register. The MFR_SVI3_IOUT_PRT(0x65)
 * is used to identify the iout scale and the MFR_VOUT_LOOP_CTRL(0xBD)
 * is used to identify the vout scale. The READ_PIN_EST(0x94) is used
 * to read input power per rail. The MP2891 does not have standard
 * READ_IIN register(0x89), the iin telemetry can be obtained through
 * the vendor redefined register READ_IIN_EST(0x95).
 */
#define MFR_VOUT_LOOP_CTRL	0xBD
#define READ_PIN_EST	0x94
#define READ_IIN_EST	0x95
#define MFR_SVI3_IOUT_PRT	0x65

#define MP2891_TEMP_LIMIT_OFFSET	40
#define MP2891_PIN_LIMIT_UINT	2
#define MP2891_IOUT_LIMIT_UINT	8
#define MP2891_IOUT_SCALE_DIV	32
#define MP2891_VOUT_SCALE_DIV	100
#define MP2891_OVUV_DELTA_SCALE	50
#define MP2891_OV_LIMIT_SCALE	20
#define MP2891_UV_LIMIT_SCALE	5

#define MP2891_PAGE_NUM	2

#define MP2891_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
							PMBUS_HAVE_IOUT | PMBUS_HAVE_TEMP | \
							PMBUS_HAVE_POUT | PMBUS_HAVE_PIN | \
							PMBUS_HAVE_IIN | PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_INPUT | \
							PMBUS_HAVE_STATUS_TEMP)

#define MP2891_RAIL2_FUNC	(PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | \
							PMBUS_HAVE_TEMP | PMBUS_HAVE_POUT | \
							PMBUS_HAVE_PIN | PMBUS_HAVE_IIN | \
							PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_INPUT | \
							PMBUS_HAVE_STATUS_TEMP)

struct mp2891_data {
	struct pmbus_driver_info info;
	int vout_scale[MP2891_PAGE_NUM];
	int iout_scale[MP2891_PAGE_NUM];
};

#define to_mp2891_data(x) container_of(x, struct mp2891_data, info)

/* Converts a LINEAR11 value to DIRECT format */
static u16 mp2891_reg2data_linear11(u16 word)
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
mp2891_identify_vout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			   int page)
{
	struct mp2891_data *data = to_mp2891_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_VOUT_LOOP_CTRL);
	if (ret < 0)
		return ret;

	/*
	 * The output voltage is equal to the READ_VOUT(0x8B) register value multiplied
	 * by vout_scale.
	 * Obtain vout scale from the register MFR_VOUT_LOOP_CTRL, bits 15-14,bit 13.
	 * If MFR_VOUT_LOOP_CTRL[13] = 1, the vout scale is below:
	 * 2.5mV/LSB
	 * If MFR_VOUT_LOOP_CTRL[13] = 0, the vout scale is decided by
	 * MFR_VOUT_LOOP_CTRL[15:14]:
	 * 00b - 6.25mV/LSB, 01b - 5mV/LSB, 10b - 2mV/LSB, 11b - 1mV
	 */
	if (ret & GENMASK(13, 13)) {
		data->vout_scale[page] = 250;
	} else {
		ret = FIELD_GET(GENMASK(15, 14), ret);
		if (ret == 0)
			data->vout_scale[page] = 625;
		else if (ret == 1)
			data->vout_scale[page] = 500;
		else if (ret == 2)
			data->vout_scale[page] = 200;
		else
			data->vout_scale[page] = 100;
	}

	return 0;
}

static int
mp2891_identify_iout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			   int page)
{
	struct mp2891_data *data = to_mp2891_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_SVI3_IOUT_PRT);
	if (ret < 0)
		return ret;

	/*
	 * The output current is equal to the READ_IOUT(0x8C) register value
	 * multiplied by iout_scale.
	 * Obtain iout_scale from the register MFR_SVI3_IOUT_PRT[2:0].
	 * The value is selected as below:
	 * 000b - 1A/LSB, 001b - (1/32)A/LSB, 010b - (1/16)A/LSB,
	 * 011b - (1/8)A/LSB, 100b - (1/4)A/LSB, 101b - (1/2)A/LSB
	 * 110b - 1A/LSB, 111b - 2A/LSB
	 */
	switch (ret & GENMASK(2, 0)) {
	case 0:
	case 6:
		data->iout_scale[page] = 32;
		break;
	case 1:
		data->iout_scale[page] = 1;
		break;
	case 2:
		data->iout_scale[page] = 2;
		break;
	case 3:
		data->iout_scale[page] = 4;
		break;
	case 4:
		data->iout_scale[page] = 8;
		break;
	case 5:
		data->iout_scale[page] = 16;
		break;
	default:
		data->iout_scale[page] = 64;
		break;
	}

	return 0;
}

static int mp2891_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	/* Identify vout scale for rail 1. */
	ret = mp2891_identify_vout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	/* Identify vout scale for rail 2. */
	ret = mp2891_identify_vout_scale(client, info, 1);
	if (ret < 0)
		return ret;

	/* Identify iout scale for rail 1. */
	ret = mp2891_identify_iout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	/* Identify iout scale for rail 2. */
	return mp2891_identify_iout_scale(client, info, 1);
}

static int mp2891_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * The MP2891 does not follow standard PMBus protocol completely, the
		 * PMBUS_VOUT_MODE(0x20) in MP2891 is reserved and 0x00 is always
		 * returned when the register is read. But the calculation of vout in
		 * this driver is based on direct format. As a result, the format of
		 * vout is enforced to direct.
		 */
		ret = PB_VOUT_MODE_DIRECT;
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int mp2891_read_word_data(struct i2c_client *client, int page,
				 int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2891_data *data = to_mp2891_data(info);
	int ret;

	switch (reg) {
	case PMBUS_READ_VIN:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = ret & GENMASK(9, 0);
		break;
	case PMBUS_READ_IIN:
		/*
		 * The MP2891 does not have standard PMBUS_READ_IIN register(0x89),
		 * the iin telemetry can be obtained through the vender redefined
		 * register READ_IIN_EST(0x95). The MP2891 PMBUS_READ_IIN register
		 * is linear11 format, But the pout scale is set to 1A/Lsb(using
		 * r/m/b scale). As a result, the iin read from MP2891 should be
		 * calculated to A, then return the result to pmbus core.
		 */
		ret = pmbus_read_word_data(client, page, phase, READ_IIN_EST);
		if (ret < 0)
			return ret;

		ret = mp2891_reg2data_linear11(ret);
		break;
	case PMBUS_READ_PIN:
		/*
		 * The MP2891 has standard PMBUS_READ_PIN register(0x97), but this
		 * is not used to read the input power per rail. The input power
		 * per rail is read through the vender redefined register
		 * READ_PIN_EST(0x94). The MP2891 PMBUS_READ_PIN register is linear11
		 * format, But the pout scale is set to 1W/Lsb(using r/m/b scale).
		 * As a result, the pin read from MP2891 should be calculated to W,
		 * then return the result to pmbus core.
		 */
		ret = pmbus_read_word_data(client, page, phase, READ_PIN_EST);
		if (ret < 0)
			return ret;

		ret = mp2891_reg2data_linear11(ret);
		break;
	case PMBUS_READ_POUT:
		/*
		 * The MP2891 PMBUS_READ_POUT register is linear11 format, and the
		 * exponent is not a constant value. But the pout scale is set to
		 * 1W/Lsb(using r/m/b scale). As a result, the pout read from MP2891
		 * should be calculated to W, then return the result to pmbus core.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = mp2891_reg2data_linear11(ret);
		break;
	case PMBUS_READ_VOUT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST(ret * data->vout_scale[page], MP2891_VOUT_SCALE_DIV);
		break;
	case PMBUS_READ_IOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(10, 0)) * data->iout_scale[page],
					MP2891_IOUT_SCALE_DIV);
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The scale of MP2891 PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT
		 * is 1°C/LSB and they have 40°C offset.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(7, 0)) - MP2891_TEMP_LIMIT_OFFSET;
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/*
		 * The MP2891 PMBUS_VIN_OV_FAULT_LIMIT scale is 125mV/Lsb.
		 * but the vin scale is set to 31.25mV/Lsb(using r/m/b scale).
		 * As a result, the limit value should be multiplied by 4.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(7, 0)) * 4;
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(11, 8), ret))
			ret = FIELD_GET(GENMASK(7, 0), ret) * MP2891_UV_LIMIT_SCALE -
				(FIELD_GET(GENMASK(11, 8), ret) + 1) * MP2891_OVUV_DELTA_SCALE;
		else
			ret = FIELD_GET(GENMASK(7, 0), ret) * MP2891_UV_LIMIT_SCALE;

		ret = ret < 0 ? 0 : ret;
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(11, 8), ret))
			ret = FIELD_GET(GENMASK(7, 0), ret) * MP2891_OV_LIMIT_SCALE +
				(FIELD_GET(GENMASK(11, 8), ret) + 1) * MP2891_OVUV_DELTA_SCALE;
		else
			ret = FIELD_GET(GENMASK(7, 0), ret) * MP2891_OV_LIMIT_SCALE;
		break;
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(7, 0)) * data->iout_scale[page] *
						MP2891_IOUT_LIMIT_UINT, MP2891_IOUT_SCALE_DIV);
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		/*
		 * The scale of PMBUS_IIN_OC_WARN_LIMIT is 0.5A/Lsb, but the iin scale
		 * is set to 1A/Lsb(using r/m/b scale), so the word data should be
		 * divided by 2.
		 */
		ret = pmbus_read_word_data(client, 0, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(9, 0)), 2);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		/*
		 * The scale of PMBUS_PIN_OP_WARN_LIMIT is 2W/Lsb, but the pin scale
		 * is set to 1W/Lsb(using r/m/b scale), so the word data should be
		 * multiplied by 2.
		 */
		ret = pmbus_read_word_data(client, 0, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(9, 0)) * MP2891_PIN_LIMIT_UINT;
		break;
	case PMBUS_READ_TEMPERATURE_1:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
		ret = -ENODATA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp2891_write_word_data(struct i2c_client *client, int page, int reg,
				  u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2891_data *data = to_mp2891_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VOUT_UV_WARN_LIMIT:
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word * MP2891_VOUT_SCALE_DIV,
							      data->vout_scale[page]));
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		/*
		 * The PMBUS_VOUT_UV_FAULT_LIMIT[7:0] is the limit value, and bit8-bit15
		 * should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(11, 8), ret))
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(7, 0)) |
							FIELD_PREP(GENMASK(7, 0),
								   DIV_ROUND_CLOSEST(word +
							(FIELD_GET(GENMASK(11, 8), ret) + 1) *
								MP2891_OVUV_DELTA_SCALE,
								MP2891_UV_LIMIT_SCALE)));
		else
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(7, 0)) |
					FIELD_PREP(GENMASK(7, 0),
						   DIV_ROUND_CLOSEST(word,
								     MP2891_UV_LIMIT_SCALE)));
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/*
		 * The PMBUS_VOUT_OV_FAULT_LIMIT[7:0] is the limit value, and bit8-bit15
		 * should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(11, 8), ret))
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(7, 0)) |
							FIELD_PREP(GENMASK(7, 0),
								   DIV_ROUND_CLOSEST(word -
							(FIELD_GET(GENMASK(11, 8), ret) + 1) *
								MP2891_OVUV_DELTA_SCALE,
								MP2891_OV_LIMIT_SCALE)));
		else
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(7, 0)) |
					FIELD_PREP(GENMASK(7, 0),
						   DIV_ROUND_CLOSEST(word,
								     MP2891_OV_LIMIT_SCALE)));
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/*
		 * The PMBUS_VIN_OV_FAULT_LIMIT[7:0] is the limit value, and bit8-bit15
		 * should not be changed. The scale of PMBUS_VIN_OV_FAULT_LIMIT is 125mV/Lsb,
		 * but the vin scale is set to 31.25mV/Lsb(using r/m/b scale), so the word data
		 * should be divided by 4.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(7, 0)) |
						FIELD_PREP(GENMASK(7, 0),
							   DIV_ROUND_CLOSEST(word, 4)));
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The scale of MP2891 PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT
		 * have 40°C offset. The bit0-bit7 is the limit value, and bit8-bit15
		 * should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(7, 0)) |
				FIELD_PREP(GENMASK(7, 0), word + MP2891_TEMP_LIMIT_OFFSET));
		break;
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word * MP2891_IOUT_SCALE_DIV,
							      MP2891_IOUT_LIMIT_UINT *
								  data->iout_scale[page]));
		break;
	case PMBUS_IIN_OC_WARN_LIMIT:
		/*
		 * The scale of PMBUS_IIN_OC_WARN_LIMIT is 0.5A/Lsb, but the iin scale
		 * is set to 1A/Lsb(using r/m/b scale), so the word data should be
		 * multiplied by 2.
		 */
		ret = pmbus_write_word_data(client, page, reg, word * 2);
		break;
	case PMBUS_PIN_OP_WARN_LIMIT:
		/*
		 * The scale of PMBUS_PIN_OP_WARN_LIMIT is 2W/Lsb, but the pin scale
		 * is set to 1W/Lsb(using r/m/b scale), so the word data should be
		 * divided by 2.
		 */
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word, MP2891_PIN_LIMIT_UINT));
		break;
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
		ret = -ENODATA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct pmbus_driver_info mp2891_info = {
	.pages = MP2891_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,

	/* set vin scale 31.25mV/Lsb */
	.m[PSC_VOLTAGE_IN] = 32,
	.R[PSC_VOLTAGE_IN] = 0,
	.b[PSC_VOLTAGE_IN] = 0,

	/* set temp scale 1000m°C/Lsb */
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

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.func[0] = MP2891_RAIL1_FUNC,
	.func[1] = MP2891_RAIL2_FUNC,
	.read_word_data = mp2891_read_word_data,
	.write_word_data = mp2891_write_word_data,
	.read_byte_data = mp2891_read_byte_data,
	.identify = mp2891_identify,
};

static int mp2891_probe(struct i2c_client *client)
{
	struct mp2891_data *data;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp2891_info, sizeof(mp2891_info));

	return pmbus_do_probe(client, &data->info);
}

static const struct i2c_device_id mp2891_id[] = {
	{ "mp2891" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mp2891_id);

static const struct of_device_id __maybe_unused mp2891_of_match[] = {
	{.compatible = "mps,mp2891"},
	{}
};
MODULE_DEVICE_TABLE(of, mp2891_of_match);

static struct i2c_driver mp2891_driver = {
	.driver = {
		.name = "mp2891",
		.of_match_table = mp2891_of_match,
	},
	.probe = mp2891_probe,
	.id_table = mp2891_id,
};

module_i2c_driver(mp2891_driver);

MODULE_AUTHOR("Noah Wang <noahwang.wang@outlook.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2891");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PMBUS);
