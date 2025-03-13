// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for MPS2856/2857
 * Monolithic Power Systems VR Controllers
 *
 * Copyright (C) 2023 Quanta Computer lnc.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pmbus.h>
#include "pmbus.h"

/* Vendor specific registers. */
#define MP2856_MFR_VR_MULTI_CONFIG_R1	0x0d
#define MP2856_MFR_VR_MULTI_CONFIG_R2	0x1d

#define MP2856_MUL1_BOOT_SR_R2		0x10
#define MP2856_VR_ACTIVE		BIT(15)

#define MP2856_MFR_VR_CONFIG2		0x5e
#define MP2856_VOUT_MODE		BIT(11)

#define MP2856_MFR_VR_CONFIG1		0x68
#define MP2856_DRMOS_KCS		GENMASK(13, 12)

#define MP2856_MFR_READ_CS1_2_R1	0x82
#define MP2856_MFR_READ_CS3_4_R1	0x83
#define MP2856_MFR_READ_CS5_6_R1	0x84
#define MP2856_MFR_READ_CS7_8_R1	0x85
#define MP2856_MFR_READ_CS9_10_R1	0x86
#define MP2856_MFR_READ_CS11_12_R1	0x87

#define MP2856_MFR_READ_CS1_2_R2	0x85
#define MP2856_MFR_READ_CS3_4_R2	0x86
#define MP2856_MFR_READ_CS5_6_R2	0x87

#define MP2856_MAX_PHASE_RAIL1		8
#define MP2856_MAX_PHASE_RAIL2		4

#define MP2857_MAX_PHASE_RAIL1		12
#define MP2857_MAX_PHASE_RAIL2		4

#define MP2856_PAGE_NUM			2

enum chips { mp2856, mp2857 };

static const int mp2856_max_phases[][MP2856_PAGE_NUM] = {
	[mp2856] = { MP2856_MAX_PHASE_RAIL1, MP2856_MAX_PHASE_RAIL2 },
	[mp2857] = { MP2857_MAX_PHASE_RAIL1, MP2857_MAX_PHASE_RAIL2 },
};

static const struct i2c_device_id mp2856_id[] = {
	{"mp2856", mp2856},
	{"mp2857", mp2857},
	{}
};

MODULE_DEVICE_TABLE(i2c, mp2856_id);

struct mp2856_data {
	struct pmbus_driver_info info;
	int vout_format[MP2856_PAGE_NUM];
	int curr_sense_gain[MP2856_PAGE_NUM];
	int max_phases[MP2856_PAGE_NUM];
};

#define to_mp2856_data(x)	container_of(x, struct mp2856_data, info)

#define MAX_LIN_MANTISSA	(1023 * 1000)
#define MIN_LIN_MANTISSA	(511 * 1000)

static u16 val2linear11(s64 val)
{
	s16 exponent = 0, mantissa;
	bool negative = false;

	if (val == 0)
		return 0;

	if (val < 0) {
		negative = true;
		val = -val;
	}

	/* Reduce large mantissa until it fits into 10 bit */
	while (val >= MAX_LIN_MANTISSA && exponent < 15) {
		exponent++;
		val >>= 1;
	}
	/* Increase small mantissa to improve precision */
	while (val < MIN_LIN_MANTISSA && exponent > -15) {
		exponent--;
		val <<= 1;
	}

	/* Convert mantissa from milli-units to units */
	mantissa = clamp_val(DIV_ROUND_CLOSEST_ULL(val, 1000), 0, 0x3ff);

	/* restore sign */
	if (negative)
		mantissa = -mantissa;

	/* Convert to 5 bit exponent, 11 bit mantissa */
	return (mantissa & 0x7ff) | ((exponent << 11) & 0xf800);
}

static int
mp2856_read_word_helper(struct i2c_client *client, int page, int phase, u8 reg,
			u16 mask)
{
	int ret = pmbus_read_word_data(client, page, phase, reg);

	return (ret > 0) ? ret & mask : ret;
}

static int
mp2856_read_vout(struct i2c_client *client, struct mp2856_data *data, int page,
		 int phase, u8 reg)
{
	int ret;

	ret = mp2856_read_word_helper(client, page, phase, reg,
				      GENMASK(9, 0));
	if (ret < 0)
		return ret;

	/* convert vout result to direct format */
	ret = (data->vout_format[page] == vid) ?
		((ret + 49) * 5) : ((ret * 1000) >> 8);

	return ret;
}

