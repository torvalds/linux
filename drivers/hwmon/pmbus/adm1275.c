// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for Analog Devices ADM1275 Hot-Swap Controller
 * and Digital Power Monitor
 *
 * Copyright (c) 2011 Ericsson AB.
 * Copyright (c) 2018 Guenter Roeck
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/log2.h>
#include "pmbus.h"

enum chips { adm1075, adm1272, adm1275, adm1276, adm1278, adm1293, adm1294 };

#define ADM1275_MFR_STATUS_IOUT_WARN2	BIT(0)
#define ADM1293_MFR_STATUS_VAUX_UV_WARN	BIT(5)
#define ADM1293_MFR_STATUS_VAUX_OV_WARN	BIT(6)

#define ADM1275_PEAK_IOUT		0xd0
#define ADM1275_PEAK_VIN		0xd1
#define ADM1275_PEAK_VOUT		0xd2
#define ADM1275_PMON_CONFIG		0xd4

#define ADM1275_VIN_VOUT_SELECT		BIT(6)
#define ADM1275_VRANGE			BIT(5)
#define ADM1075_IRANGE_50		BIT(4)
#define ADM1075_IRANGE_25		BIT(3)
#define ADM1075_IRANGE_MASK		(BIT(3) | BIT(4))

#define ADM1272_IRANGE			BIT(0)

#define ADM1278_TSFILT			BIT(15)
#define ADM1278_TEMP1_EN		BIT(3)
#define ADM1278_VIN_EN			BIT(2)
#define ADM1278_VOUT_EN			BIT(1)

#define ADM1278_PMON_DEFCONFIG		(ADM1278_VOUT_EN | ADM1278_TEMP1_EN | ADM1278_TSFILT)

#define ADM1293_IRANGE_25		0
#define ADM1293_IRANGE_50		BIT(6)
#define ADM1293_IRANGE_100		BIT(7)
#define ADM1293_IRANGE_200		(BIT(6) | BIT(7))
#define ADM1293_IRANGE_MASK		(BIT(6) | BIT(7))

#define ADM1293_VIN_SEL_012		BIT(2)
#define ADM1293_VIN_SEL_074		BIT(3)
#define ADM1293_VIN_SEL_210		(BIT(2) | BIT(3))
#define ADM1293_VIN_SEL_MASK		(BIT(2) | BIT(3))

#define ADM1293_VAUX_EN			BIT(1)

#define ADM1278_PEAK_TEMP		0xd7
#define ADM1275_IOUT_WARN2_LIMIT	0xd7
#define ADM1275_DEVICE_CONFIG		0xd8

#define ADM1275_IOUT_WARN2_SELECT	BIT(4)

#define ADM1276_PEAK_PIN		0xda
#define ADM1075_READ_VAUX		0xdd
#define ADM1075_VAUX_OV_WARN_LIMIT	0xde
#define ADM1075_VAUX_UV_WARN_LIMIT	0xdf
#define ADM1293_IOUT_MIN		0xe3
#define ADM1293_PIN_MIN			0xe4
#define ADM1075_VAUX_STATUS		0xf6

#define ADM1075_VAUX_OV_WARN		BIT(7)
#define ADM1075_VAUX_UV_WARN		BIT(6)

#define ADM1275_VI_AVG_SHIFT		0
#define ADM1275_VI_AVG_MASK		GENMASK(ADM1275_VI_AVG_SHIFT + 2, \
						ADM1275_VI_AVG_SHIFT)
#define ADM1275_SAMPLES_AVG_MAX		128

#define ADM1278_PWR_AVG_SHIFT		11
#define ADM1278_PWR_AVG_MASK		GENMASK(ADM1278_PWR_AVG_SHIFT + 2, \
						ADM1278_PWR_AVG_SHIFT)
#define ADM1278_VI_AVG_SHIFT		8
#define ADM1278_VI_AVG_MASK		GENMASK(ADM1278_VI_AVG_SHIFT + 2, \
						ADM1278_VI_AVG_SHIFT)

struct adm1275_data {
	int id;
	bool have_oc_fault;
	bool have_uc_fault;
	bool have_vout;
	bool have_vaux_status;
	bool have_mfr_vaux_status;
	bool have_iout_min;
	bool have_pin_min;
	bool have_pin_max;
	bool have_temp_max;
	bool have_power_sampling;
	struct pmbus_driver_info info;
};

