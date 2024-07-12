// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2012 Guenter Roeck <linux@roeck-us.net>
 *
 * based on max1668.c
 * Copyright (c) 2011 David George <david.george@ska.ac.za>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>

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
 * Map device tree / internal register bit map to chip bit map.
 * Applies to alert register and over-temperature register.
 */

#define MAX6697_EXTERNAL_MASK_DT	GENMASK(7, 1)
#define MAX6697_LOCAL_MASK_DT		BIT(0)
#define MAX6697_EXTERNAL_MASK_CHIP	GENMASK(6, 0)
#define MAX6697_LOCAL_MASK_CHIP		BIT(7)

/* alert - local channel is in bit 6 */
#define MAX6697_ALERT_MAP_BITS(reg)	((((reg) & 0x7e) >> 1) | \
				 (((reg) & 0x01) << 6) | ((reg) & 0x80))

/* over-temperature - local channel is in bit 7 */
#define MAX6697_OVERT_MAP_BITS(reg)	\
	(FIELD_PREP(MAX6697_EXTERNAL_MASK_CHIP, FIELD_GET(MAX6697_EXTERNAL_MASK_DT, reg)) | \
	 FIELD_PREP(MAX6697_LOCAL_MASK_CHIP, FIELD_GET(MAX6697_LOCAL_MASK_DT, reg)))

#define MAX6697_REG_STAT(n)		(0x44 + (n))

#define MAX6697_REG_CONFIG		0x41
#define MAX6581_CONF_EXTENDED		BIT(1)
#define MAX6693_CONF_BETA		BIT(2)
#define MAX6697_CONF_RESISTANCE		BIT(3)
#define MAX6697_CONF_TIMEOUT		BIT(5)
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
	struct regmap *regmap;

	enum chips type;
	const struct max6697_chip_data *chip;

	int temp_offset;	/* in degrees C */

	struct mutex update_lock;

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

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct max6697_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	unsigned int regs[2] = { MAX6697_REG_TEMP[index],
				 MAX6697_REG_TEMP_EXT[index] };
	u8 regdata[2] = { };
	int temp, ret;

	ret = regmap_multi_reg_read(data->regmap, regs, regdata,
				    data->chip->have_ext & BIT(index) ? 2 : 1);
	if (ret)
		return ret;

	temp = ((regdata[0] - data->temp_offset) << 3) | (regdata[1] >> 5);

	return sprintf(buf, "%d\n", temp * 125);
}

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct max6697_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	unsigned int temp;
	int reg, ret;

	if (index == MAX6697_TEMP_MAX)
		reg = MAX6697_REG_MAX[nr];
	else
		reg = MAX6697_REG_CRIT[nr];

	ret = regmap_read(data->regmap, reg, &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", ((int)temp - data->temp_offset) * 1000);
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct max6697_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	unsigned int alarms;
	int reg, ret;

	if (data->chip->alarm_map)
		index = data->chip->alarm_map[index];

	reg = MAX6697_REG_STAT(2 - (index / 8));
	ret = regmap_read(data->regmap, reg, &alarms);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", !!(alarms & BIT(index & 7)));
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

	temp = clamp_val(temp, -1000000, 1000000);	/* prevent underflow */
	temp = DIV_ROUND_CLOSEST(temp, 1000) + data->temp_offset;
	temp = clamp_val(temp, 0, data->type == max6581 ? 255 : 127);
	ret = regmap_write(data->regmap,
			   index == 2 ? MAX6697_REG_MAX[nr]
				      : MAX6697_REG_CRIT[nr],
			   temp);

	return ret ? : count;
}

static ssize_t offset_store(struct device *dev, struct device_attribute *devattr, const char *buf,
			    size_t count)
{
	struct max6697_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	struct regmap *regmap = data->regmap;
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);
	temp = clamp_val(temp, MAX6581_OFFSET_MIN, MAX6581_OFFSET_MAX);
	temp = DIV_ROUND_CLOSEST(temp, 250);
	if (!temp) {	/* disable this (and only this) channel */
		ret = regmap_clear_bits(regmap, MAX6581_REG_OFFSET_SELECT, BIT(index - 1));
		goto unlock;
	}
	/* enable channel, and update offset */
	ret = regmap_set_bits(regmap, MAX6581_REG_OFFSET_SELECT, BIT(index - 1));
	if (ret)
		goto unlock;
	ret = regmap_write(regmap, MAX6581_REG_OFFSET, temp);
unlock:
	mutex_unlock(&data->update_lock);
	return ret ? : count;
}

static ssize_t offset_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	unsigned int regs[2] = { MAX6581_REG_OFFSET_SELECT, MAX6581_REG_OFFSET };
	struct max6697_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(devattr)->index;
	u8 regdata[2];
	int ret;

	ret = regmap_multi_reg_read(data->regmap, regs, regdata, 2);
	if (ret)
		return ret;

	if (!(regdata[0] & BIT(index - 1)))
		regdata[1] = 0;

	return sprintf(buf, "%d\n", max6581_offset_to_millic(regdata[1]));
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