static int
mp2856_read_phase(struct i2c_client *client, struct mp2856_data *data,
		  int page, int phase, u8 reg)
{
	int ret;
	int val;

	ret = pmbus_read_word_data(client, page, phase, reg);
	if (ret < 0)
		return ret;

	if (!((phase + 1) % MP2856_PAGE_NUM))
		ret >>= 8;
	ret &= 0xff;

	/*
	 * Output value is calculated as: (READ_CSx * 12.5mV - 1.23V) / (Kcs * Rcs)
	 */
	val = (ret * 125) - 12300;

	return val2linear11(val);
}

static int
mp2856_read_phases(struct i2c_client *client, struct mp2856_data *data,
		   int page, int phase)
{
	int ret;

	if (page == 0) {
		switch (phase) {
		case 0 ... 1:
			ret = mp2856_read_phase(client, data, page, phase,
						MP2856_MFR_READ_CS1_2_R1);
			break;
		case 2 ... 3:
			ret = mp2856_read_phase(client, data, page, phase,
						MP2856_MFR_READ_CS3_4_R1);
			break;
		case 4 ... 5:
			ret = mp2856_read_phase(client, data, page, phase,
						MP2856_MFR_READ_CS5_6_R1);
			break;
		case 6 ... 7:
			ret = mp2856_read_phase(client, data, page, phase,
						MP2856_MFR_READ_CS7_8_R1);
			break;
		default:
			return -ENODATA;
		}
	} else {
		switch (phase) {
		case 0 ... 1:
			ret = mp2856_read_phase(client, data, page, phase,
						MP2856_MFR_READ_CS1_2_R2);
			break;
		case 2 ... 3:
			ret = mp2856_read_phase(client, data, page, phase,
						MP2856_MFR_READ_CS1_2_R2);
			break;
		default:
			return -ENODATA;
		}
	}
	return ret;
}

static int
mp2856_read_word_data(struct i2c_client *client, int page,
		      int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct mp2856_data *data = to_mp2856_data(info);
	int ret;

	switch (reg) {
	case PMBUS_READ_VOUT:
		ret = mp2856_read_vout(client, data, page, phase, reg);
		break;
	case PMBUS_READ_IOUT:
		if (phase != 0xff)
			ret = mp2856_read_phases(client, data, page, phase);
		else
			ret = pmbus_read_word_data(client, page, phase, reg);
		break;
	default:
		return -ENODATA;
	}

	return ret;
}

static int
mp2856_read_byte_data(struct i2c_client *client, int page, int reg)
{
	switch (reg) {
	case PMBUS_VOUT_MODE:
		/* Enforce VOUT direct format. */
		return PB_VOUT_MODE_DIRECT;
	default:
		return -ENODATA;
	}
}

static int
mp2856_identify_multiphase(struct i2c_client *client, u8 reg, u8 max_phase,
			   u16 mask)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	ret &= mask;
	return (ret >= max_phase) ? max_phase : ret;
}

static int
mp2856_identify_multiphase_rail1(struct i2c_client *client,
				 struct mp2856_data *data)
{
	int ret, i;

	ret = mp2856_identify_multiphase(client, MP2856_MFR_VR_MULTI_CONFIG_R1,
					 MP2856_MAX_PHASE_RAIL1, GENMASK(3, 0));
	if (ret < 0)
		return ret;

	data->info.phases[0] = (ret > data->max_phases[0]) ?
				data->max_phases[0] : ret;

	for (i = 0 ; i < data->info.phases[0]; i++)
		data->info.pfunc[i] |= PMBUS_HAVE_IOUT;

	return 0;
}

static int
mp2856_identify_multiphase_rail2(struct i2c_client *client,
				 struct mp2856_data *data)
{
	int ret, i;

	ret = mp2856_identify_multiphase(client, MP2856_MFR_VR_MULTI_CONFIG_R2,
					 MP2856_MAX_PHASE_RAIL2, GENMASK(2, 0));
	if (ret < 0)
		return ret;

	data->info.phases[1] = (ret > data->max_phases[1]) ?
				data->max_phases[1] : ret;

	for (i = 0 ; i < data->info.phases[0]; i++)
		data->info.pfunc[i] |= PMBUS_HAVE_IOUT;

	return 0;
}

static int
mp2856_current_sense_gain_get(struct i2c_client *client,
			      struct mp2856_data *data)
{
	int i, ret;

