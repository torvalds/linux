// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS Multi-phase Digital VR Controllers(MP2869)
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include "pmbus.h"

/*
 * Vender specific registers, the register MFR_SVI3_IOUT_PRT(0x67),
 * READ_PIN_EST(0x94)and READ_IIN_EST(0x95) redefine the standard
 * PMBUS register. The MFR_VOUT_LOOP_CTRL(0x29) is used to identify
 * the vout scale and the MFR_SVI3_IOUT_PRT(0x67) is used to identify
 * the iout scale. The READ_PIN_EST(0x94) is used to read input power
 * per rail. The MP2891 does not have standard READ_IIN register(0x89),
 * the iin telemetry can be obtained through the vendor redefined
 * register READ_IIN_EST(0x95).
 */
#define MFR_SVI3_IOUT_PRT	0x67
#define MFR_READ_PIN_EST	0x94
#define MFR_READ_IIN_EST	0x95
#define MFR_TSNS_FLT_SET	0xBB

#define MP2869_VIN_OV_FAULT_GAIN	4
#define MP2869_READ_VOUT_DIV	1024
#define MP2869_READ_IOUT_DIV	32
#define MP2869_OVUV_LIMIT_SCALE	10
#define MP2869_OVUV_DELTA_SCALE	50
#define MP2869_TEMP_LIMIT_OFFSET	40
#define MP2869_IOUT_LIMIT_UINT	8
#define MP2869_POUT_OP_GAIN	2

#define MP2869_PAGE_NUM	2

#define MP2869_RAIL1_FUNC	(PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | \
							PMBUS_HAVE_IOUT | PMBUS_HAVE_POUT | \
							PMBUS_HAVE_TEMP | PMBUS_HAVE_PIN | \
							PMBUS_HAVE_IIN | \
							PMBUS_HAVE_STATUS_VOUT | \
							PMBUS_HAVE_STATUS_IOUT | \
							PMBUS_HAVE_STATUS_TEMP | \
							PMBUS_HAVE_STATUS_INPUT)

#define MP2869_RAIL2_FUNC	(PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT | \
							 PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP | \
							 PMBUS_HAVE_PIN | PMBUS_HAVE_IIN | \
							 PMBUS_HAVE_STATUS_VOUT | \
							 PMBUS_HAVE_STATUS_IOUT | \
							 PMBUS_HAVE_STATUS_TEMP | \
							 PMBUS_HAVE_STATUS_INPUT)

struct mp2869_data {
	struct pmbus_driver_info info;
	bool mfr_thwn_flt_en;
	int vout_scale[MP2869_PAGE_NUM];
	int iout_scale[MP2869_PAGE_NUM];
};

static const int mp2869_vout_sacle[8] = {6400, 5120, 2560, 2048, 1024,
										 4, 2, 1};
static const int mp2869_iout_sacle[8] = {32, 1, 2, 4, 8, 16, 32, 64};

#define to_mp2869_data(x)	container_of(x, struct mp2869_data, info)

static u16 mp2869_reg2data_linear11(u16 word)
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
mp2869_identify_thwn_flt(struct i2c_client *client, struct pmbus_driver_info *info,
			 int page)
{
	struct mp2869_data *data = to_mp2869_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_TSNS_FLT_SET);
	if (ret < 0)
		return ret;

	data->mfr_thwn_flt_en = FIELD_GET(GENMASK(13, 13), ret);

	return 0;
}

static int
mp2869_identify_vout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			   int page)
{
	struct mp2869_data *data = to_mp2869_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, PMBUS_VOUT_SCALE_LOOP);
	if (ret < 0)
		return ret;

	/*
	 * The output voltage is equal to the READ_VOUT(0x8B) register value multiply
	 * by vout_scale.
	 * Obtain vout scale from the register PMBUS_VOUT_SCALE_LOOP, bits 12-10
	 * PMBUS_VOUT_SCALE_LOOP[12:10]:
	 * 000b - 6.25mV/LSB, 001b - 5mV/LSB, 010b - 2.5mV/LSB, 011b - 2mV/LSB
	 * 100b - 1mV/Lsb, 101b - (1/256)mV/LSB, 110b - (1/512)mV/LSB,
	 * 111b - (1/1024)mV/LSB
	 */
	data->vout_scale[page] = mp2869_vout_sacle[FIELD_GET(GENMASK(12, 10), ret)];

	return 0;
}

