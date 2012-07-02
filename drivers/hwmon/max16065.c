/*
 * Driver for
 *  Maxim MAX16065/MAX16066 12-Channel/8-Channel, Flash-Configurable
 *  System Managers with Nonvolatile Fault Registers
 *  Maxim MAX16067/MAX16068 6-Channel, Flash-Configurable System Managers
 *  with Nonvolatile Fault Registers
 *  Maxim MAX16070/MAX16071 12-Channel/8-Channel, Flash-Configurable System
 *  Monitors with Nonvolatile Fault Registers
 *
 * Copyright (C) 2011 Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

enum chips { max16065, max16066, max16067, max16068, max16070, max16071 };

/*
 * Registers
 */
#define MAX16065_ADC(x)		((x) * 2)

#define MAX16065_CURR_SENSE	0x18
#define MAX16065_CSP_ADC	0x19
#define MAX16065_FAULT(x)	(0x1b + (x))
#define MAX16065_SCALE(x)	(0x43 + (x))
#define MAX16065_CURR_CONTROL	0x47
#define MAX16065_LIMIT(l, x)	(0x48 + (l) + (x) * 3)	/*
							 * l: limit
							 *  0: min/max
							 *  1: crit
							 *  2: lcrit
							 * x: ADC index
							 */

#define MAX16065_SW_ENABLE	0x73

#define MAX16065_WARNING_OV	(1 << 3) /* Set if secondary threshold is OV
					    warning */

#define MAX16065_CURR_ENABLE	(1 << 0)

#define MAX16065_NUM_LIMIT	3
#define MAX16065_NUM_ADC	12	/* maximum number of ADC channels */

static const int max16065_num_adc[] = {
	[max16065] = 12,
	[max16066] = 8,
	[max16067] = 6,
	[max16068] = 6,
	[max16070] = 12,
	[max16071] = 8,
};

static const bool max16065_have_secondary[] = {
	[max16065] = true,
	[max16066] = true,
	[max16067] = false,
	[max16068] = false,
	[max16070] = true,
	[max16071] = true,
};

static const bool max16065_have_current[] = {
	[max16065] = true,
	[max16066] = true,
	[max16067] = false,
	[max16068] = false,
	[max16070] = true,
	[max16071] = true,
};

struct max16065_data {
	enum chips type;
	struct device *hwmon_dev;
	struct mutex update_lock;
	bool valid;
	unsigned long last_updated; /* in jiffies */
	int num_adc;
	bool have_current;
	int curr_gain;
	/* limits are in mV */
	int limit[MAX16065_NUM_LIMIT][MAX16065_NUM_ADC];
	int range[MAX16065_NUM_ADC + 1];/* voltage range */
	int adc[MAX16065_NUM_ADC + 1];	/* adc values (raw) including csp_adc */
	int curr_sense;
	int fault[2];
};

static const int max16065_adc_range[] = { 5560, 2780, 1390, 0 };
static const int max16065_csp_adc_range[] = { 7000, 14000 };

/* ADC registers have 10 bit resolution. */
static inline int ADC_TO_MV(int adc, int range)
{
	return (adc * range) / 1024;
}

/*
 * Limit registers have 8 bit resolution and match upper 8 bits of ADC
 * registers.
 */
static inline int LIMIT_TO_MV(int limit, int range)
{
	return limit * range / 256;
}

static inline int MV_TO_LIMIT(int mv, int range)
{
	return SENSORS_LIMIT(DIV_ROUND_CLOSEST(mv * 256, range), 0, 255);
}

static inline int ADC_TO_CURR(int adc, int gain)
{
	return adc * 1400000 / (gain * 255);
}

/*
 * max16065_read_adc()
 *
 * Read 16 bit value from <reg>, <reg+1>.
 * Upper 8 bits are in <reg>, lower 2 bits are in bits 7:6 of <reg+1>.
 */
