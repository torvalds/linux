// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2012 Guenter Roeck <linux@roeck-us.net>
 *
 * based on max1668.c
 * Copyright (c) 2011 David George <david.george@ska.ac.za>
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
#include <linux/of_device.h>
#include <linux/of.h>

#include <linux/platform_data/max6697.h>

enum chips { max6581, max6602, max6622, max6636, max6689, max6693, max6694,
	     max6697, max6698, max6699 };

/* Report local sensor as temp1 */

static const u8 MAX6697_REG_TEMP[] = {
			0x07, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08 };
static const u8 MAX6697_REG_TEMP_EXT[] = {
			0x57, 0x09, 0x52, 0x53, 0x54, 0x55, 0x56, 0 };
static const u8 MAX6697_REG_MAX[] = {
			0x17, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18 };
static const u8 MAX6697_REG_CRIT[] = {
			0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27 };

/*
 * Map device tree / platform data register bit map to chip bit map.
 * Applies to alert register and over-temperature register.
 */
#define MAX6697_ALERT_MAP_BITS(reg)	((((reg) & 0x7e) >> 1) | \
				 (((reg) & 0x01) << 6) | ((reg) & 0x80))
#define MAX6697_OVERT_MAP_BITS(reg) (((reg) >> 1) | (((reg) & 0x01) << 7))

#define MAX6697_REG_STAT(n)		(0x44 + (n))

#define MAX6697_REG_CONFIG		0x41
#define MAX6581_CONF_EXTENDED		(1 << 1)
#define MAX6693_CONF_BETA		(1 << 2)
#define MAX6697_CONF_RESISTANCE		(1 << 3)
#define MAX6697_CONF_TIMEOUT		(1 << 5)
#define MAX6697_REG_ALERT_MASK		0x42
#define MAX6697_REG_OVERT_MASK		0x43

#define MAX6581_REG_RESISTANCE		0x4a
#define MAX6581_REG_IDEALITY		0x4b
#define MAX6581_REG_IDEALITY_SELECT	0x4c
#define MAX6581_REG_OFFSET		0x4d
#define MAX6581_REG_OFFSET_SELECT	0x4e
#define MAX6581_OFFSET_MIN		-31750
#define MAX6581_OFFSET_MAX		31750

#define MAX6697_CONV_TIME		156	/* ms per channel, worst case */

struct max6697_chip_data {
	int channels;
	u32 have_ext;
	u32 have_crit;
	u32 have_fault;
	u8 valid_conf;
	const u8 *alarm_map;
};

struct max6697_data {
	struct i2c_client *client;

	enum chips type;
	const struct max6697_chip_data *chip;

	int update_interval;	/* in milli-seconds */
	int temp_offset;	/* in degrees C */

	struct mutex update_lock;
	unsigned long last_updated;	/* In jiffies */
	bool valid;		/* true if following fields are valid */

	/* 1x local and up to 7x remote */
	u8 temp[8][4];		/* [nr][0]=temp [1]=ext [2]=max [3]=crit */
#define MAX6697_TEMP_INPUT	0
#define MAX6697_TEMP_EXT	1
#define MAX6697_TEMP_MAX	2
#define MAX6697_TEMP_CRIT	3
	u32 alarms;
};

/* Diode fault status bits on MAX6581 are right shifted by one bit */
static const u8 max6581_alarm_map[] = {
	 0, 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23 };

