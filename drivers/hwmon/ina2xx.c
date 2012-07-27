/*
 * Driver for Texas Instruments INA219, INA226 power monitor chips
 *
 * INA219:
 * Zero Drift Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina219
 *
 * INA226:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina226
 *
 * Copyright (C) 2012 Lothar Felten <l-felten@ti.com>
 * Thanks to Jan Volkering
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

#include <linux/platform_data/ina2xx.h>

/* common register definitions */
#define INA2XX_CONFIG			0x00
#define INA2XX_SHUNT_VOLTAGE		0x01 /* readonly */
#define INA2XX_BUS_VOLTAGE		0x02 /* readonly */
#define INA2XX_POWER			0x03 /* readonly */
#define INA2XX_CURRENT			0x04 /* readonly */
#define INA2XX_CALIBRATION		0x05

/* INA226 register definitions */
#define INA226_MASK_ENABLE		0x06
#define INA226_ALERT_LIMIT		0x07
#define INA226_DIE_ID			0xFF


/* register count */
#define INA219_REGISTERS		6
#define INA226_REGISTERS		8

#define INA2XX_MAX_REGISTERS		8

/* settings - depend on use case */
#define INA219_CONFIG_DEFAULT		0x399F	/* PGA=8 */
#define INA226_CONFIG_DEFAULT		0x4527	/* averages=16 */

/* worst case is 68.10 ms (~14.6Hz, ina219) */
#define INA2XX_CONVERSION_RATE		15

enum ina2xx_ids { ina219, ina226 };

struct ina2xx_data {
	struct device *hwmon_dev;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;

	int kind;
	int registers;
	u16 regs[INA2XX_MAX_REGISTERS];
};

int ina2xx_read_word(struct i2c_client *client, int reg)
{
	int val = i2c_smbus_read_word_data(client, reg);
	if (unlikely(val < 0)) {
		dev_dbg(&client->dev,
			"Failed to read register: %d\n", reg);
		return val;
	}
	return be16_to_cpu(val);
}

void ina2xx_write_word(struct i2c_client *client, int reg, int data)
{
	i2c_smbus_write_word_data(client, reg, cpu_to_be16(data));
}

static struct ina2xx_data *ina2xx_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ina2xx_data *data = i2c_get_clientdata(client);
	struct ina2xx_data *ret = data;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated +
		       HZ / INA2XX_CONVERSION_RATE) || !data->valid) {

		int i;

		dev_dbg(&client->dev, "Starting ina2xx update\n");

		/* Read all registers */
		for (i = 0; i < data->registers; i++) {
			int rv = ina2xx_read_word(client, i);
			if (rv < 0) {
				ret = ERR_PTR(rv);
				goto abort;
			}
			data->regs[i] = rv;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int ina219_get_value(struct ina2xx_data *data, u8 reg)
{
	/*
	 * calculate exact value for the given register
	 * we assume default power-on reset settings:
	 * bus voltage range 32V
	 * gain = /8
	 * adc 1 & 2 -> conversion time 532uS
	 * mode is continuous shunt and bus
	 * calibration value is INA219_CALIBRATION_VALUE
	 */
	int val = data->regs[reg];

	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
		/* LSB=10uV. Convert to mV. */
		val = DIV_ROUND_CLOSEST(val, 100);
		break;
	case INA2XX_BUS_VOLTAGE:
		/* LSB=4mV. Register is not right aligned, convert to mV. */
		val = (val >> 3) * 4;
		break;
	case INA2XX_POWER:
		/* LSB=20mW. Convert to uW */
		val = val * 20 * 1000;
		break;
	case INA2XX_CURRENT:
		/* LSB=1mA (selected). Is in mA */
		break;
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		val = 0;
		break;
	}

	return val;
}

static int ina226_get_value(struct ina2xx_data *data, u8 reg)
{
	/*
	 * calculate exact value for the given register
	 * we assume default power-on reset settings:
	 * bus voltage range 32V
	 * gain = /8
	 * adc 1 & 2 -> conversion time 532uS
	 * mode is continuous shunt and bus
	 * calibration value is INA226_CALIBRATION_VALUE
	 */
	int val = data->regs[reg];

	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
		/* LSB=2.5uV. Convert to mV. */
		val = DIV_ROUND_CLOSEST(val, 400);
		break;
	case INA2XX_BUS_VOLTAGE:
		/* LSB=1.25mV. Convert to mV. */
		val = val + DIV_ROUND_CLOSEST(val, 4);
		break;
	case INA2XX_POWER:
		/* LSB=25mW. Convert to uW */
		val = val * 25 * 1000;
		break;
	case INA2XX_CURRENT:
		/* LSB=1mA (selected). Is in mA */
		break;
	default:
		/* programmer goofed */
		WARN_ON_ONCE(1);
		val = 0;
		break;
	}

	return val;
}