#define to_adm1275_data(x)  container_of(x, struct adm1275_data, info)

struct coefficients {
	s16 m;
	s16 b;
	s16 R;
};

static const struct coefficients adm1075_coefficients[] = {
	[0] = { 27169, 0, -1 },		/* voltage */
	[1] = { 806, 20475, -1 },	/* current, irange25 */
	[2] = { 404, 20475, -1 },	/* current, irange50 */
	[3] = { 8549, 0, -1 },		/* power, irange25 */
	[4] = { 4279, 0, -1 },		/* power, irange50 */
};

static const struct coefficients adm1272_coefficients[] = {
	[0] = { 6770, 0, -2 },		/* voltage, vrange 60V */
	[1] = { 4062, 0, -2 },		/* voltage, vrange 100V */
	[2] = { 1326, 20480, -1 },	/* current, vsense range 15mV */
	[3] = { 663, 20480, -1 },	/* current, vsense range 30mV */
	[4] = { 3512, 0, -2 },		/* power, vrange 60V, irange 15mV */
	[5] = { 21071, 0, -3 },		/* power, vrange 100V, irange 15mV */
	[6] = { 17561, 0, -3 },		/* power, vrange 60V, irange 30mV */
	[7] = { 10535, 0, -3 },		/* power, vrange 100V, irange 30mV */
	[8] = { 42, 31871, -1 },	/* temperature */

};

static const struct coefficients adm1275_coefficients[] = {
	[0] = { 19199, 0, -2 },		/* voltage, vrange set */
	[1] = { 6720, 0, -1 },		/* voltage, vrange not set */
	[2] = { 807, 20475, -1 },	/* current */
};

static const struct coefficients adm1276_coefficients[] = {
	[0] = { 19199, 0, -2 },		/* voltage, vrange set */
	[1] = { 6720, 0, -1 },		/* voltage, vrange not set */
	[2] = { 807, 20475, -1 },	/* current */
	[3] = { 6043, 0, -2 },		/* power, vrange set */
	[4] = { 2115, 0, -1 },		/* power, vrange not set */
};

static const struct coefficients adm1278_coefficients[] = {
	[0] = { 19599, 0, -2 },		/* voltage */
	[1] = { 800, 20475, -1 },	/* current */
	[2] = { 6123, 0, -2 },		/* power */
	[3] = { 42, 31880, -1 },	/* temperature */
};

static const struct coefficients adm1293_coefficients[] = {
	[0] = { 3333, -1, 0 },		/* voltage, vrange 1.2V */
	[1] = { 5552, -5, -1 },		/* voltage, vrange 7.4V */
	[2] = { 19604, -50, -2 },	/* voltage, vrange 21V */
	[3] = { 8000, -100, -2 },	/* current, irange25 */
	[4] = { 4000, -100, -2 },	/* current, irange50 */
	[5] = { 20000, -1000, -3 },	/* current, irange100 */
	[6] = { 10000, -1000, -3 },	/* current, irange200 */
	[7] = { 10417, 0, -1 },		/* power, 1.2V, irange25 */
	[8] = { 5208, 0, -1 },		/* power, 1.2V, irange50 */
	[9] = { 26042, 0, -2 },		/* power, 1.2V, irange100 */
	[10] = { 13021, 0, -2 },	/* power, 1.2V, irange200 */
	[11] = { 17351, 0, -2 },	/* power, 7.4V, irange25 */
	[12] = { 8676, 0, -2 },		/* power, 7.4V, irange50 */
	[13] = { 4338, 0, -2 },		/* power, 7.4V, irange100 */
	[14] = { 21689, 0, -3 },	/* power, 7.4V, irange200 */
	[15] = { 6126, 0, -2 },		/* power, 21V, irange25 */
	[16] = { 30631, 0, -3 },	/* power, 21V, irange50 */
	[17] = { 15316, 0, -3 },	/* power, 21V, irange100 */
	[18] = { 7658, 0, -3 },		/* power, 21V, irange200 */
};

static int adm1275_read_pmon_config(const struct adm1275_data *data,
				    struct i2c_client *client, bool is_power)
{
	int shift, ret;
	u16 mask;