static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 15);
static SENSOR_DEVICE_ATTR_RO(temp2_crit_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(temp3_crit_alarm, alarm, 9);
static SENSOR_DEVICE_ATTR_RO(temp4_crit_alarm, alarm, 10);
static SENSOR_DEVICE_ATTR_RO(temp5_crit_alarm, alarm, 11);
static SENSOR_DEVICE_ATTR_RO(temp6_crit_alarm, alarm, 12);
static SENSOR_DEVICE_ATTR_RO(temp7_crit_alarm, alarm, 13);
static SENSOR_DEVICE_ATTR_RO(temp8_crit_alarm, alarm, 14);

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

	if ((nr == 3 || nr == 4) && !(chip->have_crit & BIT(channel)))
		return 0;
	if (nr == 5 && !(chip->have_fault & BIT(channel)))
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

static int max6697_config_of(struct device_node *node, struct max6697_data *data)
{
	const struct max6697_chip_data *chip = data->chip;
	struct regmap *regmap = data->regmap;
	int ret, confreg;
	u32 vals[2];

	confreg = 0;
	if (of_property_read_bool(node, "smbus-timeout-disable") &&
	    (chip->valid_conf & MAX6697_CONF_TIMEOUT)) {
		confreg |= MAX6697_CONF_TIMEOUT;
	}
	if (of_property_read_bool(node, "extended-range-enable") &&
	    (chip->valid_conf & MAX6581_CONF_EXTENDED)) {
		confreg |= MAX6581_CONF_EXTENDED;
		data->temp_offset = 64;
	}
	if (of_property_read_bool(node, "beta-compensation-enable") &&
	    (chip->valid_conf & MAX6693_CONF_BETA)) {
		confreg |= MAX6693_CONF_BETA;
	}

	if (of_property_read_u32(node, "alert-mask", vals))
		vals[0] = 0;
	ret = regmap_write(regmap, MAX6697_REG_ALERT_MASK,
			   MAX6697_ALERT_MAP_BITS(vals[0]));
	if (ret)
		return ret;

	if (of_property_read_u32(node, "over-temperature-mask", vals))
		vals[0] = 0;
	ret = regmap_write(regmap, MAX6697_REG_OVERT_MASK,
			   MAX6697_OVERT_MAP_BITS(vals[0]));
	if (ret)
		return ret;

	if (data->type != max6581) {
		if (of_property_read_bool(node, "resistance-cancellation") &&
		    chip->valid_conf & MAX6697_CONF_RESISTANCE) {
			confreg |= MAX6697_CONF_RESISTANCE;
		}
	} else {
		if (of_property_read_u32(node, "resistance-cancellation", &vals[0])) {
			if (of_property_read_bool(node, "resistance-cancellation"))
				vals[0] = 0xfe;
			else
				vals[0] = 0;
		}

		vals[0] &= 0xfe;
		ret = regmap_write(regmap, MAX6581_REG_RESISTANCE, vals[0] >> 1);
		if (ret < 0)
			return ret;

		if (of_property_read_u32_array(node, "transistor-ideality", vals, 2)) {
			vals[0] = 0;
			vals[1] = 0;
		}

		ret = regmap_write(regmap, MAX6581_REG_IDEALITY, vals[1]);
		if (ret < 0)
			return ret;
		ret = regmap_write(regmap, MAX6581_REG_IDEALITY_SELECT,
				   (vals[0] & 0xfe) >> 1);
		if (ret < 0)
			return ret;
	}
	return regmap_write(regmap, MAX6697_REG_CONFIG, confreg);
}

static int max6697_init_chip(struct device_node *np, struct max6697_data *data)
{
	unsigned int reg;
	int ret;

	/*
	 * Don't touch configuration if there is no devicetree configuration.
	 * If that is the case, use the current chip configuration.
	 */
	if (!np) {
		struct regmap *regmap = data->regmap;

		ret = regmap_read(regmap, MAX6697_REG_CONFIG, &reg);
		if (ret < 0)
			return ret;
		if (data->type == max6581) {
			if (reg & MAX6581_CONF_EXTENDED)
				data->temp_offset = 64;
			ret = regmap_read(regmap, MAX6581_REG_RESISTANCE, &reg);
		}
	} else {
		ret = max6697_config_of(np, data);
	}

	return ret;
}

static bool max6697_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00 ... 0x09:	/* temperature high bytes */
	case 0x44 ... 0x47:	/* status */
	case 0x51 ... 0x58:	/* temperature low bytes */
		return true;
	default:
		return false;
	}
}

static bool max6697_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg != 0x0a && reg != 0x0f && !max6697_volatile_reg(dev, reg);
}

static const struct regmap_config max6697_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x58,
	.writeable_reg = max6697_writeable_reg,
	.volatile_reg = max6697_volatile_reg,
	.cache_type = REGCACHE_MAPLE,
};

static int max6697_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max6697_data *data;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int err;

	regmap = regmap_init_i2c(client, &max6697_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	data = devm_kzalloc(dev, sizeof(struct max6697_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = regmap;
	data->type = (uintptr_t)i2c_get_match_data(client);
	data->chip = &max6697_chip_data[data->type];
	mutex_init(&data->update_lock);

	err = max6697_init_chip(client->dev.of_node, data);
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
