// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 David George <david.george@ska.ac.za>
 *
 * based on adm1021.c
 * some credit to Christoph Scheurer, but largely a rewrite
 */

#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* Addresses to scan */
static const unsigned short max1668_addr_list[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

/* max1668 registers */

#define MAX1668_REG_TEMP(nr)	(nr)
#define MAX1668_REG_STAT1	0x05
#define MAX1668_REG_STAT2	0x06
#define MAX1668_REG_MAN_ID	0xfe
#define MAX1668_REG_DEV_ID	0xff

/* limits */

/* high limits */
#define MAX1668_REG_LIMH(nr)	(0x08 + 2 * (nr))
/* read low limits */
#define MAX1668_REG_LIML(nr)	(0x09 + 2 * (nr))

/* manufacturer and device ID Constants */
#define MAN_ID_MAXIM		0x4d
#define DEV_ID_MAX1668		0x3
#define DEV_ID_MAX1805		0x5
#define DEV_ID_MAX1989		0xb

/* read only mode module parameter */
static bool read_only;
module_param(read_only, bool, 0);
MODULE_PARM_DESC(read_only, "Don't set any values, read only mode");

enum chips { max1668, max1805, max1989 };

struct max1668_data {
	struct regmap *regmap;
	const struct attribute_group *groups[3];
	enum chips type;
};

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	u32 temp;
	int ret;

	ret = regmap_read(data->regmap, MAX1668_REG_TEMP(index), &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", sign_extend32(temp, 7) * 1000);
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	u32 temp;
	int ret;

	ret = regmap_read(data->regmap, MAX1668_REG_LIMH(index), &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", sign_extend32(temp, 7) * 1000);
}

static ssize_t show_temp_min(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	u32 temp;
	int ret;

	ret = regmap_read(data->regmap, MAX1668_REG_LIML(index), &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", sign_extend32(temp, 7) * 1000);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	u32 alarm;
	int ret;

	ret = regmap_read(data->regmap,
			  index >= 8 ? MAX1668_REG_STAT1 : MAX1668_REG_STAT2,
			  &alarm);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", !!(alarm & BIT(index & 7)));
}

static ssize_t show_fault(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 alarm, temp;
	int ret;

	ret = regmap_read(regmap, MAX1668_REG_STAT1, &alarm);
	if (ret)
		return ret;

	ret = regmap_read(regmap, MAX1668_REG_TEMP(index), &temp);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", (alarm & BIT(4)) && temp == 127);
}

static ssize_t set_temp_max(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	temp = clamp_val(temp / 1000, -128, 127);
	ret = regmap_write(data->regmap, MAX1668_REG_LIMH(index), temp);
	if (ret < 0)
		count = ret;

	return count;
}

static ssize_t set_temp_min(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct max1668_data *data = dev_get_drvdata(dev);
	long temp;
	int ret;

	ret = kstrtol(buf, 10, &temp);
	if (ret < 0)
		return ret;

	temp = clamp_val(temp / 1000, -128, 127);
	ret = regmap_write(data->regmap, MAX1668_REG_LIML(index), temp);
	if (ret < 0)
		count = ret;

	return count;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp_max,
				set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO, show_temp_min,
				set_temp_min, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO, show_temp_max,
				set_temp_max, 1);
static SENSOR_DEVICE_ATTR(temp2_min, S_IRUGO, show_temp_min,
				set_temp_min, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_max, S_IRUGO, show_temp_max,
				set_temp_max, 2);
static SENSOR_DEVICE_ATTR(temp3_min, S_IRUGO, show_temp_min,
				set_temp_min, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_max, S_IRUGO, show_temp_max,
				set_temp_max, 3);
static SENSOR_DEVICE_ATTR(temp4_min, S_IRUGO, show_temp_min,
				set_temp_min, 3);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO, show_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(temp5_max, S_IRUGO, show_temp_max,
				set_temp_max, 4);
static SENSOR_DEVICE_ATTR(temp5_min, S_IRUGO, show_temp_min,
				set_temp_min, 4);

static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 14);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp3_min_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp4_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_max_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp5_min_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp5_max_alarm, S_IRUGO, show_alarm, NULL, 0);

static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_fault, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_fault, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_fault, S_IRUGO, show_fault, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_fault, S_IRUGO, show_fault, NULL, 4);