	/*
	 * The PMON configuration register is a 16-bit register only on chips
	 * supporting power average sampling. On other chips it is an 8-bit
	 * register.
	 */
	if (data->have_power_sampling) {
		ret = i2c_smbus_read_word_data(client, ADM1275_PMON_CONFIG);
		mask = is_power ? ADM1278_PWR_AVG_MASK : ADM1278_VI_AVG_MASK;
		shift = is_power ? ADM1278_PWR_AVG_SHIFT : ADM1278_VI_AVG_SHIFT;
	} else {
		ret = i2c_smbus_read_byte_data(client, ADM1275_PMON_CONFIG);
		mask = ADM1275_VI_AVG_MASK;
		shift = ADM1275_VI_AVG_SHIFT;
	}
	if (ret < 0)
		return ret;

	return (ret & mask) >> shift;
}

static int adm1275_write_pmon_config(const struct adm1275_data *data,
				     struct i2c_client *client,
				     bool is_power, u16 word)
{
	int shift, ret;
	u16 mask;

	if (data->have_power_sampling) {
		ret = i2c_smbus_read_word_data(client, ADM1275_PMON_CONFIG);
		mask = is_power ? ADM1278_PWR_AVG_MASK : ADM1278_VI_AVG_MASK;
		shift = is_power ? ADM1278_PWR_AVG_SHIFT : ADM1278_VI_AVG_SHIFT;
	} else {
		ret = i2c_smbus_read_byte_data(client, ADM1275_PMON_CONFIG);
		mask = ADM1275_VI_AVG_MASK;
		shift = ADM1275_VI_AVG_SHIFT;
	}
	if (ret < 0)
		return ret;

	word = (ret & ~mask) | ((word << shift) & mask);
	if (data->have_power_sampling)
		ret = i2c_smbus_write_word_data(client, ADM1275_PMON_CONFIG,
						word);
	else
		ret = i2c_smbus_write_byte_data(client, ADM1275_PMON_CONFIG,
						word);

	return ret;
}

