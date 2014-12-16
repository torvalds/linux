/*
 * Hardware monitoring driver for LTC2974, LTC2977, LTC2978, LTC3880,
 * LTC3883, and LTM4676
 *
 * Copyright (c) 2011 Ericsson AB.
 * Copyright (c) 2013, 2014 Guenter Roeck
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

enum chips { ltc2974, ltc2977, ltc2978, ltc3880, ltc3883, ltm4676 };

/* Common for all chips */
#define LTC2978_MFR_VOUT_PEAK		0xdd
#define LTC2978_MFR_VIN_PEAK		0xde
#define LTC2978_MFR_TEMPERATURE_PEAK	0xdf
#define LTC2978_MFR_SPECIAL_ID		0xe7

/* LTC2974, LCT2977, and LTC2978 */
#define LTC2978_MFR_VOUT_MIN		0xfb
#define LTC2978_MFR_VIN_MIN		0xfc
#define LTC2978_MFR_TEMPERATURE_MIN	0xfd

/* LTC2974 only */
#define LTC2974_MFR_IOUT_PEAK		0xd7
#define LTC2974_MFR_IOUT_MIN		0xd8

/* LTC3880, LTC3883, and LTM4676 */
#define LTC3880_MFR_IOUT_PEAK		0xd7
#define LTC3880_MFR_CLEAR_PEAKS		0xe3
#define LTC3880_MFR_TEMPERATURE2_PEAK	0xf4

/* LTC3883 only */
#define LTC3883_MFR_IIN_PEAK		0xe1

#define LTC2974_ID_REV1			0x0212
#define LTC2974_ID_REV2			0x0213
#define LTC2977_ID			0x0130
#define LTC2978_ID_REV1			0x0121
#define LTC2978_ID_REV2			0x0122
#define LTC2978A_ID			0x0124
#define LTC3880_ID			0x4000
#define LTC3880_ID_MASK			0xff00
#define LTC3883_ID			0x4300
#define LTC3883_ID_MASK			0xff00
#define LTM4676_ID			0x4480	/* datasheet claims 0x440X */
#define LTM4676_ID_MASK			0xfff0

#define LTC2974_NUM_PAGES		4
#define LTC2978_NUM_PAGES		8
#define LTC3880_NUM_PAGES		2
#define LTC3883_NUM_PAGES		1

/*
 * LTC2978 clears peak data whenever the CLEAR_FAULTS command is executed, which
 * happens pretty much each time chip data is updated. Raw peak data therefore
 * does not provide much value. To be able to provide useful peak data, keep an
 * internal cache of measured peak data, which is only cleared if an explicit
 * "clear peak" command is executed for the sensor in question.
 */

struct ltc2978_data {
	enum chips id;
	u16 vin_min, vin_max;
	u16 temp_min[LTC2974_NUM_PAGES], temp_max[LTC2974_NUM_PAGES];
	u16 vout_min[LTC2978_NUM_PAGES], vout_max[LTC2978_NUM_PAGES];
	u16 iout_min[LTC2974_NUM_PAGES], iout_max[LTC2974_NUM_PAGES];
	u16 iin_max;
	u16 temp2_max;
	struct pmbus_driver_info info;
};

#define to_ltc2978_data(x)  container_of(x, struct ltc2978_data, info)

static inline int lin11_to_val(int data)
{
	s16 e = ((s16)data) >> 11;
	s32 m = (((s16)(data << 5)) >> 5);

	/*
	 * mantissa is 10 bit + sign, exponent adds up to 15 bit.
	 * Add 6 bit to exponent for maximum accuracy (10 + 15 + 6 = 31).
	 */
	e += 6;
	return (e < 0 ? m >> -e : m << e);
}