static int
mp2869_identify_iout_scale(struct i2c_client *client, struct pmbus_driver_info *info,
			   int page)
{
	struct mp2869_data *data = to_mp2869_data(info);
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, page);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, MFR_SVI3_IOUT_PRT);
	if (ret < 0)
		return ret;

	/*
	 * The output current is equal to the READ_IOUT(0x8C) register value
	 * multiply by iout_scale.
	 * Obtain iout_scale from the register MFR_SVI3_IOUT_PRT[2:0].
	 * The value is selected as below:
	 * 000b - 1A/LSB, 001b - (1/32)A/LSB, 010b - (1/16)A/LSB,
	 * 011b - (1/8)A/LSB, 100b - (1/4)A/LSB, 101b - (1/2)A/LSB
	 * 110b - 1A/LSB, 111b - 2A/LSB
	 */
	data->iout_scale[page] = mp2869_iout_sacle[FIELD_GET(GENMASK(2, 0), ret)];

	return 0;
}

static int mp2869_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2869_data *data = to_mp2869_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VOUT_MODE:
		/*
		 * The calculation of vout in this driver is based on direct format.
		 * As a result, the format of vout is enforced to direct.
		 */
		ret = PB_VOUT_MODE_DIRECT;
		break;
	case PMBUS_STATUS_BYTE:
		/*
		 * If the tsns digital fault is enabled, the TEMPERATURE flag
		 * of PMBUS_STATUS_BYTE should come from STATUS_MFR_SPECIFIC
		 * register bit1.
		 */
		if (!data->mfr_thwn_flt_en)
			return -ENODATA;

		ret = pmbus_read_byte_data(client, page, reg);
		if (ret < 0)
			return ret;

		ret = (ret & ~GENMASK(2, 2)) |
			FIELD_PREP(GENMASK(2, 2),
				   FIELD_GET(GENMASK(1, 1),
					     pmbus_read_byte_data(client, page,
								  PMBUS_STATUS_MFR_SPECIFIC)));
		break;
	case PMBUS_STATUS_TEMPERATURE:
		/*
		 * If the tsns digital fault is enabled, the OT Fault and OT Warning
		 * flag of PMBUS_STATUS_TEMPERATURE should come from STATUS_MFR_SPECIFIC
		 * register bit1.
		 */
		if (!data->mfr_thwn_flt_en)
			return -ENODATA;

		ret = pmbus_read_byte_data(client, page, reg);
		if (ret < 0)
			return ret;

		ret = (ret & ~GENMASK(7, 6)) |
			FIELD_PREP(GENMASK(6, 6),
				   FIELD_GET(GENMASK(1, 1),
					     pmbus_read_byte_data(client, page,
								  PMBUS_STATUS_MFR_SPECIFIC))) |
			 FIELD_PREP(GENMASK(7, 7),
				    FIELD_GET(GENMASK(1, 1),
					      pmbus_read_byte_data(client, page,
								   PMBUS_STATUS_MFR_SPECIFIC)));
		break;
	default:
		ret = -ENODATA;
		break;
	}

	return ret;
}

