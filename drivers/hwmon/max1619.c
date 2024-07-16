// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max1619.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring
 * Copyright (C) 2003-2004 Oleksij Rempel <bug-track@fisher-privat.net>
 *                         Jean Delvare <jdelvare@suse.de>
 *
 * Based on the lm90 driver. The MAX1619 is a sensor chip made by Maxim.
 * It reports up to two temperatures (its own plus up to
 * one external one). Complete datasheet can be
 * obtained from Maxim's website at:
 *   http://pdfserv.maxim-ic.com/en/ds/MAX1619.pdf
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

/*
 * The MAX1619 registers
 */

#define MAX1619_REG_LOCAL_TEMP		0x00
#define MAX1619_REG_REMOTE_TEMP		0x01
#define MAX1619_REG_STATUS		0x02
#define MAX1619_REG_CONFIG		0x03
#define MAX1619_REG_CONVRATE		0x04
#define MAX1619_REG_REMOTE_HIGH		0x07
#define MAX1619_REG_REMOTE_LOW		0x08
#define MAX1619_REG_REMOTE_CRIT		0x10
#define MAX1619_REG_REMOTE_CRIT_HYST	0x11
#define MAX1619_REG_MAN_ID		0xFE
#define MAX1619_REG_CHIP_ID		0xFF

enum temp_index {
	t_input1 = 0,
	t_input2,
	t_low2,
	t_high2,
	t_crit2,
	t_hyst2,
	t_num_regs
};

/*
 * Client data (each client gets its own)
 */

static const u8 regs[t_num_regs] = {
	[t_input1] = MAX1619_REG_LOCAL_TEMP,
	[t_input2] = MAX1619_REG_REMOTE_TEMP,
	[t_low2] = MAX1619_REG_REMOTE_LOW,
	[t_high2] = MAX1619_REG_REMOTE_HIGH,
	[t_crit2] = MAX1619_REG_REMOTE_CRIT,
	[t_hyst2] = MAX1619_REG_REMOTE_CRIT_HYST,
};

/*
 * Sysfs stuff
 */

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct regmap *regmap = dev_get_drvdata(dev);
	u32 temp;
	int ret;

	ret = regmap_read(regmap, regs[attr->index], &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", sign_extend32(temp, 7) * 1000);
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct regmap *regmap = dev_get_drvdata(dev);
	long val;
	int err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	val = DIV_ROUND_CLOSEST(clamp_val(val, -128000, 127000), 1000);
	err = regmap_write(regmap, regs[attr->index], val);
	if (err < 0)
		return err;
	return count;
}

static int get_alarms(struct regmap *regmap)
{
	static u32 regs[2] = { MAX1619_REG_STATUS, MAX1619_REG_CONFIG };
	u8 regdata[2];
	int ret;

	ret = regmap_multi_reg_read(regmap, regs, regdata, 2);
	if (ret)
		return ret;

	/* OVERT status bit may be reversed */
	if (!(regdata[1] & 0x20))
		regdata[0] ^= 0x02;

	return regdata[0] & 0x1e;
}

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	int alarms;

	alarms = get_alarms(regmap);
	if (alarms < 0)
		return alarms;

	return sprintf(buf, "%d\n", alarms);
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct regmap *regmap = dev_get_drvdata(dev);
	int alarms;

	alarms = get_alarms(regmap);
	if (alarms < 0)
		return alarms;

	return sprintf(buf, "%d\n", (alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, t_input1);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, t_input2);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp, t_low2);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp, t_high2);
static SENSOR_DEVICE_ATTR_RW(temp2_crit, temp, t_crit2);
static SENSOR_DEVICE_ATTR_RW(temp2_crit_hyst, temp, t_hyst2);

static DEVICE_ATTR_RO(alarms);
static SENSOR_DEVICE_ATTR_RO(temp2_crit_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp2_min_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(temp2_max_alarm, alarm, 4);

static struct attribute *max1619_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,

	&dev_attr_alarms.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(max1619);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max1619_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	u8 reg_config, reg_convrate, reg_status, man_id, chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* detection */
	reg_config = i2c_smbus_read_byte_data(client, MAX1619_REG_CONFIG);
	reg_convrate = i2c_smbus_read_byte_data(client, MAX1619_REG_CONVRATE);
	reg_status = i2c_smbus_read_byte_data(client, MAX1619_REG_STATUS);
	if ((reg_config & 0x03) != 0x00
	 || reg_convrate > 0x07 || (reg_status & 0x61) != 0x00) {
		dev_dbg(&adapter->dev, "MAX1619 detection failed at 0x%02x\n",
			client->addr);
		return -ENODEV;
	}

	/* identification */
	man_id = i2c_smbus_read_byte_data(client, MAX1619_REG_MAN_ID);
	chip_id = i2c_smbus_read_byte_data(client, MAX1619_REG_CHIP_ID);
	if (man_id != 0x4D || chip_id != 0x04) {
		dev_info(&adapter->dev,
			 "Unsupported chip (man_id=0x%02X, chip_id=0x%02X).\n",
			 man_id, chip_id);
		return -ENODEV;
	}

	strscpy(info->type, "max1619", I2C_NAME_SIZE);

	return 0;
}

static int max1619_init_chip(struct regmap *regmap)
{
	int ret;

	ret = regmap_write(regmap, MAX1619_REG_CONVRATE, 5);	/* 2 Hz */
	if (ret)
		return ret;

	/* Start conversions */
	return regmap_clear_bits(regmap, MAX1619_REG_CONFIG, 0x40);
}

/* regmap */

static int max1619_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(context, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int max1619_reg_write(void *context, unsigned int reg, unsigned int val)
{
	int offset = reg < MAX1619_REG_REMOTE_CRIT ? 6 : 2;

	return i2c_smbus_write_byte_data(context, reg + offset, val);
}

static bool max1619_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return reg <= MAX1619_REG_STATUS;
}

static bool max1619_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return reg > MAX1619_REG_STATUS && reg <= MAX1619_REG_REMOTE_CRIT_HYST;
}

static const struct regmap_config max1619_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX1619_REG_REMOTE_CRIT_HYST,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max1619_regmap_is_volatile,
	.writeable_reg = max1619_regmap_is_writeable,
};

static const struct regmap_bus max1619_regmap_bus = {
	.reg_write = max1619_reg_write,
	.reg_read = max1619_reg_read,
};

static int max1619_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init(dev, &max1619_regmap_bus, client,
				  &max1619_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = max1619_init_chip(regmap);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name,
							   regmap,
							   max1619_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max1619_id[] = {
	{ "max1619" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max1619_id);

#ifdef CONFIG_OF
static const struct of_device_id max1619_of_match[] = {
	{ .compatible = "maxim,max1619", },
	{},
};

MODULE_DEVICE_TABLE(of, max1619_of_match);
#endif

static struct i2c_driver max1619_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "max1619",
		.of_match_table = of_match_ptr(max1619_of_match),
	},
	.probe		= max1619_probe,
	.id_table	= max1619_id,
	.detect		= max1619_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(max1619_driver);

MODULE_AUTHOR("Oleksij Rempel <bug-track@fisher-privat.net>, Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("MAX1619 sensor driver");
MODULE_LICENSE("GPL");