static int max16065_read_adc(struct i2c_client *client, int reg)
{
	int rv;

	rv = i2c_smbus_read_word_swapped(client, reg);
	if (unlikely(rv < 0))
		return rv;
	return rv >> 6;
}

static struct max16065_data *max16065_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max16065_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);
	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		int i;

		for (i = 0; i < data->num_adc; i++)
			data->adc[i]
			  = max16065_read_adc(client, MAX16065_ADC(i));

		if (data->have_current) {
			data->adc[MAX16065_NUM_ADC]
			  = max16065_read_adc(client, MAX16065_CSP_ADC);
			data->curr_sense
			  = i2c_smbus_read_byte_data(client,
						     MAX16065_CURR_SENSE);
		}

		for (i = 0; i < DIV_ROUND_UP(data->num_adc, 8); i++)
			data->fault[i]
			  = i2c_smbus_read_byte_data(client, MAX16065_FAULT(i));

		data->last_updated = jiffies;
		data->valid = 1;
	}
	mutex_unlock(&data->update_lock);
	return data;
}

static ssize_t max16065_show_alarm(struct device *dev,
				   struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(da);
	struct max16065_data *data = max16065_update_device(dev);
	int val = data->fault[attr2->nr];

	if (val < 0)
		return val;

	val &= (1 << attr2->index);
	if (val)
		i2c_smbus_write_byte_data(to_i2c_client(dev),
					  MAX16065_FAULT(attr2->nr), val);

	return snprintf(buf, PAGE_SIZE, "%d\n", !!val);
}

static ssize_t max16065_show_input(struct device *dev,
				   struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct max16065_data *data = max16065_update_device(dev);
	int adc = data->adc[attr->index];

	if (unlikely(adc < 0))
		return adc;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			ADC_TO_MV(adc, data->range[attr->index]));
}

static ssize_t max16065_show_current(struct device *dev,
				     struct device_attribute *da, char *buf)
{
	struct max16065_data *data = max16065_update_device(dev);

	if (unlikely(data->curr_sense < 0))
		return data->curr_sense;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			ADC_TO_CURR(data->curr_sense, data->curr_gain));
}

static ssize_t max16065_set_limit(struct device *dev,
				  struct device_attribute *da,
				  const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct max16065_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;
	int limit;

	err = kstrtoul(buf, 10, &val);
	if (unlikely(err < 0))
		return err;

	limit = MV_TO_LIMIT(val, data->range[attr2->index]);

	mutex_lock(&data->update_lock);
	data->limit[attr2->nr][attr2->index]
	  = LIMIT_TO_MV(limit, data->range[attr2->index]);
	i2c_smbus_write_byte_data(client,
				  MAX16065_LIMIT(attr2->nr, attr2->index),
				  limit);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t max16065_show_limit(struct device *dev,
				   struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct max16065_data *data = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			data->limit[attr2->nr][attr2->index]);
}

/* Construct a sensor_device_attribute structure for each register */

/* Input voltages */
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, max16065_show_input, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, max16065_show_input, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, max16065_show_input, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, max16065_show_input, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, max16065_show_input, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, max16065_show_input, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, max16065_show_input, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, max16065_show_input, NULL, 7);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, max16065_show_input, NULL, 8);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, max16065_show_input, NULL, 9);
static SENSOR_DEVICE_ATTR(in10_input, S_IRUGO, max16065_show_input, NULL, 10);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, max16065_show_input, NULL, 11);
static SENSOR_DEVICE_ATTR(in12_input, S_IRUGO, max16065_show_input, NULL, 12);