static int ltc2978_read_word_data_common(struct i2c_client *client, int page,
					 int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VIN_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret) > lin11_to_val(data->vin_max))
				data->vin_max = ret;
			ret = data->vin_max;
		}
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VOUT_PEAK);
		if (ret >= 0) {
			/*
			 * VOUT is 16 bit unsigned with fixed exponent,
			 * so we can compare it directly
			 */
			if (ret > data->vout_max[page])
				data->vout_max[page] = ret;
			ret = data->vout_max[page];
		}
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		ret = pmbus_read_word_data(client, page,
					   LTC2978_MFR_TEMPERATURE_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    > lin11_to_val(data->temp_max[page]))
				data->temp_max[page] = ret;
			ret = data->temp_max[page];
		}
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_VIN_HISTORY:
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = 0;
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int ltc2978_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_VIN_MIN:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VIN_MIN);
		if (ret >= 0) {
			if (lin11_to_val(ret) < lin11_to_val(data->vin_min))
				data->vin_min = ret;
			ret = data->vin_min;
		}
		break;
	case PMBUS_VIRT_READ_VOUT_MIN:
		ret = pmbus_read_word_data(client, page, LTC2978_MFR_VOUT_MIN);
		if (ret >= 0) {
			/*
			 * VOUT_MIN is known to not be supported on some lots
			 * of LTC2978 revision 1, and will return the maximum
			 * possible voltage if read. If VOUT_MAX is valid and
			 * lower than the reading of VOUT_MIN, use it instead.
			 */
			if (data->vout_max[page] && ret > data->vout_max[page])
				ret = data->vout_max[page];
			if (ret < data->vout_min[page])
				data->vout_min[page] = ret;
			ret = data->vout_min[page];
		}
		break;
	case PMBUS_VIRT_READ_TEMP_MIN:
		ret = pmbus_read_word_data(client, page,
					   LTC2978_MFR_TEMPERATURE_MIN);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    < lin11_to_val(data->temp_min[page]))
				data->temp_min[page] = ret;
			ret = data->temp_min[page];
		}
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_READ_TEMP2_MAX:
	case PMBUS_VIRT_RESET_TEMP2_HISTORY:
		ret = -ENXIO;
		break;
	default:
		ret = ltc2978_read_word_data_common(client, page, reg);
		break;
	}
	return ret;
}

static int ltc2974_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, LTC2974_MFR_IOUT_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    > lin11_to_val(data->iout_max[page]))
				data->iout_max[page] = ret;
			ret = data->iout_max[page];
		}
		break;
	case PMBUS_VIRT_READ_IOUT_MIN:
		ret = pmbus_read_word_data(client, page, LTC2974_MFR_IOUT_MIN);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    < lin11_to_val(data->iout_min[page]))
				data->iout_min[page] = ret;
			ret = data->iout_min[page];
		}
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
		ret = 0;
		break;
	default:
		ret = ltc2978_read_word_data(client, page, reg);
		break;
	}
	return ret;
}

static int ltc3880_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, page, LTC3880_MFR_IOUT_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    > lin11_to_val(data->iout_max[page]))
				data->iout_max[page] = ret;
			ret = data->iout_max[page];
		}
		break;
	case PMBUS_VIRT_READ_TEMP2_MAX:
		ret = pmbus_read_word_data(client, page,
					   LTC3880_MFR_TEMPERATURE2_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret) > lin11_to_val(data->temp2_max))
				data->temp2_max = ret;
			ret = data->temp2_max;
		}
		break;
	case PMBUS_VIRT_READ_VIN_MIN:
	case PMBUS_VIRT_READ_VOUT_MIN:
	case PMBUS_VIRT_READ_TEMP_MIN:
		ret = -ENXIO;
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_TEMP2_HISTORY:
		ret = 0;
		break;
	default:
		ret = ltc2978_read_word_data_common(client, page, reg);
		break;
	}
	return ret;
}

static int ltc3883_read_word_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_READ_IIN_MAX:
		ret = pmbus_read_word_data(client, page, LTC3883_MFR_IIN_PEAK);
		if (ret >= 0) {
			if (lin11_to_val(ret)
			    > lin11_to_val(data->iin_max))
				data->iin_max = ret;
			ret = data->iin_max;
		}
		break;
	case PMBUS_VIRT_RESET_IIN_HISTORY:
		ret = 0;
		break;
	default:
		ret = ltc3880_read_word_data(client, page, reg);
		break;
	}
	return ret;
}

