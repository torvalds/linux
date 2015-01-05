/*
 * Driver for Texas Instruments INA219, INA226 power monitor chips
 *
 * INA219:
 * Zero Drift Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina219
 *
 * INA220:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina220
 *
 * INA226:
 * Bi-Directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina226
 *
 * INA230:
 * Bi-directional Current/Power Monitor with I2C Interface
 * Datasheet: http://www.ti.com/product/ina230
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
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/delay.h>

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
#define INA2XX_MAX_DELAY		69 /* worst case delay in ms */

#define INA2XX_RSHUNT_DEFAULT		10000

enum ina2xx_ids { ina219, ina226 };

struct ina2xx_config {
	u16 config_default;
	int calibration_factor;
	int registers;
	int shunt_div;
	int bus_voltage_shift;
	int bus_voltage_lsb;	/* uV */
	int power_lsb;		/* uW */
};

struct ina2xx_data {
	struct i2c_client *client;
	const struct ina2xx_config *config;

	long rshunt;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;

	int kind;
	u16 regs[INA2XX_MAX_REGISTERS];
};

static const struct ina2xx_config ina2xx_config[] = {
	[ina219] = {
		.config_default = INA219_CONFIG_DEFAULT,
		.calibration_factor = 40960000,
		.registers = INA219_REGISTERS,
		.shunt_div = 100,
		.bus_voltage_shift = 3,
		.bus_voltage_lsb = 4000,
		.power_lsb = 20000,
	},
	[ina226] = {
		.config_default = INA226_CONFIG_DEFAULT,
		.calibration_factor = 5120000,
		.registers = INA226_REGISTERS,
		.shunt_div = 400,
		.bus_voltage_shift = 0,
		.bus_voltage_lsb = 1250,
		.power_lsb = 25000,
	},
};

/*
 * Initialize the configuration and calibration registers.
 */
static int ina2xx_init(struct ina2xx_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	/* device configuration */
	ret = i2c_smbus_write_word_swapped(client, INA2XX_CONFIG,
					   data->config->config_default);
	if (ret < 0)
		return ret;

	/*
	 * Set current LSB to 1mA, shunt is in uOhms
	 * (equation 13 in datasheet).
	 */
	return i2c_smbus_write_word_swapped(client, INA2XX_CALIBRATION,
			data->config->calibration_factor / data->rshunt);
}

static int ina2xx_do_update(struct device *dev)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i, rv, retry;

	dev_dbg(&client->dev, "Starting ina2xx update\n");

	for (retry = 5; retry; retry--) {
		/* Read all registers */
		for (i = 0; i < data->config->registers; i++) {
			rv = i2c_smbus_read_word_swapped(client, i);
			if (rv < 0)
				return rv;
			data->regs[i] = rv;
		}

		/*
		 * If the current value in the calibration register is 0, the
		 * power and current registers will also remain at 0. In case
		 * the chip has been reset let's check the calibration
		 * register and reinitialize if needed.
		 */
		if (data->regs[INA2XX_CALIBRATION] == 0) {
			dev_warn(dev, "chip not calibrated, reinitializing\n");

			rv = ina2xx_init(data);
			if (rv < 0)
				return rv;

			/*
			 * Let's make sure the power and current registers
			 * have been updated before trying again.
			 */
			msleep(INA2XX_MAX_DELAY);
			continue;
		}

		data->last_updated = jiffies;
		data->valid = 1;

		return 0;
	}

	/*
	 * If we're here then although all write operations succeeded, the
	 * chip still returns 0 in the calibration register. Nothing more we
	 * can do here.
	 */
	dev_err(dev, "unable to reinitialize the chip\n");
	return -ENODEV;
}

static struct ina2xx_data *ina2xx_update_device(struct device *dev)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	struct ina2xx_data *ret = data;
	int rv;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated +
		       HZ / INA2XX_CONVERSION_RATE) || !data->valid) {
		rv = ina2xx_do_update(dev);
		if (rv < 0)
			ret = ERR_PTR(rv);
	}

	mutex_unlock(&data->update_lock);
	return ret;
}

static int ina2xx_get_value(struct ina2xx_data *data, u8 reg)
{
	int val;

	switch (reg) {
	case INA2XX_SHUNT_VOLTAGE:
		/* signed register */
		val = DIV_ROUND_CLOSEST((s16)data->regs[reg],
					data->config->shunt_div);
		break;
	case INA2XX_BUS_VOLTAGE:
		val = (data->regs[reg] >> data->config->bus_voltage_shift)
		  * data->config->bus_voltage_lsb;
		val = DIV_ROUND_CLOSEST(val, 1000);
		break;
	case INA2XX_POWER:
		val = data->regs[reg] * data->config->power_lsb;
		break;
	case INA2XX_CURRENT:
		/* signed register, LSB=1mA (selected), in mA */
		val = (s16)data->regs[reg];
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

	if (IS_ERR(data))
		return PTR_ERR(data);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			ina2xx_get_value(data, attr->index));
}

/* shunt voltage */
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, ina2xx_show_value, NULL,
			  INA2XX_SHUNT_VOLTAGE);

/* bus voltage */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, ina2xx_show_value, NULL,
			  INA2XX_BUS_VOLTAGE);

/* calculated current */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, ina2xx_show_value, NULL,
			  INA2XX_CURRENT);

/* calculated power */
static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, ina2xx_show_value, NULL,
			  INA2XX_POWER);

/* pointers to created device attributes */
static struct attribute *ina2xx_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ina2xx);

static int ina2xx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct ina2xx_platform_data *pdata;
	struct device *dev = &client->dev;
	struct ina2xx_data *data;
	struct device *hwmon_dev;
	u32 val;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (dev_get_platdata(dev)) {
		pdata = dev_get_platdata(dev);
		data->rshunt = pdata->shunt_uohms;
	} else if (!of_property_read_u32(dev->of_node,
					 "shunt-resistor", &val)) {
		data->rshunt = val;
	} else {
		data->rshunt = INA2XX_RSHUNT_DEFAULT;
	}

	/* set the device type */
	data->kind = id->driver_data;
	data->config = &ina2xx_config[data->kind];
	data->client = client;

	if (data->rshunt <= 0 ||
	    data->rshunt > data->config->calibration_factor)
		return -ENODEV;

	ret = ina2xx_init(data);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	mutex_init(&data->update_lock);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, ina2xx_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "power monitor %s (Rshunt = %li uOhm)\n",
		 id->name, data->rshunt);

	return 0;
}

static const struct i2c_device_id ina2xx_id[] = {
	{ "ina219", ina219 },
	{ "ina220", ina219 },
	{ "ina226", ina226 },
	{ "ina230", ina226 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina2xx_id);

static struct i2c_driver ina2xx_driver = {
	.driver = {
		.name	= "ina2xx",
	},
	.probe		= ina2xx_probe,
	.id_table	= ina2xx_id,
};

module_i2c_driver(ina2xx_driver);

MODULE_AUTHOR("Lothar Felten <l-felten@ti.com>");
MODULE_DESCRIPTION("ina2xx driver");
MODULE_LICENSE("GPL");
