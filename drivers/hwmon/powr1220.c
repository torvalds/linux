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

/* Shows the voltage associated with the specified ADC channel */
static ssize_t powr1220_voltage_show(struct device *dev,
				     struct device_attribute *dev_attr,
				     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	int adc_val = powr1220_read_adc(dev, attr->index);

	if (adc_val < 0)
		return adc_val;

	return sprintf(buf, "%d\n", adc_val);
}

/* Shows the maximum setting associated with the specified ADC channel */
static ssize_t powr1220_max_show(struct device *dev,
				 struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct powr1220_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", data->adc_maxes[attr->index]);
}

/* Shows the label associated with the specified ADC channel */
static ssize_t powr1220_label_show(struct device *dev,
				   struct device_attribute *dev_attr,
				   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	return sprintf(buf, "%s\n", input_names[attr->index]);
}

static SENSOR_DEVICE_ATTR_RO(in0_input, powr1220_voltage, VMON1);
static SENSOR_DEVICE_ATTR_RO(in1_input, powr1220_voltage, VMON2);
static SENSOR_DEVICE_ATTR_RO(in2_input, powr1220_voltage, VMON3);
static SENSOR_DEVICE_ATTR_RO(in3_input, powr1220_voltage, VMON4);
static SENSOR_DEVICE_ATTR_RO(in4_input, powr1220_voltage, VMON5);
static SENSOR_DEVICE_ATTR_RO(in5_input, powr1220_voltage, VMON6);
static SENSOR_DEVICE_ATTR_RO(in6_input, powr1220_voltage, VMON7);
static SENSOR_DEVICE_ATTR_RO(in7_input, powr1220_voltage, VMON8);
static SENSOR_DEVICE_ATTR_RO(in8_input, powr1220_voltage, VMON9);
static SENSOR_DEVICE_ATTR_RO(in9_input, powr1220_voltage, VMON10);
static SENSOR_DEVICE_ATTR_RO(in10_input, powr1220_voltage, VMON11);
static SENSOR_DEVICE_ATTR_RO(in11_input, powr1220_voltage, VMON12);
static SENSOR_DEVICE_ATTR_RO(in12_input, powr1220_voltage, VCCA);
static SENSOR_DEVICE_ATTR_RO(in13_input, powr1220_voltage, VCCINP);

static SENSOR_DEVICE_ATTR_RO(in0_highest, powr1220_max, VMON1);
static SENSOR_DEVICE_ATTR_RO(in1_highest, powr1220_max, VMON2);
static SENSOR_DEVICE_ATTR_RO(in2_highest, powr1220_max, VMON3);
static SENSOR_DEVICE_ATTR_RO(in3_highest, powr1220_max, VMON4);
static SENSOR_DEVICE_ATTR_RO(in4_highest, powr1220_max, VMON5);
static SENSOR_DEVICE_ATTR_RO(in5_highest, powr1220_max, VMON6);
static SENSOR_DEVICE_ATTR_RO(in6_highest, powr1220_max, VMON7);
static SENSOR_DEVICE_ATTR_RO(in7_highest, powr1220_max, VMON8);
static SENSOR_DEVICE_ATTR_RO(in8_highest, powr1220_max, VMON9);
static SENSOR_DEVICE_ATTR_RO(in9_highest, powr1220_max, VMON10);
static SENSOR_DEVICE_ATTR_RO(in10_highest, powr1220_max, VMON11);
static SENSOR_DEVICE_ATTR_RO(in11_highest, powr1220_max, VMON12);
static SENSOR_DEVICE_ATTR_RO(in12_highest, powr1220_max, VCCA);
static SENSOR_DEVICE_ATTR_RO(in13_highest, powr1220_max, VCCINP);

