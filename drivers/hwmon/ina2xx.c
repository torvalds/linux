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
#include <linux/util_macros.h>

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

/* bit mask for reading the averaging setting in the configuration register */
#define INA226_AVG_RD_MASK		0x0E00

#define INA226_READ_AVG(reg)		(((reg) & INA226_AVG_RD_MASK) >> 9)
#define INA226_SHIFT_AVG(val)		((val) << 9)

/* common attrs, ina226 attrs and NULL */
#define INA2XX_MAX_ATTRIBUTE_GROUPS	3

/*
 * Both bus voltage and shunt voltage conversion times for ina226 are set
 * to 0b0100 on POR, which translates to 2200 microseconds in total.
 */
#define INA226_TOTAL_CONV_TIME_DEFAULT	2200

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
	u16 curr_config;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;
	int update_interval; /* in jiffies */

	int kind;
	const struct attribute_group *groups[INA2XX_MAX_ATTRIBUTE_GROUPS];
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
 * Available averaging rates for ina226. The indices correspond with
 * the bit values expected by the chip (according to the ina226 datasheet,
 * table 3 AVG bit settings, found at
 * http://www.ti.com/lit/ds/symlink/ina226.pdf.
 */
static const int ina226_avg_tab[] = { 1, 4, 16, 64, 128, 256, 512, 1024 };

static int ina226_reg_to_interval(u16 config)
{
	int avg = ina226_avg_tab[INA226_READ_AVG(config)];

	/*
	 * Multiply the total conversion time by the number of averages.
	 * Return the result in milliseconds.
	 */
	return DIV_ROUND_CLOSEST(avg * INA226_TOTAL_CONV_TIME_DEFAULT, 1000);
}

static u16 ina226_interval_to_reg(int interval, u16 config)
{
	int avg, avg_bits;

	avg = DIV_ROUND_CLOSEST(interval * 1000,
				INA226_TOTAL_CONV_TIME_DEFAULT);
	avg_bits = find_closest(avg, ina226_avg_tab,
				ARRAY_SIZE(ina226_avg_tab));

	return (config & ~INA226_AVG_RD_MASK) | INA226_SHIFT_AVG(avg_bits);
}

static void ina226_set_update_interval(struct ina2xx_data *data)
{
	int ms;

	ms = ina226_reg_to_interval(data->curr_config);
	data->update_interval = msecs_to_jiffies(ms);
}

static int ina2xx_calibrate(struct ina2xx_data *data)
{
	u16 val = DIV_ROUND_CLOSEST(data->config->calibration_factor,
				    data->rshunt);

	return i2c_smbus_write_word_swapped(data->client,
					    INA2XX_CALIBRATION, val);
}

/*
 * Initialize the configuration and calibration registers.
 */
static int ina2xx_init(struct ina2xx_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	/* device configuration */
	ret = i2c_smbus_write_word_swapped(client, INA2XX_CONFIG,
					   data->curr_config);
	if (ret < 0)
		return ret;

	/*
	 * Set current LSB to 1mA, shunt is in uOhms
	 * (equation 13 in datasheet).
	 */
	return ina2xx_calibrate(data);
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
	unsigned long after;
	int rv;

	mutex_lock(&data->update_lock);

	after = data->last_updated + data->update_interval;
	if (time_after(jiffies, after) || !data->valid) {
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
	case INA2XX_CALIBRATION:
		val = DIV_ROUND_CLOSEST(data->config->calibration_factor,
					data->regs[reg]);
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

static ssize_t ina2xx_set_shunt(struct device *dev,
				struct device_attribute *da,
				const char *buf, size_t count)
{
	struct ina2xx_data *data = ina2xx_update_device(dev);
	unsigned long val;
	int status;

	if (IS_ERR(data))
		return PTR_ERR(data);

	status = kstrtoul(buf, 10, &val);
	if (status < 0)
		return status;

	if (val == 0 ||
	    /* Values greater than the calibration factor make no sense. */
	    val > data->config->calibration_factor)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->rshunt = val;
	status = ina2xx_calibrate(data);
	mutex_unlock(&data->update_lock);
	if (status < 0)
		return status;

	return count;
}

static ssize_t ina226_set_interval(struct device *dev,
				   struct device_attribute *da,
				   const char *buf, size_t count)
{
	struct ina2xx_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int status;

	status = kstrtoul(buf, 10, &val);
	if (status < 0)
		return status;

	if (val > INT_MAX || val == 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->curr_config = ina226_interval_to_reg(val,
						   data->regs[INA2XX_CONFIG]);
	status = i2c_smbus_write_word_swapped(data->client,
					      INA2XX_CONFIG,
					      data->curr_config);

	ina226_set_update_interval(data);
	/* Make sure the next access re-reads all registers. */
	data->valid = 0;
	mutex_unlock(&data->update_lock);
	if (status < 0)
		return status;

	return count;
}

static ssize_t ina226_show_interval(struct device *dev,
				    struct device_attribute *da, char *buf)
{
	struct ina2xx_data *data = ina2xx_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	/*
	 * We don't use data->update_interval here as we want to display
	 * the actual interval used by the chip and jiffies_to_msecs()
	 * doesn't seem to be accurate enough.
	 */
	return snprintf(buf, PAGE_SIZE, "%d\n",
			ina226_reg_to_interval(data->regs[INA2XX_CONFIG]));
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

/* shunt resistance */
static SENSOR_DEVICE_ATTR(shunt_resistor, S_IRUGO | S_IWUSR,
			  ina2xx_show_value, ina2xx_set_shunt,
			  INA2XX_CALIBRATION);

/* update interval (ina226 only) */
static SENSOR_DEVICE_ATTR(update_interval, S_IRUGO | S_IWUSR,
			  ina226_show_interval, ina226_set_interval, 0);

/* pointers to created device attributes */
static struct attribute *ina2xx_attrs[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_shunt_resistor.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina2xx_group = {
	.attrs = ina2xx_attrs,
};

static struct attribute *ina226_attrs[] = {
	&sensor_dev_attr_update_interval.dev_attr.attr,
	NULL,
};

static const struct attribute_group ina226_group = {
	.attrs = ina226_attrs,
};

static int ina2xx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct ina2xx_platform_data *pdata;
	struct device *dev = &client->dev;
	struct ina2xx_data *data;
	struct device *hwmon_dev;
	u32 val;
	int ret, group = 0;

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
	data->curr_config = data->config->config_default;
	data->client = client;

	/*
	 * Ina226 has a variable update_interval. For ina219 we
	 * use a constant value.
	 */
	if (data->kind == ina226)
		ina226_set_update_interval(data);
	else
		data->update_interval = HZ / INA2XX_CONVERSION_RATE;

	if (data->rshunt <= 0 ||
	    data->rshunt > data->config->calibration_factor)
		return -ENODEV;

	ret = ina2xx_init(data);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	mutex_init(&data->update_lock);

	data->groups[group++] = &ina2xx_group;
	if (data->kind == ina226)
		data->groups[group++] = &ina226_group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
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
	{ "ina231", ina226 },
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