static int ltc2978_clear_peaks(struct i2c_client *client, int page,
			       enum chips id)
{
	int ret;

	if (id == ltc3880 || id == ltc3883)
		ret = pmbus_write_byte(client, 0, LTC3880_MFR_CLEAR_PEAKS);
	else
		ret = pmbus_write_byte(client, page, PMBUS_CLEAR_FAULTS);

	return ret;
}

static int ltc2978_write_word_data(struct i2c_client *client, int page,
				    int reg, u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct ltc2978_data *data = to_ltc2978_data(info);
	int ret;

	switch (reg) {
	case PMBUS_VIRT_RESET_IIN_HISTORY:
		data->iin_max = 0x7c00;
		ret = ltc2978_clear_peaks(client, page, data->id);
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
		data->iout_max[page] = 0x7c00;
		data->iout_min[page] = 0xfbff;
		ret = ltc2978_clear_peaks(client, page, data->id);
		break;
	case PMBUS_VIRT_RESET_TEMP2_HISTORY:
		data->temp2_max = 0x7c00;
		ret = ltc2978_clear_peaks(client, page, data->id);
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		data->vout_min[page] = 0xffff;
		data->vout_max[page] = 0;
		ret = ltc2978_clear_peaks(client, page, data->id);
		break;
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		data->vin_min = 0x7bff;
		data->vin_max = 0x7c00;
		ret = ltc2978_clear_peaks(client, page, data->id);
		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		data->temp_min[page] = 0x7bff;
		data->temp_max[page] = 0x7c00;
		ret = ltc2978_clear_peaks(client, page, data->id);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static const struct i2c_device_id ltc2978_id[] = {
	{"ltc2974", ltc2974},
	{"ltc2977", ltc2977},
	{"ltc2978", ltc2978},
	{"ltc3880", ltc3880},
	{"ltc3883", ltc3883},
	{"ltm4676", ltm4676},
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2978_id);

#if IS_ENABLED(CONFIG_SENSORS_LTC2978_REGULATOR)
static const struct regulator_desc ltc2978_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
	PMBUS_REGULATOR("vout", 1),
	PMBUS_REGULATOR("vout", 2),
	PMBUS_REGULATOR("vout", 3),
	PMBUS_REGULATOR("vout", 4),
	PMBUS_REGULATOR("vout", 5),
	PMBUS_REGULATOR("vout", 6),
	PMBUS_REGULATOR("vout", 7),
};
#endif /* CONFIG_SENSORS_LTC2978_REGULATOR */

static int ltc2978_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int chip_id, i;
	struct ltc2978_data *data;
	struct pmbus_driver_info *info;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct ltc2978_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip_id = i2c_smbus_read_word_data(client, LTC2978_MFR_SPECIAL_ID);
	if (chip_id < 0)
		return chip_id;

	if (chip_id == LTC2974_ID_REV1 || chip_id == LTC2974_ID_REV2) {
		data->id = ltc2974;
	} else if (chip_id == LTC2977_ID) {
		data->id = ltc2977;
	} else if (chip_id == LTC2978_ID_REV1 || chip_id == LTC2978_ID_REV2 ||
		   chip_id == LTC2978A_ID) {
		data->id = ltc2978;
	} else if ((chip_id & LTC3880_ID_MASK) == LTC3880_ID) {
		data->id = ltc3880;
	} else if ((chip_id & LTC3883_ID_MASK) == LTC3883_ID) {
		data->id = ltc3883;
	} else if ((chip_id & LTM4676_ID_MASK) == LTM4676_ID) {
		data->id = ltm4676;
	} else {
		dev_err(&client->dev, "Unsupported chip ID 0x%x\n", chip_id);
		return -ENODEV;
	}
	if (data->id != id->driver_data)
		dev_warn(&client->dev,
			 "Device mismatch: Configured %s, detected %s\n",
			 id->name,
			 ltc2978_id[data->id].name);

	info = &data->info;
	info->write_word_data = ltc2978_write_word_data;

	data->vin_min = 0x7bff;
	data->vin_max = 0x7c00;
	for (i = 0; i < ARRAY_SIZE(data->vout_min); i++)
		data->vout_min[i] = 0xffff;
	for (i = 0; i < ARRAY_SIZE(data->iout_min); i++)
		data->iout_min[i] = 0xfbff;
	for (i = 0; i < ARRAY_SIZE(data->iout_max); i++)
		data->iout_max[i] = 0x7c00;
	for (i = 0; i < ARRAY_SIZE(data->temp_min); i++)
		data->temp_min[i] = 0x7bff;
	for (i = 0; i < ARRAY_SIZE(data->temp_max); i++)
		data->temp_max[i] = 0x7c00;
	data->temp2_max = 0x7c00;

	switch (data->id) {
	case ltc2974:
		info->read_word_data = ltc2974_read_word_data;
		info->pages = LTC2974_NUM_PAGES;
		info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
		  | PMBUS_HAVE_TEMP2;
		for (i = 0; i < info->pages; i++) {
			info->func[i] |= PMBUS_HAVE_VOUT
			  | PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_POUT
			  | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
			  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT;
		}
		break;
	case ltc2977:
	case ltc2978:
		info->read_word_data = ltc2978_read_word_data;
		info->pages = LTC2978_NUM_PAGES;
		info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT
		  | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;
		for (i = 1; i < LTC2978_NUM_PAGES; i++) {
			info->func[i] = PMBUS_HAVE_VOUT
			  | PMBUS_HAVE_STATUS_VOUT;
		}
		break;
	case ltc3880:
	case ltm4676:
		info->read_word_data = ltc3880_read_word_data;
		info->pages = LTC3880_NUM_PAGES;
		info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN
		  | PMBUS_HAVE_STATUS_INPUT
		  | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		  | PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP
		  | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP;
		info->func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		  | PMBUS_HAVE_POUT
		  | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;
		break;
	case ltc3883:
		info->read_word_data = ltc3883_read_word_data;
		info->pages = LTC3883_NUM_PAGES;
		info->func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN
		  | PMBUS_HAVE_STATUS_INPUT
		  | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		  | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		  | PMBUS_HAVE_PIN | PMBUS_HAVE_POUT | PMBUS_HAVE_TEMP
		  | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP;
		break;
	default:
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_SENSORS_LTC2978_REGULATOR)
	info->num_regulators = info->pages;
	info->reg_desc = ltc2978_reg_desc;
	if (info->num_regulators > ARRAY_SIZE(ltc2978_reg_desc)) {
		dev_err(&client->dev, "num_regulators too large!");
		info->num_regulators = ARRAY_SIZE(ltc2978_reg_desc);
	}
#endif

	return pmbus_do_probe(client, id, info);
}

#ifdef CONFIG_OF
static const struct of_device_id ltc2978_of_match[] = {
	{ .compatible = "lltc,ltc2974" },
	{ .compatible = "lltc,ltc2977" },
	{ .compatible = "lltc,ltc2978" },
	{ .compatible = "lltc,ltc3880" },
	{ .compatible = "lltc,ltc3883" },
	{ .compatible = "lltc,ltm4676" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2978_of_match);
#endif

static struct i2c_driver ltc2978_driver = {
	.driver = {
		   .name = "ltc2978",
		   .of_match_table = of_match_ptr(ltc2978_of_match),
		   },
	.probe = ltc2978_probe,
	.remove = pmbus_do_remove,
	.id_table = ltc2978_id,
};

module_i2c_driver(ltc2978_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for LTC2974, LTC2978, LTC3880, LTC3883, and LTM4676");
MODULE_LICENSE("GPL");