/* Input voltages lcrit */
static SENSOR_DEVICE_ATTR_2(in0_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 0);
static SENSOR_DEVICE_ATTR_2(in1_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 1);
static SENSOR_DEVICE_ATTR_2(in2_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 2);
static SENSOR_DEVICE_ATTR_2(in3_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 3);
static SENSOR_DEVICE_ATTR_2(in4_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 4);
static SENSOR_DEVICE_ATTR_2(in5_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 5);
static SENSOR_DEVICE_ATTR_2(in6_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 6);
static SENSOR_DEVICE_ATTR_2(in7_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 7);
static SENSOR_DEVICE_ATTR_2(in8_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 8);
static SENSOR_DEVICE_ATTR_2(in9_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 9);
static SENSOR_DEVICE_ATTR_2(in10_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 10);
static SENSOR_DEVICE_ATTR_2(in11_lcrit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 2, 11);

/* Input voltages crit */
static SENSOR_DEVICE_ATTR_2(in0_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 0);
static SENSOR_DEVICE_ATTR_2(in1_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 1);
static SENSOR_DEVICE_ATTR_2(in2_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 2);
static SENSOR_DEVICE_ATTR_2(in3_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 3);
static SENSOR_DEVICE_ATTR_2(in4_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 4);
static SENSOR_DEVICE_ATTR_2(in5_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 5);
static SENSOR_DEVICE_ATTR_2(in6_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 6);
static SENSOR_DEVICE_ATTR_2(in7_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 7);
static SENSOR_DEVICE_ATTR_2(in8_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 8);
static SENSOR_DEVICE_ATTR_2(in9_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 9);
static SENSOR_DEVICE_ATTR_2(in10_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 10);
static SENSOR_DEVICE_ATTR_2(in11_crit, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 1, 11);

/* Input voltages min */
static SENSOR_DEVICE_ATTR_2(in0_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 0);
static SENSOR_DEVICE_ATTR_2(in1_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 1);
static SENSOR_DEVICE_ATTR_2(in2_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 2);
static SENSOR_DEVICE_ATTR_2(in3_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 3);
static SENSOR_DEVICE_ATTR_2(in4_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 4);
static SENSOR_DEVICE_ATTR_2(in5_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 5);
static SENSOR_DEVICE_ATTR_2(in6_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 6);
static SENSOR_DEVICE_ATTR_2(in7_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 7);
static SENSOR_DEVICE_ATTR_2(in8_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 8);
static SENSOR_DEVICE_ATTR_2(in9_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 9);
static SENSOR_DEVICE_ATTR_2(in10_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 10);
static SENSOR_DEVICE_ATTR_2(in11_min, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 11);

/* Input voltages max */
static SENSOR_DEVICE_ATTR_2(in0_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 0);
static SENSOR_DEVICE_ATTR_2(in1_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 1);
static SENSOR_DEVICE_ATTR_2(in2_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 2);
static SENSOR_DEVICE_ATTR_2(in3_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 3);
static SENSOR_DEVICE_ATTR_2(in4_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 4);
static SENSOR_DEVICE_ATTR_2(in5_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 5);
static SENSOR_DEVICE_ATTR_2(in6_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 6);
static SENSOR_DEVICE_ATTR_2(in7_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 7);
static SENSOR_DEVICE_ATTR_2(in8_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 8);
static SENSOR_DEVICE_ATTR_2(in9_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 9);
static SENSOR_DEVICE_ATTR_2(in10_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 10);
static SENSOR_DEVICE_ATTR_2(in11_max, S_IWUSR | S_IRUGO, max16065_show_limit,
			    max16065_set_limit, 0, 11);

/* alarms */
static SENSOR_DEVICE_ATTR_2(in0_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 0);
static SENSOR_DEVICE_ATTR_2(in1_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 1);
static SENSOR_DEVICE_ATTR_2(in2_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 2);
static SENSOR_DEVICE_ATTR_2(in3_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 3);
static SENSOR_DEVICE_ATTR_2(in4_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 4);
static SENSOR_DEVICE_ATTR_2(in5_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 5);
static SENSOR_DEVICE_ATTR_2(in6_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 6);
static SENSOR_DEVICE_ATTR_2(in7_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    0, 7);
static SENSOR_DEVICE_ATTR_2(in8_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    1, 0);
static SENSOR_DEVICE_ATTR_2(in9_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    1, 1);
static SENSOR_DEVICE_ATTR_2(in10_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    1, 2);
static SENSOR_DEVICE_ATTR_2(in11_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    1, 3);