static int adm1275_read_word_data(struct i2c_client *client, int page,
				  int phase, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct adm1275_data *data = to_adm1275_data(info);
	int ret = 0;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_IOUT_UC_FAULT_LIMIT:
		if (!data->have_uc_fault)
			return -ENXIO;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1275_IOUT_WARN2_LIMIT);
		break;
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		if (!data->have_oc_fault)
			return -ENXIO;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1275_IOUT_WARN2_LIMIT);
		break;
	case PMBUS_VOUT_OV_WARN_LIMIT:
		if (data->have_vout)
			return -ENODATA;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1075_VAUX_OV_WARN_LIMIT);
		break;
	case PMBUS_VOUT_UV_WARN_LIMIT:
		if (data->have_vout)
			return -ENODATA;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1075_VAUX_UV_WARN_LIMIT);
		break;
	case PMBUS_READ_VOUT:
		if (data->have_vout)
			return -ENODATA;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1075_READ_VAUX);
		break;
	case PMBUS_VIRT_READ_IOUT_MIN:
		if (!data->have_iout_min)
			return -ENXIO;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1293_IOUT_MIN);
		break;
	case PMBUS_VIRT_READ_IOUT_MAX:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1275_PEAK_IOUT);
		break;
	case PMBUS_VIRT_READ_VOUT_MAX:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1275_PEAK_VOUT);
		break;
	case PMBUS_VIRT_READ_VIN_MAX:
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1275_PEAK_VIN);
		break;
	case PMBUS_VIRT_READ_PIN_MIN:
		if (!data->have_pin_min)
			return -ENXIO;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1293_PIN_MIN);
		break;
	case PMBUS_VIRT_READ_PIN_MAX:
		if (!data->have_pin_max)
			return -ENXIO;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1276_PEAK_PIN);
		break;
	case PMBUS_VIRT_READ_TEMP_MAX:
		if (!data->have_temp_max)
			return -ENXIO;
		ret = pmbus_read_word_data(client, 0, 0xff,
					   ADM1278_PEAK_TEMP);
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		break;
	case PMBUS_VIRT_RESET_PIN_HISTORY:
		if (!data->have_pin_max)
			return -ENXIO;
		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		if (!data->have_temp_max)
			return -ENXIO;
		break;
	case PMBUS_VIRT_POWER_SAMPLES:
		if (!data->have_power_sampling)
			return -ENXIO;
		ret = adm1275_read_pmon_config(data, client, true);
		if (ret < 0)
			break;
		ret = BIT(ret);
		break;
	case PMBUS_VIRT_IN_SAMPLES:
	case PMBUS_VIRT_CURR_SAMPLES:
		ret = adm1275_read_pmon_config(data, client, false);
		if (ret < 0)
			break;
		ret = BIT(ret);
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int adm1275_write_word_data(struct i2c_client *client, int page, int reg,
				   u16 word)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct adm1275_data *data = to_adm1275_data(info);
	int ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_IOUT_UC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
		ret = pmbus_write_word_data(client, 0, ADM1275_IOUT_WARN2_LIMIT,
					    word);
		break;
	case PMBUS_VIRT_RESET_IOUT_HISTORY:
		ret = pmbus_write_word_data(client, 0, ADM1275_PEAK_IOUT, 0);
		if (!ret && data->have_iout_min)
			ret = pmbus_write_word_data(client, 0,
						    ADM1293_IOUT_MIN, 0);
		break;
	case PMBUS_VIRT_RESET_VOUT_HISTORY:
		ret = pmbus_write_word_data(client, 0, ADM1275_PEAK_VOUT, 0);
		break;
	case PMBUS_VIRT_RESET_VIN_HISTORY:
		ret = pmbus_write_word_data(client, 0, ADM1275_PEAK_VIN, 0);
		break;
	case PMBUS_VIRT_RESET_PIN_HISTORY:
		ret = pmbus_write_word_data(client, 0, ADM1276_PEAK_PIN, 0);
		if (!ret && data->have_pin_min)
			ret = pmbus_write_word_data(client, 0,
						    ADM1293_PIN_MIN, 0);
		break;
	case PMBUS_VIRT_RESET_TEMP_HISTORY:
		ret = pmbus_write_word_data(client, 0, ADM1278_PEAK_TEMP, 0);
		break;
	case PMBUS_VIRT_POWER_SAMPLES:
		if (!data->have_power_sampling)
			return -ENXIO;
		word = clamp_val(word, 1, ADM1275_SAMPLES_AVG_MAX);
		ret = adm1275_write_pmon_config(data, client, true,
						ilog2(word));
		break;
	case PMBUS_VIRT_IN_SAMPLES:
	case PMBUS_VIRT_CURR_SAMPLES:
		word = clamp_val(word, 1, ADM1275_SAMPLES_AVG_MAX);
		ret = adm1275_write_pmon_config(data, client, false,
						ilog2(word));
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int adm1275_read_byte_data(struct i2c_client *client, int page, int reg)
{
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	const struct adm1275_data *data = to_adm1275_data(info);
	int mfr_status, ret;

	if (page > 0)
		return -ENXIO;

	switch (reg) {
	case PMBUS_STATUS_IOUT:
		ret = pmbus_read_byte_data(client, page, PMBUS_STATUS_IOUT);
		if (ret < 0)
			break;
		if (!data->have_oc_fault && !data->have_uc_fault)
			break;
		mfr_status = pmbus_read_byte_data(client, page,
						  PMBUS_STATUS_MFR_SPECIFIC);
		if (mfr_status < 0)
			return mfr_status;
		if (mfr_status & ADM1275_MFR_STATUS_IOUT_WARN2) {
			ret |= data->have_oc_fault ?
			  PB_IOUT_OC_FAULT : PB_IOUT_UC_FAULT;
		}
		break;
	case PMBUS_STATUS_VOUT:
		if (data->have_vout)
			return -ENODATA;
		ret = 0;
		if (data->have_vaux_status) {
			mfr_status = pmbus_read_byte_data(client, 0,
							  ADM1075_VAUX_STATUS);
			if (mfr_status < 0)
				return mfr_status;
			if (mfr_status & ADM1075_VAUX_OV_WARN)
				ret |= PB_VOLTAGE_OV_WARNING;
			if (mfr_status & ADM1075_VAUX_UV_WARN)
				ret |= PB_VOLTAGE_UV_WARNING;
		} else if (data->have_mfr_vaux_status) {
			mfr_status = pmbus_read_byte_data(client, page,
						PMBUS_STATUS_MFR_SPECIFIC);
			if (mfr_status < 0)
				return mfr_status;
			if (mfr_status & ADM1293_MFR_STATUS_VAUX_OV_WARN)
				ret |= PB_VOLTAGE_OV_WARNING;
			if (mfr_status & ADM1293_MFR_STATUS_VAUX_UV_WARN)
				ret |= PB_VOLTAGE_UV_WARNING;
		}
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static const struct i2c_device_id adm1275_id[] = {
	{ "adm1075", adm1075 },
	{ "adm1272", adm1272 },
	{ "adm1275", adm1275 },
	{ "adm1276", adm1276 },
	{ "adm1278", adm1278 },
	{ "adm1293", adm1293 },
	{ "adm1294", adm1294 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1275_id);

/* Enable VOUT & TEMP1 if not enabled (disabled by default) */
static int adm1275_enable_vout_temp(struct i2c_client *client, int config)
{
	int ret;

	if ((config & ADM1278_PMON_DEFCONFIG) != ADM1278_PMON_DEFCONFIG) {
		config |= ADM1278_PMON_DEFCONFIG;
		ret = i2c_smbus_write_word_data(client, ADM1275_PMON_CONFIG, config);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to enable VOUT/TEMP1 monitoring\n");
			return ret;
		}
	}
	return 0;
}

static int adm1275_probe(struct i2c_client *client)
{
	s32 (*config_read_fn)(const struct i2c_client *client, u8 reg);
	u8 block_buffer[I2C_SMBUS_BLOCK_MAX + 1];
	int config, device_config;
	int ret;
	struct pmbus_driver_info *info;
	struct adm1275_data *data;
	const struct i2c_device_id *mid;
	const struct coefficients *coefficients;
	int vindex = -1, voindex = -1, cindex = -1, pindex = -1;
	int tindex = -1;
	u32 shunt;
	u32 avg;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_BYTE_DATA
				     | I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_ID, block_buffer);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read Manufacturer ID\n");
		return ret;
	}
	if (ret != 3 || strncmp(block_buffer, "ADI", 3)) {
		dev_err(&client->dev, "Unsupported Manufacturer ID\n");
		return -ENODEV;
	}

	ret = i2c_smbus_read_block_data(client, PMBUS_MFR_MODEL, block_buffer);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read Manufacturer Model\n");
		return ret;
	}
	for (mid = adm1275_id; mid->name[0]; mid++) {
		if (!strncasecmp(mid->name, block_buffer, strlen(mid->name)))
			break;
	}
	if (!mid->name[0]) {
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	if (strcmp(client->name, mid->name) != 0)
		dev_notice(&client->dev,
			   "Device mismatch: Configured %s, detected %s\n",
			   client->name, mid->name);

	if (mid->driver_data == adm1272 || mid->driver_data == adm1278 ||
	    mid->driver_data == adm1293 || mid->driver_data == adm1294)
		config_read_fn = i2c_smbus_read_word_data;
	else
		config_read_fn = i2c_smbus_read_byte_data;
	config = config_read_fn(client, ADM1275_PMON_CONFIG);
	if (config < 0)
		return config;

	device_config = config_read_fn(client, ADM1275_DEVICE_CONFIG);
	if (device_config < 0)
		return device_config;

	data = devm_kzalloc(&client->dev, sizeof(struct adm1275_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (of_property_read_u32(client->dev.of_node,
				 "shunt-resistor-micro-ohms", &shunt))
		shunt = 1000; /* 1 mOhm if not set via DT */

	if (shunt == 0)
		return -EINVAL;

	data->id = mid->driver_data;

	info = &data->info;

	info->pages = 1;
	info->format[PSC_VOLTAGE_IN] = direct;
	info->format[PSC_VOLTAGE_OUT] = direct;
	info->format[PSC_CURRENT_OUT] = direct;
	info->format[PSC_POWER] = direct;
	info->format[PSC_TEMPERATURE] = direct;
	info->func[0] = PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT |
			PMBUS_HAVE_SAMPLES;

	info->read_word_data = adm1275_read_word_data;
	info->read_byte_data = adm1275_read_byte_data;
	info->write_word_data = adm1275_write_word_data;

	switch (data->id) {
	case adm1075:
		if (device_config & ADM1275_IOUT_WARN2_SELECT)
			data->have_oc_fault = true;
		else
			data->have_uc_fault = true;
		data->have_pin_max = true;
		data->have_vaux_status = true;

		coefficients = adm1075_coefficients;
		vindex = 0;
		switch (config & ADM1075_IRANGE_MASK) {
		case ADM1075_IRANGE_25:
			cindex = 1;
			pindex = 3;
			break;
		case ADM1075_IRANGE_50:
			cindex = 2;
			pindex = 4;
			break;
		default:
			dev_err(&client->dev, "Invalid input current range");
			break;
		}

		info->func[0] |= PMBUS_HAVE_VIN | PMBUS_HAVE_PIN
		  | PMBUS_HAVE_STATUS_INPUT;
		if (config & ADM1275_VIN_VOUT_SELECT)
			info->func[0] |=
			  PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;
		break;
	case adm1272:
		data->have_vout = true;
		data->have_pin_max = true;
		data->have_temp_max = true;
		data->have_power_sampling = true;

		coefficients = adm1272_coefficients;
		vindex = (config & ADM1275_VRANGE) ? 1 : 0;
		cindex = (config & ADM1272_IRANGE) ? 3 : 2;
		/* pindex depends on the combination of the above */
		switch (config & (ADM1275_VRANGE | ADM1272_IRANGE)) {
		case 0:
		default:
			pindex = 4;
			break;
		case ADM1275_VRANGE:
			pindex = 5;
			break;
		case ADM1272_IRANGE:
			pindex = 6;
			break;
		case ADM1275_VRANGE | ADM1272_IRANGE:
			pindex = 7;
			break;
		}
		tindex = 8;

		info->func[0] |= PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;

		ret = adm1275_enable_vout_temp(client, config);
		if (ret)
			return ret;

		if (config & ADM1278_VIN_EN)
			info->func[0] |= PMBUS_HAVE_VIN;
		break;
	case adm1275:
		if (device_config & ADM1275_IOUT_WARN2_SELECT)
			data->have_oc_fault = true;
		else
			data->have_uc_fault = true;
		data->have_vout = true;

		coefficients = adm1275_coefficients;
		vindex = (config & ADM1275_VRANGE) ? 0 : 1;
		cindex = 2;

		if (config & ADM1275_VIN_VOUT_SELECT)
			info->func[0] |=
			  PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;
		else
			info->func[0] |=
			  PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT;
		break;
	case adm1276:
		if (device_config & ADM1275_IOUT_WARN2_SELECT)
			data->have_oc_fault = true;
		else
			data->have_uc_fault = true;
		data->have_vout = true;
		data->have_pin_max = true;

		coefficients = adm1276_coefficients;
		vindex = (config & ADM1275_VRANGE) ? 0 : 1;
		cindex = 2;
		pindex = (config & ADM1275_VRANGE) ? 3 : 4;

		info->func[0] |= PMBUS_HAVE_VIN | PMBUS_HAVE_PIN
		  | PMBUS_HAVE_STATUS_INPUT;
		if (config & ADM1275_VIN_VOUT_SELECT)
			info->func[0] |=
			  PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;
		break;
	case adm1278:
		data->have_vout = true;
		data->have_pin_max = true;
		data->have_temp_max = true;
		data->have_power_sampling = true;

		coefficients = adm1278_coefficients;
		vindex = 0;
		cindex = 1;
		pindex = 2;
		tindex = 3;

		info->func[0] |= PMBUS_HAVE_PIN | PMBUS_HAVE_STATUS_INPUT |
			PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP;

		ret = adm1275_enable_vout_temp(client, config);
		if (ret)
			return ret;

		if (config & ADM1278_VIN_EN)
			info->func[0] |= PMBUS_HAVE_VIN;
		break;
	case adm1293:
	case adm1294:
		data->have_iout_min = true;
		data->have_pin_min = true;
		data->have_pin_max = true;
		data->have_mfr_vaux_status = true;
		data->have_power_sampling = true;

		coefficients = adm1293_coefficients;

		voindex = 0;
		switch (config & ADM1293_VIN_SEL_MASK) {
		case ADM1293_VIN_SEL_012:	/* 1.2V */
			vindex = 0;
			break;
		case ADM1293_VIN_SEL_074:	/* 7.4V */
			vindex = 1;
			break;
		case ADM1293_VIN_SEL_210:	/* 21V */
			vindex = 2;
			break;
		default:			/* disabled */
			break;
		}

		switch (config & ADM1293_IRANGE_MASK) {
		case ADM1293_IRANGE_25:
			cindex = 3;
			break;
		case ADM1293_IRANGE_50:
			cindex = 4;
			break;
		case ADM1293_IRANGE_100:
			cindex = 5;
			break;
		case ADM1293_IRANGE_200:
			cindex = 6;
			break;
		}

		if (vindex >= 0)
			pindex = 7 + vindex * 4 + (cindex - 3);

		if (config & ADM1293_VAUX_EN)
			info->func[0] |=
				PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;

		info->func[0] |= PMBUS_HAVE_PIN |
			PMBUS_HAVE_VIN | PMBUS_HAVE_STATUS_INPUT;

		break;
	default:
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	if (data->have_power_sampling &&
	    of_property_read_u32(client->dev.of_node,
				 "adi,power-sample-average", &avg) == 0) {
		if (!avg || avg > ADM1275_SAMPLES_AVG_MAX ||
		    BIT(__fls(avg)) != avg) {
			dev_err(&client->dev,
				"Invalid number of power samples");
			return -EINVAL;
		}
		ret = adm1275_write_pmon_config(data, client, true,
						ilog2(avg));
		if (ret < 0) {
			dev_err(&client->dev,
				"Setting power sample averaging failed with error %d",
				ret);
			return ret;
		}
	}

	if (of_property_read_u32(client->dev.of_node,
				"adi,volt-curr-sample-average", &avg) == 0) {
		if (!avg || avg > ADM1275_SAMPLES_AVG_MAX ||
		    BIT(__fls(avg)) != avg) {
			dev_err(&client->dev,
				"Invalid number of voltage/current samples");
			return -EINVAL;
		}
		ret = adm1275_write_pmon_config(data, client, false,
						ilog2(avg));
		if (ret < 0) {
			dev_err(&client->dev,
				"Setting voltage and current sample averaging failed with error %d",
				ret);
			return ret;
		}
	}

	if (voindex < 0)
		voindex = vindex;
	if (vindex >= 0) {
		info->m[PSC_VOLTAGE_IN] = coefficients[vindex].m;
		info->b[PSC_VOLTAGE_IN] = coefficients[vindex].b;
		info->R[PSC_VOLTAGE_IN] = coefficients[vindex].R;
	}
	if (voindex >= 0) {
		info->m[PSC_VOLTAGE_OUT] = coefficients[voindex].m;
		info->b[PSC_VOLTAGE_OUT] = coefficients[voindex].b;
		info->R[PSC_VOLTAGE_OUT] = coefficients[voindex].R;
	}
	if (cindex >= 0) {
		/* Scale current with sense resistor value */
		info->m[PSC_CURRENT_OUT] =
			coefficients[cindex].m * shunt / 1000;
		info->b[PSC_CURRENT_OUT] = coefficients[cindex].b;
		info->R[PSC_CURRENT_OUT] = coefficients[cindex].R;
	}
	if (pindex >= 0) {
		info->m[PSC_POWER] =
			coefficients[pindex].m * shunt / 1000;
		info->b[PSC_POWER] = coefficients[pindex].b;
		info->R[PSC_POWER] = coefficients[pindex].R;
	}
	if (tindex >= 0) {
		info->m[PSC_TEMPERATURE] = coefficients[tindex].m;
		info->b[PSC_TEMPERATURE] = coefficients[tindex].b;
		info->R[PSC_TEMPERATURE] = coefficients[tindex].R;
	}

	return pmbus_do_probe(client, info);
}

static struct i2c_driver adm1275_driver = {
	.driver = {
		   .name = "adm1275",
		   },
	.probe_new = adm1275_probe,
	.remove = pmbus_do_remove,
	.id_table = adm1275_id,
};

module_i2c_driver(adm1275_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for Analog Devices ADM1275 and compatibles");
MODULE_LICENSE("GPL");