static ssize_t ina2xx_show_value(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ina2xx_data *data = ina2xx_update_device(dev);
	int value = 0;

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (data->kind) {
	case ina219:
		value = ina219_get_value(data, attr->index);
		break;
	case ina226:
		value = ina226_get_value(data, attr->index);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

/* shunt voltage */
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, \
	ina2xx_show_value, NULL, INA2XX_SHUNT_VOLTAGE);

/* bus voltage */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, \
	ina2xx_show_value, NULL, INA2XX_BUS_VOLTAGE);

/* calculated current */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, \
	ina2xx_show_value, NULL, INA2XX_CURRENT);

/* calculated power */
static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, \
	ina2xx_show_value, NULL, INA2XX_POWER);

/* pointers to created device attributes */
static struct attribute *ina2xx_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina2xx_group = {
	.attrs = ina2xx_attributes,
};

static int ina2xx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct ina2xx_data *data;
	struct ina2xx_platform_data *pdata;
	int ret = 0;
	long shunt = 10000; /* default shunt value 10mOhms */

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (client->dev.platform_data) {
		pdata =
		  (struct ina2xx_platform_data *)client->dev.platform_data;
		shunt = pdata->shunt_uohms;
	}

	if (shunt <= 0)
		return -ENODEV;

	/* set the device type */
	data->kind = id->driver_data;

	switch (data->kind) {
	case ina219:
		/* device configuration */
		ina2xx_write_word(client, INA2XX_CONFIG, INA219_CONFIG_DEFAULT);

		/* set current LSB to 1mA, shunt is in uOhms */
		/* (equation 13 in datasheet) */
		ina2xx_write_word(client, INA2XX_CALIBRATION, 40960000 / shunt);
		dev_info(&client->dev,
			 "power monitor INA219 (Rshunt = %li uOhm)\n", shunt);
		data->registers = INA219_REGISTERS;
		break;
	case ina226:
		/* device configuration */
		ina2xx_write_word(client, INA2XX_CONFIG, INA226_CONFIG_DEFAULT);

		/* set current LSB to 1mA, shunt is in uOhms */
		/* (equation 1 in datasheet)*/
		ina2xx_write_word(client, INA2XX_CALIBRATION, 5120000 / shunt);
		dev_info(&client->dev,
			 "power monitor INA226 (Rshunt = %li uOhm)\n", shunt);
		data->registers = INA226_REGISTERS;
		break;
	default:
		/* unknown device id */
		return -ENODEV;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	ret = sysfs_create_group(&client->dev.kobj, &ina2xx_group);
	if (ret)
		return ret;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto out_err_hwmon;
	}

	return 0;

out_err_hwmon:
	sysfs_remove_group(&client->dev.kobj, &ina2xx_group);
	return ret;
}

static int ina2xx_remove(struct i2c_client *client)
{
	struct ina2xx_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &ina2xx_group);

	return 0;
}

static const struct i2c_device_id ina2xx_id[] = {
	{ "ina219", ina219 },
	{ "ina226", ina226 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina2xx_id);

static struct i2c_driver ina2xx_driver = {
	.driver = {
		.name	= "ina2xx",
	},
	.probe		= ina2xx_probe,
	.remove		= ina2xx_remove,
	.id_table	= ina2xx_id,
};

static int __init ina2xx_init(void)
{
	return i2c_add_driver(&ina2xx_driver);
}

static void __exit ina2xx_exit(void)
{
	i2c_del_driver(&ina2xx_driver);
}

MODULE_AUTHOR("Lothar Felten <l-felten@ti.com>");
MODULE_DESCRIPTION("ina2xx driver");
MODULE_LICENSE("GPL");

module_init(ina2xx_init);
module_exit(ina2xx_exit);