	/*
	 * Obtain DrMOS current sense gain of power stage from the register
	 * MP2856_MFR_VR_CONFIG1, bits 13-12. The value is selected as below:
	 * 00b - 5µA/A, 01b - 8.5µA/A, 10b - 9.7µA/A, 11b - 10µA/A. Other
	 * values are invalid.
	 */
	for (i = 0 ; i < data->info.pages; i++) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_read_word_data(client,
					       MP2856_MFR_VR_CONFIG1);
		if (ret < 0)
			return ret;

		switch ((ret & MP2856_DRMOS_KCS) >> 12) {
		case 0:
			data->curr_sense_gain[i] = 50;
			break;
		case 1:
			data->curr_sense_gain[i] = 85;
			break;
		case 2:
			data->curr_sense_gain[i] = 97;
			break;
		default:
			data->curr_sense_gain[i] = 100;
			break;
		}
	}
	return 0;
}

static int
mp2856_identify_vout_format(struct i2c_client *client,
			    struct mp2856_data *data)
{
	int i, ret;

	for (i = 0; i < data->info.pages; i++) {
		ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, i);
		if (ret < 0)
			return ret;

		ret = i2c_smbus_read_word_data(client, MP2856_MFR_VR_CONFIG2);
		if (ret < 0)
			return ret;

		data->vout_format[i] = (ret & MP2856_VOUT_MODE) ? linear : vid;
	}
	return 0;
}

static bool
mp2856_is_rail2_active(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PMBUS_PAGE, 2);
	if (ret < 0)
		return true;

	ret = i2c_smbus_read_word_data(client, MP2856_MUL1_BOOT_SR_R2);
	if (ret < 0)
		return true;

	return (ret & MP2856_VR_ACTIVE) ? true : false;
}

static struct pmbus_driver_info mp2856_info = {
	.pages = MP2856_PAGE_NUM,
	.format[PSC_VOLTAGE_IN] = linear,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_TEMPERATURE] = linear,
	.format[PSC_CURRENT_IN] = linear,
	.format[PSC_CURRENT_OUT] = linear,
	.format[PSC_POWER] = linear,
	.m[PSC_VOLTAGE_OUT] = 1,
	.R[PSC_VOLTAGE_OUT] = 3,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
		PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
		PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_POUT |
		PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT,
	.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_IOUT |
		PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP,
	.read_byte_data = mp2856_read_byte_data,
	.read_word_data = mp2856_read_word_data,
};

static int mp2856_probe(struct i2c_client *client)
{
	struct pmbus_driver_info *info;
	struct mp2856_data *data;
	enum chips chip_id;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(struct mp2856_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip_id = (kernel_ulong_t)i2c_get_match_data(client);

	memcpy(data->max_phases, mp2856_max_phases[chip_id],
	       sizeof(data->max_phases));

	memcpy(&data->info, &mp2856_info, sizeof(*info));
	info = &data->info;

	/* Identify multiphase configuration. */
	ret = mp2856_identify_multiphase_rail1(client, data);
	if (ret < 0)
		return ret;

	if (mp2856_is_rail2_active(client)) {
		ret = mp2856_identify_multiphase_rail2(client, data);
		if (ret < 0)
			return ret;
	} else {
		/* rail2 is not active */
		info->pages = 1;
	}

	/* Obtain current sense gain of power stage. */
	ret = mp2856_current_sense_gain_get(client, data);
	if (ret)
		return ret;

	/* Identify vout format. */
	ret = mp2856_identify_vout_format(client, data);
	if (ret)
		return ret;

	/* set the device to page 0 */
	i2c_smbus_write_byte_data(client, PMBUS_PAGE, 0);

	return pmbus_do_probe(client, info);
}

static const struct of_device_id __maybe_unused mp2856_of_match[] = {
	{.compatible = "mps,mp2856", .data = (void *)mp2856},
	{.compatible = "mps,mp2857", .data = (void *)mp2857},
	{}
};
MODULE_DEVICE_TABLE(of, mp2856_of_match);

static struct i2c_driver mp2856_driver = {
	.driver = {
		.name = "mp2856",
		.of_match_table = mp2856_of_match,
	},
	.probe = mp2856_probe,
	.id_table = mp2856_id,
};

module_i2c_driver(mp2856_driver);

MODULE_AUTHOR("Peter Yin <peter.yin@quantatw.com>");
MODULE_DESCRIPTION("PMBus driver for MPS MP2856/MP2857 device");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PMBUS");