static int mp2869_read_word_data(struct i2c_client *client, int page, int phase,
				 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2869_data *data = to_mp2869_data(info);
	int ret;

	switch (reg) {
	case PMBUS_STATUS_WORD:
		/*
		 * If the tsns digital fault is enabled, the OT Fault flag
		 * of PMBUS_STATUS_WORD should come from STATUS_MFR_SPECIFIC
		 * register bit1.
		 */
		if (!data->mfr_thwn_flt_en)
			return -ENODATA;

		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & ~GENMASK(2, 2)) |
			 FIELD_PREP(GENMASK(2, 2),
				    FIELD_GET(GENMASK(1, 1),
					      pmbus_read_byte_data(client, page,
								   PMBUS_STATUS_MFR_SPECIFIC)));
		break;
	case PMBUS_READ_VIN:
		/*
		 * The MP2869 PMBUS_READ_VIN[10:0] is the vin value, the vin scale is
		 * 31.25mV/LSB. And the vin scale is set to 31.25mV/Lsb(using r/m/b scale)
		 * in MP2869 pmbus_driver_info struct, so the word data bit0-bit10 can be
		 * returned to pmbus core directly.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(10, 0), ret);
		break;
	case PMBUS_READ_IIN:
		/*
		 * The MP2869 redefine the standard 0x95 register as iin telemetry
		 * per rail.
		 */
		ret = pmbus_read_word_data(client, page, phase, MFR_READ_IIN_EST);
		if (ret < 0)
			return ret;

		break;
	case PMBUS_READ_PIN:
		/*
		 * The MP2869 redefine the standard 0x94 register as pin telemetry
		 * per rail. The MP2869 MFR_READ_PIN_EST register is linear11 format,
		 * but the pin scale is set to 1W/Lsb(using r/m/b scale). As a result,
		 * the pin read from MP2869 should be converted to W, then return
		 * the result to pmbus core.
		 */
		ret = pmbus_read_word_data(client, page, phase, MFR_READ_PIN_EST);
		if (ret < 0)
			return ret;

		ret = mp2869_reg2data_linear11(ret);
		break;
	case PMBUS_READ_VOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret &  GENMASK(11, 0)) * data->vout_scale[page],
					MP2869_READ_VOUT_DIV);
		break;
	case PMBUS_READ_IOUT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(10, 0)) * data->iout_scale[page],
					MP2869_READ_IOUT_DIV);
		break;
	case PMBUS_READ_POUT:
		/*
		 * The MP2869 PMBUS_READ_POUT register is linear11 format, but the pout
		 * scale is set to 1W/Lsb(using r/m/b scale). As a result, the pout read
		 * from MP2869 should be converted to W, then return the result to pmbus
		 * core.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = mp2869_reg2data_linear11(ret);
		break;
	case PMBUS_READ_TEMPERATURE_1:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(10, 0), ret);
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(12, 9), ret))
			ret = FIELD_GET(GENMASK(8, 0), ret) * MP2869_OVUV_LIMIT_SCALE +
				(FIELD_GET(GENMASK(12, 9), ret) + 1) * MP2869_OVUV_DELTA_SCALE;
		else
			ret = FIELD_GET(GENMASK(8, 0), ret) * MP2869_OVUV_LIMIT_SCALE;
		break;
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(12, 9), ret))
			ret = FIELD_GET(GENMASK(8, 0), ret) * MP2869_OVUV_LIMIT_SCALE -
				(FIELD_GET(GENMASK(12, 9), ret) + 1) * MP2869_OVUV_DELTA_SCALE;
		else
			ret = FIELD_GET(GENMASK(8, 0), ret) * MP2869_OVUV_LIMIT_SCALE;
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * The scale of MP2869 PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT
		 * is 1°C/LSB and they have 40°C offset.
		 */
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(7, 0)) - MP2869_TEMP_LIMIT_OFFSET;
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(7, 0)) * MP2869_VIN_OV_FAULT_GAIN;
		break;
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = FIELD_GET(GENMASK(9, 0), ret);
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = DIV_ROUND_CLOSEST((ret & GENMASK(7, 0)) * data->iout_scale[page] *
						MP2869_IOUT_LIMIT_UINT, MP2869_READ_IOUT_DIV);
		break;
	case PMBUS_POUT_OP_WARN_LIMIT:
		ret = pmbus_read_word_data(client, page, phase, reg);
		if (ret < 0)
			return ret;

		ret = (ret & GENMASK(7, 0)) * MP2869_POUT_OP_GAIN;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp2869_write_word_data(struct i2c_client *client, int page, int reg,
				  u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2869_data *data = to_mp2869_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VOUT_UV_FAULT_LIMIT:
		/*
		 * The MP2869 PMBUS_VOUT_UV_FAULT_LIMIT[8:0] is the limit value,
		 * and bit9-bit15 should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(12, 9), ret))
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(8, 0)) |
				FIELD_PREP(GENMASK(8, 0),
					   DIV_ROUND_CLOSEST(word +
						(FIELD_GET(GENMASK(12, 9),
						ret) + 1) *
					MP2869_OVUV_DELTA_SCALE,
					MP2869_OVUV_LIMIT_SCALE)));
		else
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(8, 0)) |
					FIELD_PREP(GENMASK(8, 0),
						   DIV_ROUND_CLOSEST(word,
								     MP2869_OVUV_LIMIT_SCALE)));
		break;
	case PMBUS_VOUT_OV_FAULT_LIMIT:
		/*
		 * The MP2869 PMBUS_VOUT_OV_FAULT_LIMIT[8:0] is the limit value,
		 * and bit9-bit15 should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		if (FIELD_GET(GENMASK(12, 9), ret))
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(8, 0)) |
				FIELD_PREP(GENMASK(8, 0),
					   DIV_ROUND_CLOSEST(word -
							(FIELD_GET(GENMASK(12, 9),
							ret) + 1) *
						MP2869_OVUV_DELTA_SCALE,
						MP2869_OVUV_LIMIT_SCALE)));
		else
			ret = pmbus_write_word_data(client, page, reg,
						    (ret & ~GENMASK(8, 0)) |
				FIELD_PREP(GENMASK(8, 0),
					   DIV_ROUND_CLOSEST(word,
							     MP2869_OVUV_LIMIT_SCALE)));
		break;
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_OT_WARN_LIMIT:
		/*
		 * If the tsns digital fault is enabled, the PMBUS_OT_FAULT_LIMIT and
		 * PMBUS_OT_WARN_LIMIT can not be written.
		 */
		if (data->mfr_thwn_flt_en)
			return -EINVAL;

		/*
		 * The MP2869 scale of MP2869 PMBUS_OT_FAULT_LIMIT and PMBUS_OT_WARN_LIMIT
		 * have 40°C offset. The bit0-bit7 is the limit value, and bit8-bit15
		 * should not be changed.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(7, 0)) |
					 FIELD_PREP(GENMASK(7, 0),
						    word + MP2869_TEMP_LIMIT_OFFSET));
		break;
	case PMBUS_VIN_OV_FAULT_LIMIT:
		/*
		 * The MP2869 PMBUS_VIN_OV_FAULT_LIMIT[7:0] is the limit value, and bit8-bit15
		 * should not be changed. The scale of PMBUS_VIN_OV_FAULT_LIMIT is 125mV/Lsb,
		 * but the vin scale is set to 31.25mV/Lsb(using r/m/b scale), so the word data
		 * should divide by MP2869_VIN_OV_FAULT_GAIN(4)
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(7, 0)) |
					FIELD_PREP(GENMASK(7, 0),
						   DIV_ROUND_CLOSEST(word,
								     MP2869_VIN_OV_FAULT_GAIN)));
		break;
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
		/*
		 * The PMBUS_VIN_UV_LIMIT[9:0] is the limit value, and bit10-bit15 should
		 * not be changed. The scale of PMBUS_VIN_UV_LIMIT is 31.25mV/Lsb, and the
		 * vin scale is set to 31.25mV/Lsb(using r/m/b scale), so the word data can
		 * be written directly.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(9, 0)) |
						FIELD_PREP(GENMASK(9, 0),
							   word));
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
		ret = pmbus_write_word_data(client, page, reg,
					    DIV_ROUND_CLOSEST(word * MP2869_READ_IOUT_DIV,
							      MP2869_IOUT_LIMIT_UINT *
								  data->iout_scale[page]));
		break;
	case PMBUS_POUT_OP_WARN_LIMIT:
		/*
		 * The POUT_OP_WARN_LIMIT[11:0] is the limit value, and bit12-bit15 should
		 * not be changed. The scale of POUT_OP_WARN_LIMIT is 2W/Lsb.
		 */
		ret = pmbus_read_word_data(client, page, 0xff, reg);
		if (ret < 0)
			return ret;

		ret = pmbus_write_word_data(client, page, reg,
					    (ret & ~GENMASK(11, 0)) |
					FIELD_PREP(GENMASK(11, 0),
						   DIV_ROUND_CLOSEST(word,
								     MP2869_POUT_OP_GAIN)));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mp2869_identify(struct i2c_client *client, struct pmbus_driver_info *info)
{
	int ret;

	/* Identify whether tsns digital fault is enable */
	ret = mp2869_identify_thwn_flt(client, info, 1);
	if (ret < 0)
		return 0;

	/* Identify vout scale for rail1. */
	ret = mp2869_identify_vout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	/* Identify vout scale for rail2. */
	ret = mp2869_identify_vout_scale(client, info, 1);
	if (ret < 0)
		return ret;

	/* Identify iout scale for rail 1. */
	ret = mp2869_identify_iout_scale(client, info, 0);
	if (ret < 0)
		return ret;

	/* Identify iout scale for rail 2. */
	return mp2869_identify_iout_scale(client, info, 1);
}

static const struct pmbus_driver_info mp2869_info = {
	.pages = MP2869_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,

	.m[PSC_VOLTAGE_IN] = 32,
	.R[PSC_VOLTAGE_IN] = 0,
	.b[PSC_VOLTAGE_IN] = 0,

	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.b[PSC_VOLTAGE_OUT] = 0,

	.m[PSC_CURRENT_OUT] = 1,
	.R[PSC_CURRENT_OUT] = 0,
	.b[PSC_CURRENT_OUT] = 0,

	.m[PSC_TEMPERATURE] = 1,
	.R[PSC_TEMPERATURE] = 0,
	.b[PSC_TEMPERATURE] = 0,

	.m[PSC_POWER] = 1,
	.R[PSC_POWER] = 0,
	.b[PSC_POWER] = 0,

	.func[0] = MP2869_RAIL1_FUNC,
	.func[1] = MP2869_RAIL2_FUNC,
	.read_word_data = mp2869_read_word_data,
	.write_word_data = mp2869_write_word_data,
	.read_byte_data = mp2869_read_byte_data,
	.identify = mp2869_identify,
};

static int mp2869_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	struct mp2869_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct mp2869_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy(&data->info, &mp2869_info, sizeof(*info));
	info = &data->info;

	return pmbus_do_probe(client, info);
}

static const struct i2c_device_id mp2869_id[] = {
	{"mp2869", 0},
	{"mp29608", 1},
	{"mp29612", 2},
	{"mp29816", 3},
	{}
};
MODULE_DEVICE_TABLE(i2c, mp2869_id);

static const struct of_device_id __maybe_unused mp2869_of_match[] = {
	{.compatible = "mps,mp2869", .data = (void *)0},
	{.compatible = "mps,mp29608", .data = (void *)1},
	{.compatible = "mps,mp29612", .data = (void *)2},
	{.compatible = "mps,mp29816", .data = (void *)3},
	{}
};
MODULE_DEVICE_TABLE(of, mp2869_of_match);

static struct i2c_driver mp2869_driver = {
	.driver = {
		.name = "mp2869",
		.of_match_table = mp2869_of_match,
	},
	.probe = mp2869_probe,
	.id_table = mp2869_id,
};

module_i2c_driver(mp2869_driver);

MODULE_AUTHOR("Wensheng Wang <wenswang@yeah.net>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2869");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
