// SPDX-License-Identifier: GPL-2.0-or-later
/* Honeywell HIH-6130/HIH-6131 humidity and temperature sensor driver
 *
 * Copyright (C) 2012 Iain Paton <ipaton0@gmail.com>
 *
 * heavily based on the sht21 driver
 * Copyright (C) 2010 Urs Fleisch <urs.fleisch@sensirion.com>
 *
 * Data sheets available (2012-06-22) at
 * http://sensing.honeywell.com/index.php?ci_id=3106&la_id=1&defId=44872
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

/**
 * struct hih6130 - HIH-6130 device specific data
 * @client: pointer to I2C client device
 * @lock: mutex to protect measurement values
 * @valid: only false before first measurement is taken
 * @last_update: time of last update (jiffies)
 * @temperature: cached temperature measurement value
 * @humidity: cached humidity measurement value
 * @write_length: length for I2C measurement request
 */
struct hih6130 {
	struct i2c_client *client;
	struct mutex lock;
	bool valid;
	unsigned long last_update;
	int temperature;
	int humidity;
	size_t write_length;
};

/**
 * hih6130_temp_ticks_to_millicelsius() - convert raw temperature ticks to
 * milli celsius
 * @ticks: temperature ticks value received from sensor
 */
static inline int hih6130_temp_ticks_to_millicelsius(int ticks)
{
	ticks = ticks >> 2;
	/*
	 * from data sheet section 5.0
	 * Formula T = ( ticks / ( 2^14 - 2 ) ) * 165 -40
	 */
	return (DIV_ROUND_CLOSEST(ticks * 1650, 16382) - 400) * 100;
}

/**
 * hih6130_rh_ticks_to_per_cent_mille() - convert raw humidity ticks to
 * one-thousandths of a percent relative humidity
 * @ticks: humidity ticks value received from sensor
 */
static inline int hih6130_rh_ticks_to_per_cent_mille(int ticks)
{
	ticks &= ~0xC000; /* clear status bits */
	/*
	 * from data sheet section 4.0
	 * Formula RH = ( ticks / ( 2^14 -2 ) ) * 100
	 */
	return DIV_ROUND_CLOSEST(ticks * 1000, 16382) * 100;
}

/**
 * hih6130_update_measurements() - get updated measurements from device
 * @dev: device
 *
 * Returns 0 on success, else negative errno.
 */
static int hih6130_update_measurements(struct device *dev)
{
	struct hih6130 *hih6130 = dev_get_drvdata(dev);
	struct i2c_client *client = hih6130->client;
	int ret = 0;
	int t;
	unsigned char tmp[4];
	struct i2c_msg msgs[1] = {
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 4,
			.buf = tmp,
		}
	};

	mutex_lock(&hih6130->lock);

	/*
	 * While the measurement can be completed in ~40ms the sensor takes
	 * much longer to react to a change in external conditions. How quickly
	 * it reacts depends on airflow and other factors outwith our control.
	 * The datasheet specifies maximum 'Response time' for humidity at 8s
	 * and temperature at 30s under specified conditions.
	 * We therefore choose to only read the sensor at most once per second.
	 * This trades off pointless activity polling the sensor much faster
	 * than it can react against better response times in conditions more
	 * favourable than specified in the datasheet.
	 */
	if (time_after(jiffies, hih6130->last_update + HZ) || !hih6130->valid) {

		/*
		 * Write to slave address to request a measurement.
		 * According with the datasheet it should be with no data, but
		 * for systems with I2C bus drivers that do not allow zero
		 * length packets we write one dummy byte to allow sensor
		 * measurements on them.
		 */
		tmp[0] = 0;
		ret = i2c_master_send(client, tmp, hih6130->write_length);
		if (ret < 0)
			goto out;

		/* measurement cycle time is ~36.65msec */
		msleep(40);

		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			goto out;

		if ((tmp[0] & 0xC0) != 0) {
			dev_err(&client->dev, "Error while reading measurement result\n");
			ret = -EIO;
			goto out;
		}

		t = (tmp[0] << 8) + tmp[1];
		hih6130->humidity = hih6130_rh_ticks_to_per_cent_mille(t);

		t = (tmp[2] << 8) + tmp[3];
		hih6130->temperature = hih6130_temp_ticks_to_millicelsius(t);

		hih6130->last_update = jiffies;
		hih6130->valid = true;
	}
out:
	mutex_unlock(&hih6130->lock);

	return ret >= 0 ? 0 : ret;
}

/**
 * hih6130_temperature_show() - show temperature measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to temp1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t hih6130_temperature_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct hih6130 *hih6130 = dev_get_drvdata(dev);
	int ret;

	ret = hih6130_update_measurements(dev);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", hih6130->temperature);
}

/**
 * hih6130_humidity_show() - show humidity measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to humidity1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t hih6130_humidity_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct hih6130 *hih6130 = dev_get_drvdata(dev);
	int ret;

	ret = hih6130_update_measurements(dev);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", hih6130->humidity);
}

/* sysfs attributes */
static SENSOR_DEVICE_ATTR_RO(temp1_input, hih6130_temperature, 0);
static SENSOR_DEVICE_ATTR_RO(humidity1_input, hih6130_humidity, 0);

static struct attribute *hih6130_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(hih6130);

static int hih6130_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct hih6130 *hih6130;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "adapter does not support true I2C\n");
		return -ENODEV;
	}

	hih6130 = devm_kzalloc(dev, sizeof(*hih6130), GFP_KERNEL);
	if (!hih6130)
		return -ENOMEM;

	hih6130->client = client;
	mutex_init(&hih6130->lock);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_QUICK))
		hih6130->write_length = 1;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   hih6130,
							   hih6130_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* Device ID table */
static const struct i2c_device_id hih6130_id[] = {
	{ "hih6130" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hih6130_id);

static const struct of_device_id __maybe_unused hih6130_of_match[] = {
	{ .compatible = "honeywell,hih6130", },
	{ }
};
MODULE_DEVICE_TABLE(of, hih6130_of_match);

static struct i2c_driver hih6130_driver = {
	.driver = {
		.name = "hih6130",
		.of_match_table = of_match_ptr(hih6130_of_match),
	},
	.probe       = hih6130_probe,
	.id_table    = hih6130_id,
};

module_i2c_driver(hih6130_driver);

MODULE_AUTHOR("Iain Paton <ipaton0@gmail.com>");
MODULE_DESCRIPTION("Honeywell HIH-6130 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");