/* Current and alarm */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, max16065_show_current, NULL, 0);
static SENSOR_DEVICE_ATTR_2(curr1_alarm, S_IRUGO, max16065_show_alarm, NULL,
			    1, 4);

/*
 * Finally, construct an array of pointers to members of the above objects,
 * as required for sysfs_create_group()
 */
static struct attribute *max16065_basic_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_lcrit.dev_attr.attr,
	&sensor_dev_attr_in0_crit.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,

	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_lcrit.dev_attr.attr,
	&sensor_dev_attr_in1_crit.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,

	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_lcrit.dev_attr.attr,
	&sensor_dev_attr_in2_crit.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,

	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_lcrit.dev_attr.attr,
	&sensor_dev_attr_in3_crit.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,

	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_lcrit.dev_attr.attr,
	&sensor_dev_attr_in4_crit.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,

	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_lcrit.dev_attr.attr,
	&sensor_dev_attr_in5_crit.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,

	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_lcrit.dev_attr.attr,
	&sensor_dev_attr_in6_crit.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,

	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_lcrit.dev_attr.attr,
	&sensor_dev_attr_in7_crit.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,

	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in8_lcrit.dev_attr.attr,
	&sensor_dev_attr_in8_crit.dev_attr.attr,
	&sensor_dev_attr_in8_alarm.dev_attr.attr,

	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in9_lcrit.dev_attr.attr,
	&sensor_dev_attr_in9_crit.dev_attr.attr,
	&sensor_dev_attr_in9_alarm.dev_attr.attr,

	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in10_lcrit.dev_attr.attr,
	&sensor_dev_attr_in10_crit.dev_attr.attr,
	&sensor_dev_attr_in10_alarm.dev_attr.attr,

	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in11_lcrit.dev_attr.attr,
	&sensor_dev_attr_in11_crit.dev_attr.attr,
	&sensor_dev_attr_in11_alarm.dev_attr.attr,

	NULL
};

static struct attribute *max16065_current_attributes[] = {
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_alarm.dev_attr.attr,
	NULL
};

static struct attribute *max16065_min_attributes[] = {
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in8_min.dev_attr.attr,
	&sensor_dev_attr_in9_min.dev_attr.attr,
	&sensor_dev_attr_in10_min.dev_attr.attr,
	&sensor_dev_attr_in11_min.dev_attr.attr,
	NULL
};

static struct attribute *max16065_max_attributes[] = {
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in8_max.dev_attr.attr,
	&sensor_dev_attr_in9_max.dev_attr.attr,
	&sensor_dev_attr_in10_max.dev_attr.attr,
	&sensor_dev_attr_in11_max.dev_attr.attr,
	NULL
};

static const struct attribute_group max16065_basic_group = {
	.attrs = max16065_basic_attributes,
};

static const struct attribute_group max16065_current_group = {
	.attrs = max16065_current_attributes,
};

static const struct attribute_group max16065_min_group = {
	.attrs = max16065_min_attributes,
};

static const struct attribute_group max16065_max_group = {
	.attrs = max16065_max_attributes,
};

static void max16065_cleanup(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &max16065_max_group);
	sysfs_remove_group(&client->dev.kobj, &max16065_min_group);
	sysfs_remove_group(&client->dev.kobj, &max16065_current_group);
	sysfs_remove_group(&client->dev.kobj, &max16065_basic_group);
}