static SENSOR_DEVICE_ATTR_RO(in0_label, powr1220_label, VMON1);
static SENSOR_DEVICE_ATTR_RO(in1_label, powr1220_label, VMON2);
static SENSOR_DEVICE_ATTR_RO(in2_label, powr1220_label, VMON3);
static SENSOR_DEVICE_ATTR_RO(in3_label, powr1220_label, VMON4);
static SENSOR_DEVICE_ATTR_RO(in4_label, powr1220_label, VMON5);
static SENSOR_DEVICE_ATTR_RO(in5_label, powr1220_label, VMON6);
static SENSOR_DEVICE_ATTR_RO(in6_label, powr1220_label, VMON7);
static SENSOR_DEVICE_ATTR_RO(in7_label, powr1220_label, VMON8);
static SENSOR_DEVICE_ATTR_RO(in8_label, powr1220_label, VMON9);
static SENSOR_DEVICE_ATTR_RO(in9_label, powr1220_label, VMON10);
static SENSOR_DEVICE_ATTR_RO(in10_label, powr1220_label, VMON11);
static SENSOR_DEVICE_ATTR_RO(in11_label, powr1220_label, VMON12);
static SENSOR_DEVICE_ATTR_RO(in12_label, powr1220_label, VCCA);
static SENSOR_DEVICE_ATTR_RO(in13_label, powr1220_label, VCCINP);

static struct attribute *powr1220_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in13_input.dev_attr.attr,

	&sensor_dev_attr_in0_highest.dev_attr.attr,
	&sensor_dev_attr_in1_highest.dev_attr.attr,
	&sensor_dev_attr_in2_highest.dev_attr.attr,
	&sensor_dev_attr_in3_highest.dev_attr.attr,
	&sensor_dev_attr_in4_highest.dev_attr.attr,
	&sensor_dev_attr_in5_highest.dev_attr.attr,
	&sensor_dev_attr_in6_highest.dev_attr.attr,
	&sensor_dev_attr_in7_highest.dev_attr.attr,
	&sensor_dev_attr_in8_highest.dev_attr.attr,
	&sensor_dev_attr_in9_highest.dev_attr.attr,
	&sensor_dev_attr_in10_highest.dev_attr.attr,
	&sensor_dev_attr_in11_highest.dev_attr.attr,
	&sensor_dev_attr_in12_highest.dev_attr.attr,
	&sensor_dev_attr_in13_highest.dev_attr.attr,

	&sensor_dev_attr_in0_label.dev_attr.attr,
	&sensor_dev_attr_in1_label.dev_attr.attr,
	&sensor_dev_attr_in2_label.dev_attr.attr,
	&sensor_dev_attr_in3_label.dev_attr.attr,
	&sensor_dev_attr_in4_label.dev_attr.attr,
	&sensor_dev_attr_in5_label.dev_attr.attr,
	&sensor_dev_attr_in6_label.dev_attr.attr,
	&sensor_dev_attr_in7_label.dev_attr.attr,
	&sensor_dev_attr_in8_label.dev_attr.attr,
	&sensor_dev_attr_in9_label.dev_attr.attr,
	&sensor_dev_attr_in10_label.dev_attr.attr,
	&sensor_dev_attr_in11_label.dev_attr.attr,
	&sensor_dev_attr_in12_label.dev_attr.attr,
	&sensor_dev_attr_in13_label.dev_attr.attr,

	NULL
};

ATTRIBUTE_GROUPS(powr1220);

static int powr1220_probe(struct i2c_client *client)
{
	struct powr1220_data *data;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->update_lock);
	data->client = client;

	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
			client->name, data, powr1220_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id powr1220_ids[] = {
	{ "powr1220", 0, },
	{ }
};

MODULE_DEVICE_TABLE(i2c, powr1220_ids);

static struct i2c_driver powr1220_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "powr1220",
	},
	.probe_new	= powr1220_probe,
	.id_table	= powr1220_ids,
};

module_i2c_driver(powr1220_driver);

MODULE_AUTHOR("Scott Kanowitz");
MODULE_DESCRIPTION("POWR1220 driver");
MODULE_LICENSE("GPL");
