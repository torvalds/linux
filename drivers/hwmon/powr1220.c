// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * powr1220.c - Driver for the Lattice POWR1220 programmable power supply
 * and monitor. Users can read all ADC inputs along with their labels
 * using the sysfs nodes.
 *
 * Copyright (c) 2014 Echo360 https://www.echo360.com
 * Scott Kanowitz <skanowitz@echo360.com> <scott.kanowitz@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#define ADC_STEP_MV			2
#define ADC_MAX_LOW_MEASUREMENT_MV	2000

enum powr1xxx_chips { powr1014, powr1220 };

enum powr1220_regs {
	VMON_STATUS0,
	VMON_STATUS1,
	VMON_STATUS2,
	OUTPUT_STATUS0,
	OUTPUT_STATUS1,
	OUTPUT_STATUS2,
	INPUT_STATUS,
	ADC_VALUE_LOW,
	ADC_VALUE_HIGH,
	ADC_MUX,
	UES_BYTE0,
	UES_BYTE1,
	UES_BYTE2,
	UES_BYTE3,
	GP_OUTPUT1,
	GP_OUTPUT2,
	GP_OUTPUT3,
	INPUT_VALUE,
	RESET,
	TRIM1_TRIM,
	TRIM2_TRIM,
	TRIM3_TRIM,
	TRIM4_TRIM,
	TRIM5_TRIM,
	TRIM6_TRIM,
	TRIM7_TRIM,
	TRIM8_TRIM,
	MAX_POWR1220_REGS
};

enum powr1220_adc_values {
	VMON1,
	VMON2,
	VMON3,
	VMON4,
	VMON5,
	VMON6,
	VMON7,
	VMON8,
	VMON9,
	VMON10,
	VMON11,
	VMON12,
	VCCA,
	VCCINP,
	MAX_POWR1220_ADC_VALUES
};

struct powr1220_data {
	struct i2c_client *client;
	struct mutex update_lock;
	u8 max_channels;
	bool adc_valid[MAX_POWR1220_ADC_VALUES];
	 /* the next value is in jiffies */
	unsigned long adc_last_updated[MAX_POWR1220_ADC_VALUES];

	/* values */
	int adc_maxes[MAX_POWR1220_ADC_VALUES];
	int adc_values[MAX_POWR1220_ADC_VALUES];
};

static const char * const input_names[] = {
	[VMON1]    = "vmon1",
	[VMON2]    = "vmon2",
	[VMON3]    = "vmon3",
	[VMON4]    = "vmon4",
	[VMON5]    = "vmon5",
	[VMON6]    = "vmon6",
	[VMON7]    = "vmon7",
	[VMON8]    = "vmon8",
	[VMON9]    = "vmon9",
	[VMON10]   = "vmon10",
	[VMON11]   = "vmon11",
	[VMON12]   = "vmon12",
	[VCCA]     = "vcca",
	[VCCINP]   = "vccinp",
};

/* Reads the specified ADC channel */
static int powr1220_read_adc(struct device *dev, int ch_num)
{
	struct powr1220_data *data = dev_get_drvdata(dev);
	int reading;
	int result;
	int adc_range = 0;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->adc_last_updated[ch_num] + HZ) ||
	    !data->adc_valid[ch_num]) {
		/*
		 * figure out if we need to use the attenuator for
		 * high inputs or inputs that we don't yet have a measurement
		 * for. We dynamically set the attenuator depending on the
		 * max reading.
		 */
		if (data->adc_maxes[ch_num] > ADC_MAX_LOW_MEASUREMENT_MV ||
		    data->adc_maxes[ch_num] == 0)
			adc_range = 1 << 4;

		/* set the attenuator and mux */
		result = i2c_smbus_write_byte_data(data->client, ADC_MUX,
						   adc_range | ch_num);
		if (result)
			goto exit;

		/*
		 * wait at least Tconvert time (200 us) for the
		 * conversion to complete
		 */
		udelay(200);

		/* get the ADC reading */
		result = i2c_smbus_read_byte_data(data->client, ADC_VALUE_LOW);
		if (result < 0)
			goto exit;

		reading = result >> 4;

		/* get the upper half of the reading */
		result = i2c_smbus_read_byte_data(data->client, ADC_VALUE_HIGH);
		if (result < 0)
			goto exit;

		reading |= result << 4;

		/* now convert the reading to a voltage */
		reading *= ADC_STEP_MV;
		data->adc_values[ch_num] = reading;
		data->adc_valid[ch_num] = true;
		data->adc_last_updated[ch_num] = jiffies;
		result = reading;

		if (reading > data->adc_maxes[ch_num])
			data->adc_maxes[ch_num] = reading;
	} else {
		result = data->adc_values[ch_num];
	}

exit:
	mutex_unlock(&data->update_lock);

	return result;
}

static umode_t
powr1220_is_visible(const void *data, enum hwmon_sensor_types type, u32
		    attr, int channel)
{
	struct powr1220_data *chip_data = (struct powr1220_data *)data;

	if (channel >= chip_data->max_channels)
		return 0;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_highest:
		case hwmon_in_label:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int
powr1220_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		     int channel, const char **str)
{
	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
			*str = input_names[channel];
			return 0;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static int
powr1220_read(struct device *dev, enum hwmon_sensor_types type, u32
	      attr, int channel, long *val)
{
	struct powr1220_data *data = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			ret = powr1220_read_adc(dev, channel);
			if (ret < 0)
				return ret;
			*val = ret;
			break;
		case hwmon_in_highest:
			*val = data->adc_maxes[channel];
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
}

	return 0;
}

static const struct hwmon_channel_info * const powr1220_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_HIGHEST | HWMON_I_LABEL),

	NULL
};

static const struct hwmon_ops powr1220_hwmon_ops = {
	.read = powr1220_read,
	.read_string = powr1220_read_string,
	.is_visible = powr1220_is_visible,
};

static const struct hwmon_chip_info powr1220_chip_info = {
	.ops = &powr1220_hwmon_ops,
	.info = powr1220_info,
};

static int powr1220_probe(struct i2c_client *client)
{
	struct powr1220_data *data;
	struct device *hwmon_dev;
	enum powr1xxx_chips chip;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip = (uintptr_t)i2c_get_match_data(client);
	switch (chip) {
	case powr1014:
		data->max_channels = 10;
		break;
	default:
		data->max_channels = 12;
		break;
	}

	mutex_init(&data->update_lock);
	data->client = client;

	hwmon_dev = devm_hwmon_device_register_with_info(&client->dev,
							 client->name,
							 data,
							 &powr1220_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id powr1220_ids[] = {
	{ "powr1014", powr1014, },
	{ "powr1220", powr1220, },
	{ }
};

MODULE_DEVICE_TABLE(i2c, powr1220_ids);

static struct i2c_driver powr1220_driver = {
	.driver = {
		.name	= "powr1220",
	},
	.probe		= powr1220_probe,
	.id_table	= powr1220_ids,
};

module_i2c_driver(powr1220_driver);

MODULE_AUTHOR("Scott Kanowitz");
MODULE_DESCRIPTION("POWR1220 driver");
MODULE_LICENSE("GPL");