static int max16065_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct max16065_data *data;
	int i, j, val, ret;
	bool have_secondary;		/* true if chip has secondary limits */
	bool secondary_is_max = false;	/* secondary limits reflect max */

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
				     | I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (unlikely(!data))
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	data->num_adc = max16065_num_adc[id->driver_data];
	data->have_current = max16065_have_current[id->driver_data];
	have_secondary = max16065_have_secondary[id->driver_data];

	if (have_secondary) {
		val = i2c_smbus_read_byte_data(client, MAX16065_SW_ENABLE);
		if (unlikely(val < 0))
			return val;
		secondary_is_max = val & MAX16065_WARNING_OV;
	}

	/* Read scale registers, convert to range */
	for (i = 0; i < DIV_ROUND_UP(data->num_adc, 4); i++) {
		val = i2c_smbus_read_byte_data(client, MAX16065_SCALE(i));
		if (unlikely(val < 0))
			return val;
		for (j = 0; j < 4 && i * 4 + j < data->num_adc; j++) {
			data->range[i * 4 + j] =
			  max16065_adc_range[(val >> (j * 2)) & 0x3];
		}
	}

	/* Read limits */
	for (i = 0; i < MAX16065_NUM_LIMIT; i++) {
		if (i == 0 && !have_secondary)
			continue;

		for (j = 0; j < data->num_adc; j++) {
			val = i2c_smbus_read_byte_data(client,
						       MAX16065_LIMIT(i, j));
			if (unlikely(val < 0))
				return val;
			data->limit[i][j] = LIMIT_TO_MV(val, data->range[j]);
		}
	}

	/* Register sysfs hooks */
	for (i = 0; i < data->num_adc * 4; i++) {
		/* Do not create sysfs entry if channel is disabled */
		if (!data->range[i / 4])
			continue;

		ret = sysfs_create_file(&client->dev.kobj,
					max16065_basic_attributes[i]);
		if (unlikely(ret))
			goto out;
	}

	if (have_secondary) {
		struct attribute **attr = secondary_is_max ?
		  max16065_max_attributes : max16065_min_attributes;

		for (i = 0; i < data->num_adc; i++) {
			if (!data->range[i])
				continue;

			ret = sysfs_create_file(&client->dev.kobj, attr[i]);
			if (unlikely(ret))
				goto out;
		}
	}

	if (data->have_current) {
		val = i2c_smbus_read_byte_data(client, MAX16065_CURR_CONTROL);
		if (unlikely(val < 0)) {
			ret = val;
			goto out;
		}
		if (val & MAX16065_CURR_ENABLE) {
			/*
			 * Current gain is 6, 12, 24, 48 based on values in
			 * bit 2,3.
			 */
			data->curr_gain = 6 << ((val >> 2) & 0x03);
			data->range[MAX16065_NUM_ADC]
			  = max16065_csp_adc_range[(val >> 1) & 0x01];
			ret = sysfs_create_group(&client->dev.kobj,
						 &max16065_current_group);
			if (unlikely(ret))
				goto out;
		} else {
			data->have_current = false;
		}
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (unlikely(IS_ERR(data->hwmon_dev))) {
		ret = PTR_ERR(data->hwmon_dev);
		goto out;
	}
	return 0;

out:
	max16065_cleanup(client);
	return ret;
}

static int max16065_remove(struct i2c_client *client)
{
	struct max16065_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	max16065_cleanup(client);

	return 0;
}

static const struct i2c_device_id max16065_id[] = {
	{ "max16065", max16065 },
	{ "max16066", max16066 },
	{ "max16067", max16067 },
	{ "max16068", max16068 },
	{ "max16070", max16070 },
	{ "max16071", max16071 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, max16065_id);

/* This is the driver that will be inserted */
static struct i2c_driver max16065_driver = {
	.driver = {
		.name = "max16065",
	},
	.probe = max16065_probe,
	.remove = max16065_remove,
	.id_table = max16065_id,
};

module_i2c_driver(max16065_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("MAX16065 driver");
MODULE_LICENSE("GPL");
