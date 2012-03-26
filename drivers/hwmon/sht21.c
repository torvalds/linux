/* Sensirion SHT21 humidity and temperature sensor driver
 *
 * Copyright (C) 2010 Urs Fleisch <urs.fleisch@sensirion.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Data sheet available (5/2010) at
 * http://www.sensirion.com/en/pdf/product_information/Datasheet-humidity-sensor-SHT21.pdf
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

/* I2C command bytes */
#define SHT21_TRIG_T_MEASUREMENT_HM  0xe3
#define SHT21_TRIG_RH_MEASUREMENT_HM 0xe5

/**
 * struct sht21 - SHT21 device specific data
 * @hwmon_dev: device registered with hwmon
 * @lock: mutex to protect measurement values
 * @valid: only 0 before first measurement is taken
 * @last_update: time of last update (jiffies)
 * @temperature: cached temperature measurement value
 * @humidity: cached humidity measurement value
 */
struct sht21 {
	struct device *hwmon_dev;
	struct mutex lock;
	char valid;
	unsigned long last_update;
	int temperature;
	int humidity;
};

/**
 * sht21_temp_ticks_to_millicelsius() - convert raw temperature ticks to
 * milli celsius
 * @ticks: temperature ticks value received from sensor
 */
static inline int sht21_temp_ticks_to_millicelsius(int ticks)
{
	ticks &= ~0x0003; /* clear status bits */
	/*
	 * Formula T = -46.85 + 175.72 * ST / 2^16 from data sheet 6.2,
	 * optimized for integer fixed point (3 digits) arithmetic
	 */
	return ((21965 * ticks) >> 13) - 46850;
}

/**
 * sht21_rh_ticks_to_per_cent_mille() - convert raw humidity ticks to
 * one-thousandths of a percent relative humidity
 * @ticks: humidity ticks value received from sensor
 */
static inline int sht21_rh_ticks_to_per_cent_mille(int ticks)
{
	ticks &= ~0x0003; /* clear status bits */
	/*
	 * Formula RH = -6 + 125 * SRH / 2^16 from data sheet 6.1,
	 * optimized for integer fixed point (3 digits) arithmetic
	 */
	return ((15625 * ticks) >> 13) - 6000;
}

/**
 * sht21_update_measurements() - get updated measurements from device
 * @client: I2C client device
 *
 * Returns 0 on success, else negative errno.
 */
static int sht21_update_measurements(struct i2c_client *client)
{
	int ret = 0;
	struct sht21 *sht21 = i2c_get_clientdata(client);

	mutex_lock(&sht21->lock);
	/*
	 * Data sheet 2.4:
	 * SHT2x should not be active for more than 10% of the time - e.g.
	 * maximum two measurements per second at 12bit accuracy shall be made.
	 */
	if (time_after(jiffies, sht21->last_update + HZ / 2) || !sht21->valid) {
		ret = i2c_smbus_read_word_swapped(client,
						  SHT21_TRIG_T_MEASUREMENT_HM);
		if (ret < 0)
			goto out;
		sht21->temperature = sht21_temp_ticks_to_millicelsius(ret);
		ret = i2c_smbus_read_word_swapped(client,
						  SHT21_TRIG_RH_MEASUREMENT_HM);
		if (ret < 0)
			goto out;
		sht21->humidity = sht21_rh_ticks_to_per_cent_mille(ret);
		sht21->last_update = jiffies;
		sht21->valid = 1;
	}
out:
	mutex_unlock(&sht21->lock);

	return ret >= 0 ? 0 : ret;
}

/**
 * sht21_show_temperature() - show temperature measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to temp1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht21_show_temperature(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sht21 *sht21 = i2c_get_clientdata(client);
	int ret = sht21_update_measurements(client);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", sht21->temperature);
}

/**
 * sht21_show_humidity() - show humidity measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to humidity1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht21_show_humidity(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sht21 *sht21 = i2c_get_clientdata(client);
	int ret = sht21_update_measurements(client);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", sht21->humidity);
}

/* sysfs attributes */
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, sht21_show_temperature,
	NULL, 0);
static SENSOR_DEVICE_ATTR(humidity1_input, S_IRUGO, sht21_show_humidity,
	NULL, 0);

static struct attribute *sht21_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	NULL
};

static const struct attribute_group sht21_attr_group = {
	.attrs = sht21_attributes,
};

/**
 * sht21_probe() - probe device
 * @client: I2C client device
 * @id: device ID
 *
 * Called by the I2C core when an entry in the ID table matches a
 * device's name.
 * Returns 0 on success.
 */
static int __devinit sht21_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct sht21 *sht21;
	int err;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev,
			"adapter does not support SMBus word transactions\n");
		return -ENODEV;
	}

	sht21 = kzalloc(sizeof(*sht21), GFP_KERNEL);
	if (!sht21) {
		dev_dbg(&client->dev, "kzalloc failed\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, sht21);

	mutex_init(&sht21->lock);

	err = sysfs_create_group(&client->dev.kobj, &sht21_attr_group);
	if (err) {
		dev_dbg(&client->dev, "could not create sysfs files\n");
		goto fail_free;
	}
	sht21->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(sht21->hwmon_dev)) {
		dev_dbg(&client->dev, "unable to register hwmon device\n");
		err = PTR_ERR(sht21->hwmon_dev);
		goto fail_remove_sysfs;
	}

	dev_info(&client->dev, "initialized\n");

	return 0;

fail_remove_sysfs:
	sysfs_remove_group(&client->dev.kobj, &sht21_attr_group);
fail_free:
	kfree(sht21);

	return err;
}

/**
 * sht21_remove() - remove device
 * @client: I2C client device
 */
static int __devexit sht21_remove(struct i2c_client *client)
{
	struct sht21 *sht21 = i2c_get_clientdata(client);

	hwmon_device_unregister(sht21->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &sht21_attr_group);
	kfree(sht21);

	return 0;
}

/* Device ID table */
static const struct i2c_device_id sht21_id[] = {
	{ "sht21", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sht21_id);

static struct i2c_driver sht21_driver = {
	.driver.name = "sht21",
	.probe       = sht21_probe,
	.remove      = __devexit_p(sht21_remove),
	.id_table    = sht21_id,
};

module_i2c_driver(sht21_driver);

MODULE_AUTHOR("Urs Fleisch <urs.fleisch@sensirion.com>");
MODULE_DESCRIPTION("Sensirion SHT21 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");