/* Attributes common to MAX1668, MAX1989 and MAX1805 */
static struct attribute *max1668_attribute_common[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,

	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,

	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	NULL
};

/* Attributes not present on MAX1805 */
static struct attribute *max1668_attribute_unique[] = {
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp5_min.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,

	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_min_alarm.dev_attr.attr,

	&sensor_dev_attr_temp4_fault.dev_attr.attr,
	&sensor_dev_attr_temp5_fault.dev_attr.attr,
	NULL
};

static umode_t max1668_attribute_mode(struct kobject *kobj,
				     struct attribute *attr, int index)
{
	umode_t ret = S_IRUGO;
	if (read_only)
		return ret;
	if (attr == &sensor_dev_attr_temp1_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp2_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp3_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp4_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp5_max.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp1_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp2_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp3_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp4_min.dev_attr.attr ||
	    attr == &sensor_dev_attr_temp5_min.dev_attr.attr)
		ret |= S_IWUSR;
	return ret;
}

static const struct attribute_group max1668_group_common = {
	.attrs = max1668_attribute_common,
	.is_visible = max1668_attribute_mode
};

static const struct attribute_group max1668_group_unique = {
	.attrs = max1668_attribute_unique,
	.is_visible = max1668_attribute_mode
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max1668_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *type_name;
	int man_id, dev_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Check for unsupported part */
	man_id = i2c_smbus_read_byte_data(client, MAX1668_REG_MAN_ID);
	if (man_id != MAN_ID_MAXIM)
		return -ENODEV;

	dev_id = i2c_smbus_read_byte_data(client, MAX1668_REG_DEV_ID);
	if (dev_id < 0)
		return -ENODEV;

	type_name = NULL;
	if (dev_id == DEV_ID_MAX1668)
		type_name = "max1668";
	else if (dev_id == DEV_ID_MAX1805)
		type_name = "max1805";
	else if (dev_id == DEV_ID_MAX1989)
		type_name = "max1989";

	if (!type_name)
		return -ENODEV;

	strscpy(info->type, type_name, I2C_NAME_SIZE);

	return 0;
}

/* regmap */

static int max1668_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(context, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int max1668_reg_write(void *context, unsigned int reg, unsigned int val)
{
	return i2c_smbus_write_byte_data(context, reg + 11, val);
}

static bool max1668_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return reg <= MAX1668_REG_STAT2;
}

static bool max1668_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return reg > MAX1668_REG_STAT2 && reg <= MAX1668_REG_LIML(4);
}

static const struct regmap_config max1668_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max1668_regmap_is_volatile,
	.writeable_reg = max1668_regmap_is_writeable,
};

static const struct regmap_bus max1668_regmap_bus = {
	.reg_write = max1668_reg_write,
	.reg_read = max1668_reg_read,
};

static int max1668_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct max1668_data *data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct max1668_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init(dev, &max1668_regmap_bus, client,
					&max1668_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	data->type = (uintptr_t)i2c_get_match_data(client);

	/* sysfs hooks */
	data->groups[0] = &max1668_group_common;
	if (data->type == max1668 || data->type == max1989)
		data->groups[1] = &max1668_group_unique;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max1668_id[] = {
	{ "max1668", max1668 },
	{ "max1805", max1805 },
	{ "max1989", max1989 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1668_id);

/* This is the driver that will be inserted */
static struct i2c_driver max1668_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		  .name	= "max1668",
		  },
	.probe = max1668_probe,
	.id_table = max1668_id,
	.detect	= max1668_detect,
	.address_list = max1668_addr_list,
};

module_i2c_driver(max1668_driver);

MODULE_AUTHOR("David George <david.george@ska.ac.za>");
MODULE_DESCRIPTION("MAX1668 remote temperature sensor driver");
MODULE_LICENSE("GPL");