static const struct max6697_chip_data max6697_chip_data[] = {
	[max6581] = {
		.channels = 8,
		.have_crit = 0xff,
		.have_ext = 0x7f,
		.have_fault = 0xfe,
		.valid_conf = MAX6581_CONF_EXTENDED | MAX6697_CONF_TIMEOUT,
		.alarm_map = max6581_alarm_map,
	},
	[max6602] = {
		.channels = 5,
		.have_crit = 0x12,
		.have_ext = 0x02,
		.have_fault = 0x1e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
	[max6622] = {
		.channels = 5,
		.have_crit = 0x12,
		.have_ext = 0x02,
		.have_fault = 0x1e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
	[max6636] = {
		.channels = 7,
		.have_crit = 0x72,
		.have_ext = 0x02,
		.have_fault = 0x7e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
	[max6689] = {
		.channels = 7,
		.have_crit = 0x72,
		.have_ext = 0x02,
		.have_fault = 0x7e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
	[max6693] = {
		.channels = 7,
		.have_crit = 0x72,
		.have_ext = 0x02,
		.have_fault = 0x7e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6693_CONF_BETA |
		  MAX6697_CONF_TIMEOUT,
	},
	[max6694] = {
		.channels = 5,
		.have_crit = 0x12,
		.have_ext = 0x02,
		.have_fault = 0x1e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6693_CONF_BETA |
		  MAX6697_CONF_TIMEOUT,
	},
	[max6697] = {
		.channels = 7,
		.have_crit = 0x72,
		.have_ext = 0x02,
		.have_fault = 0x7e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
	[max6698] = {
		.channels = 7,
		.have_crit = 0x72,
		.have_ext = 0x02,
		.have_fault = 0x0e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
	[max6699] = {
		.channels = 5,
		.have_crit = 0x12,
		.have_ext = 0x02,
		.have_fault = 0x1e,
		.valid_conf = MAX6697_CONF_RESISTANCE | MAX6697_CONF_TIMEOUT,
	},
};

static inline int max6581_offset_to_millic(int val)
{
	return sign_extend32(val, 7) * 250;
}

static struct max6697_data *max6697_update_device(struct device *dev)
{
	struct max6697_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct max6697_data *ret = data;
	int val;
	int i;
	u32 alarms;

	mutex_lock(&data->update_lock);

	if (data->valid &&
	    !time_after(jiffies, data->last_updated
			+ msecs_to_jiffies(data->update_interval)))
		goto abort;

	for (i = 0; i < data->chip->channels; i++) {
		if (data->chip->have_ext & (1 << i)) {
			val = i2c_smbus_read_byte_data(client,
						       MAX6697_REG_TEMP_EXT[i]);
			if (unlikely(val < 0)) {
				ret = ERR_PTR(val);
				goto abort;
			}
			data->temp[i][MAX6697_TEMP_EXT] = val;
		}

		val = i2c_smbus_read_byte_data(client, MAX6697_REG_TEMP[i]);
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp[i][MAX6697_TEMP_INPUT] = val;

		val = i2c_smbus_read_byte_data(client, MAX6697_REG_MAX[i]);
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp[i][MAX6697_TEMP_MAX] = val;

		if (data->chip->have_crit & (1 << i)) {
			val = i2c_smbus_read_byte_data(client,
						       MAX6697_REG_CRIT[i]);
			if (unlikely(val < 0)) {
				ret = ERR_PTR(val);
				goto abort;
			}
			data->temp[i][MAX6697_TEMP_CRIT] = val;
		}
	}

	alarms = 0;
	for (i = 0; i < 3; i++) {
		val = i2c_smbus_read_byte_data(client, MAX6697_REG_STAT(i));
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		alarms = (alarms << 8) | val;
	}
	data->alarms = alarms;
	data->last_updated = jiffies;
	data->valid = true;
abort:
	mutex_unlock(&data->update_lock);

	return ret;
}

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max6697_data *data = max6697_update_device(dev);
	int temp;

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = (data->temp[index][MAX6697_TEMP_INPUT] - data->temp_offset) << 3;
	temp |= data->temp[index][MAX6697_TEMP_EXT] >> 5;

	return sprintf(buf, "%d\n", temp * 125);
}

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	int index = to_sensor_dev_attr_2(devattr)->index;
	struct max6697_data *data = max6697_update_device(dev);
	int temp;

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = data->temp[nr][index];
	temp -= data->temp_offset;

	return sprintf(buf, "%d\n", temp * 1000);
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct max6697_data *data = max6697_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (data->chip->alarm_map)
		index = data->chip->alarm_map[index];

	return sprintf(buf, "%u\n", (data->alarms >> index) & 0x1);
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	int index = to_sensor_dev_attr_2(devattr)->index;
	struct max6697_data *data = dev_get_drvdata(dev);
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);
	temp = DIV_ROUND_CLOSEST(temp, 1000) + data->temp_offset;
	temp = clamp_val(temp, 0, data->type == max6581 ? 255 : 127);
	data->temp[nr][index] = temp;
	ret = i2c_smbus_write_byte_data(data->client,
					index == 2 ? MAX6697_REG_MAX[nr]
						   : MAX6697_REG_CRIT[nr],
					temp);
	mutex_unlock(&data->update_lock);

	return ret < 0 ? ret : count;
}

static ssize_t offset_store(struct device *dev, struct device_attribute *devattr, const char *buf,
			    size_t count)
{
	int val, ret, index, select;
	struct max6697_data *data;
	bool channel_enabled;
	long temp;

	index = to_sensor_dev_attr(devattr)->index;
	data = dev_get_drvdata(dev);
	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);
	select = i2c_smbus_read_byte_data(data->client, MAX6581_REG_OFFSET_SELECT);
	if (select < 0) {
		ret = select;
		goto abort;
	}
	channel_enabled = (select & (1 << (index - 1)));
	temp = clamp_val(temp, MAX6581_OFFSET_MIN, MAX6581_OFFSET_MAX);
	val = DIV_ROUND_CLOSEST(temp, 250);
	/* disable the offset for channel if the new offset is 0 */
	if (val == 0) {
		if (channel_enabled)
			ret = i2c_smbus_write_byte_data(data->client, MAX6581_REG_OFFSET_SELECT,
							select & ~(1 << (index - 1)));
		ret = ret < 0 ? ret : count;
		goto abort;
	}
	if (!channel_enabled) {
		ret = i2c_smbus_write_byte_data(data->client, MAX6581_REG_OFFSET_SELECT,
						select | (1 << (index - 1)));
		if (ret < 0)
			goto abort;
	}
	ret = i2c_smbus_write_byte_data(data->client, MAX6581_REG_OFFSET, val);
	ret = ret < 0 ? ret : count;

abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t offset_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct max6697_data *data;
	int select, ret, index;

	index = to_sensor_dev_attr(devattr)->index;
	data = dev_get_drvdata(dev);
	mutex_lock(&data->update_lock);
	select = i2c_smbus_read_byte_data(data->client, MAX6581_REG_OFFSET_SELECT);
	if (select < 0)
		ret = select;
	else if (select & (1 << (index - 1)))
		ret = i2c_smbus_read_byte_data(data->client, MAX6581_REG_OFFSET);
	else
		ret = 0;
	mutex_unlock(&data->update_lock);
	return ret < 0 ? ret : sprintf(buf, "%d\n", max6581_offset_to_millic(ret));
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_input, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_max, temp, 0, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp1_crit, temp, 0, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp2_input, temp_input, 1);
static SENSOR_DEVICE_ATTR_2_RW(temp2_max, temp, 1, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp2_crit, temp, 1, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp3_input, temp_input, 2);
static SENSOR_DEVICE_ATTR_2_RW(temp3_max, temp, 2, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp3_crit, temp, 2, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp4_input, temp_input, 3);
static SENSOR_DEVICE_ATTR_2_RW(temp4_max, temp, 3, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp4_crit, temp, 3, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp5_input, temp_input, 4);
static SENSOR_DEVICE_ATTR_2_RW(temp5_max, temp, 4, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp5_crit, temp, 4, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp6_input, temp_input, 5);
static SENSOR_DEVICE_ATTR_2_RW(temp6_max, temp, 5, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp6_crit, temp, 5, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp7_input, temp_input, 6);
static SENSOR_DEVICE_ATTR_2_RW(temp7_max, temp, 6, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp7_crit, temp, 6, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp8_input, temp_input, 7);
static SENSOR_DEVICE_ATTR_2_RW(temp8_max, temp, 7, MAX6697_TEMP_MAX);
static SENSOR_DEVICE_ATTR_2_RW(temp8_crit, temp, 7, MAX6697_TEMP_CRIT);

static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 22);
static SENSOR_DEVICE_ATTR_RO(temp2_max_alarm, alarm, 16);
static SENSOR_DEVICE_ATTR_RO(temp3_max_alarm, alarm, 17);
static SENSOR_DEVICE_ATTR_RO(temp4_max_alarm, alarm, 18);
static SENSOR_DEVICE_ATTR_RO(temp5_max_alarm, alarm, 19);
static SENSOR_DEVICE_ATTR_RO(temp6_max_alarm, alarm, 20);
static SENSOR_DEVICE_ATTR_RO(temp7_max_alarm, alarm, 21);
static SENSOR_DEVICE_ATTR_RO(temp8_max_alarm, alarm, 23);

static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 14);
static SENSOR_DEVICE_ATTR_RO(temp2_crit_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(temp3_crit_alarm, alarm, 9);
static SENSOR_DEVICE_ATTR_RO(temp4_crit_alarm, alarm, 10);
static SENSOR_DEVICE_ATTR_RO(temp5_crit_alarm, alarm, 11);
static SENSOR_DEVICE_ATTR_RO(temp6_crit_alarm, alarm, 12);
static SENSOR_DEVICE_ATTR_RO(temp7_crit_alarm, alarm, 13);
static SENSOR_DEVICE_ATTR_RO(temp8_crit_alarm, alarm, 15);

static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_fault, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp4_fault, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(temp5_fault, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(temp6_fault, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(temp7_fault, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(temp8_fault, alarm, 7);

/* There is no offset for local temperature so starting from temp2 */
static SENSOR_DEVICE_ATTR_RW(temp2_offset, offset, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_offset, offset, 2);
static SENSOR_DEVICE_ATTR_RW(temp4_offset, offset, 3);
static SENSOR_DEVICE_ATTR_RW(temp5_offset, offset, 4);
static SENSOR_DEVICE_ATTR_RW(temp6_offset, offset, 5);
static SENSOR_DEVICE_ATTR_RW(temp7_offset, offset, 6);
static SENSOR_DEVICE_ATTR_RW(temp8_offset, offset, 7);

static DEVICE_ATTR(dummy, 0, NULL, NULL);

static umode_t max6697_is_visible(struct kobject *kobj, struct attribute *attr,
				  int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct max6697_data *data = dev_get_drvdata(dev);
	const struct max6697_chip_data *chip = data->chip;
	int channel = index / 7;	/* channel number */
	int nr = index % 7;		/* attribute index within channel */

	if (channel >= chip->channels)
		return 0;

	if ((nr == 3 || nr == 4) && !(chip->have_crit & (1 << channel)))
		return 0;
	if (nr == 5 && !(chip->have_fault & (1 << channel)))
		return 0;
	/* offset reg is only supported on max6581 remote channels */
	if (nr == 6)
		if (data->type != max6581 || channel == 0)
			return 0;

	return attr->mode;
}

/*
 * max6697_is_visible uses the index into the following array to determine
 * if attributes should be created or not. Any change in order or content
 * must be matched in max6697_is_visible.
 */
static struct attribute *max6697_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&dev_attr_dummy.attr,
	&dev_attr_dummy.attr,

	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,

	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,

	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_crit.dev_attr.attr,
	&sensor_dev_attr_temp4_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_fault.dev_attr.attr,
	&sensor_dev_attr_temp4_offset.dev_attr.attr,

	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp5_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_crit.dev_attr.attr,
	&sensor_dev_attr_temp5_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_fault.dev_attr.attr,
	&sensor_dev_attr_temp5_offset.dev_attr.attr,

	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp6_max.dev_attr.attr,
	&sensor_dev_attr_temp6_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp6_crit.dev_attr.attr,
	&sensor_dev_attr_temp6_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp6_fault.dev_attr.attr,
	&sensor_dev_attr_temp6_offset.dev_attr.attr,

	&sensor_dev_attr_temp7_input.dev_attr.attr,
	&sensor_dev_attr_temp7_max.dev_attr.attr,
	&sensor_dev_attr_temp7_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp7_crit.dev_attr.attr,
	&sensor_dev_attr_temp7_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp7_fault.dev_attr.attr,
	&sensor_dev_attr_temp7_offset.dev_attr.attr,

	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp8_max.dev_attr.attr,
	&sensor_dev_attr_temp8_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp8_crit.dev_attr.attr,
	&sensor_dev_attr_temp8_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp8_fault.dev_attr.attr,
	&sensor_dev_attr_temp8_offset.dev_attr.attr,
	NULL
};

static const struct attribute_group max6697_group = {
	.attrs = max6697_attributes, .is_visible = max6697_is_visible,
};
__ATTRIBUTE_GROUPS(max6697);

static void max6697_get_config_of(struct device_node *node,
				  struct max6697_platform_data *pdata)
{
	int len;
	const __be32 *prop;

	pdata->smbus_timeout_disable =
		of_property_read_bool(node, "smbus-timeout-disable");
	pdata->extended_range_enable =
		of_property_read_bool(node, "extended-range-enable");
	pdata->beta_compensation =
		of_property_read_bool(node, "beta-compensation-enable");

	prop = of_get_property(node, "alert-mask", &len);
	if (prop && len == sizeof(u32))
		pdata->alert_mask = be32_to_cpu(prop[0]);
	prop = of_get_property(node, "over-temperature-mask", &len);
	if (prop && len == sizeof(u32))
		pdata->over_temperature_mask = be32_to_cpu(prop[0]);
	prop = of_get_property(node, "resistance-cancellation", &len);
	if (prop) {
		if (len == sizeof(u32))
			pdata->resistance_cancellation = be32_to_cpu(prop[0]);
		else
			pdata->resistance_cancellation = 0xfe;
	}
	prop = of_get_property(node, "transistor-ideality", &len);
	if (prop && len == 2 * sizeof(u32)) {
			pdata->ideality_mask = be32_to_cpu(prop[0]);
			pdata->ideality_value = be32_to_cpu(prop[1]);
	}
}

static int max6697_init_chip(struct max6697_data *data,
			     struct i2c_client *client)
{
	struct max6697_platform_data *pdata = dev_get_platdata(&client->dev);
	struct max6697_platform_data p;
	const struct max6697_chip_data *chip = data->chip;
	int factor = chip->channels;
	int ret, reg;

	/*
	 * Don't touch configuration if neither platform data nor OF
	 * configuration was specified. If that is the case, use the
	 * current chip configuration.
	 */
	if (!pdata && !client->dev.of_node) {
		reg = i2c_smbus_read_byte_data(client, MAX6697_REG_CONFIG);
		if (reg < 0)
			return reg;
		if (data->type == max6581) {
			if (reg & MAX6581_CONF_EXTENDED)
				data->temp_offset = 64;
			reg = i2c_smbus_read_byte_data(client,
						       MAX6581_REG_RESISTANCE);
			if (reg < 0)
				return reg;
			factor += hweight8(reg);
		} else {
			if (reg & MAX6697_CONF_RESISTANCE)
				factor++;
		}
		goto done;
	}

	if (client->dev.of_node) {
		memset(&p, 0, sizeof(p));
		max6697_get_config_of(client->dev.of_node, &p);
		pdata = &p;
	}

	reg = 0;
	if (pdata->smbus_timeout_disable &&
	    (chip->valid_conf & MAX6697_CONF_TIMEOUT)) {
		reg |= MAX6697_CONF_TIMEOUT;
	}
	if (pdata->extended_range_enable &&
	    (chip->valid_conf & MAX6581_CONF_EXTENDED)) {
		reg |= MAX6581_CONF_EXTENDED;
		data->temp_offset = 64;
	}
	if (pdata->resistance_cancellation &&
	    (chip->valid_conf & MAX6697_CONF_RESISTANCE)) {
		reg |= MAX6697_CONF_RESISTANCE;
		factor++;
	}
	if (pdata->beta_compensation &&
	    (chip->valid_conf & MAX6693_CONF_BETA)) {
		reg |= MAX6693_CONF_BETA;
	}

	ret = i2c_smbus_write_byte_data(client, MAX6697_REG_CONFIG, reg);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client, MAX6697_REG_ALERT_MASK,
				MAX6697_ALERT_MAP_BITS(pdata->alert_mask));
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client, MAX6697_REG_OVERT_MASK,
			MAX6697_OVERT_MAP_BITS(pdata->over_temperature_mask));
	if (ret < 0)
		return ret;

	if (data->type == max6581) {
		factor += hweight8(pdata->resistance_cancellation >> 1);
		ret = i2c_smbus_write_byte_data(client, MAX6581_REG_RESISTANCE,
					pdata->resistance_cancellation >> 1);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_write_byte_data(client, MAX6581_REG_IDEALITY,
						pdata->ideality_value);
		if (ret < 0)
			return ret;
		ret = i2c_smbus_write_byte_data(client,
						MAX6581_REG_IDEALITY_SELECT,
						pdata->ideality_mask >> 1);
		if (ret < 0)
			return ret;
	}
done:
	data->update_interval = factor * MAX6697_CONV_TIME;
	return 0;
}

static const struct i2c_device_id max6697_id[];

static int max6697_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct max6697_data *data;
	struct device *hwmon_dev;
	int err;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct max6697_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (client->dev.of_node)
		data->type = (enum chips)of_device_get_match_data(&client->dev);
	else
		data->type = i2c_match_id(max6697_id, client)->driver_data;
	data->chip = &max6697_chip_data[data->type];
	data->client = client;
	mutex_init(&data->update_lock);

	err = max6697_init_chip(data, client);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   max6697_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max6697_id[] = {
	{ "max6581", max6581 },
	{ "max6602", max6602 },
	{ "max6622", max6622 },
	{ "max6636", max6636 },
	{ "max6689", max6689 },
	{ "max6693", max6693 },
	{ "max6694", max6694 },
	{ "max6697", max6697 },
	{ "max6698", max6698 },
	{ "max6699", max6699 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max6697_id);

static const struct of_device_id __maybe_unused max6697_of_match[] = {
	{
		.compatible = "maxim,max6581",
		.data = (void *)max6581
	},
	{
		.compatible = "maxim,max6602",
		.data = (void *)max6602
	},
	{
		.compatible = "maxim,max6622",
		.data = (void *)max6622
	},
	{
		.compatible = "maxim,max6636",
		.data = (void *)max6636
	},
	{
		.compatible = "maxim,max6689",
		.data = (void *)max6689
	},
	{
		.compatible = "maxim,max6693",
		.data = (void *)max6693
	},
	{
		.compatible = "maxim,max6694",
		.data = (void *)max6694
	},
	{
		.compatible = "maxim,max6697",
		.data = (void *)max6697
	},
	{
		.compatible = "maxim,max6698",
		.data = (void *)max6698
	},
	{
		.compatible = "maxim,max6699",
		.data = (void *)max6699
	},
	{ },
};
MODULE_DEVICE_TABLE(of, max6697_of_match);

static struct i2c_driver max6697_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name	= "max6697",
		.of_match_table = of_match_ptr(max6697_of_match),
	},
	.probe = max6697_probe,
	.id_table = max6697_id,
};

module_i2c_driver(max6697_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("MAX6697 temperature sensor driver");
MODULE_LICENSE("GPL");
