/* Honeywell HIH-6130/HIH-6131 humidity and temperature sensor driver
 *
 * Copyright (C) 2012 Iain Paton <ipaton0@gmail.com>
 *
 * heavily based on the sht21 driver
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
 * @hwmon_dev: device registered with hwmon
 * @lock: mutex to protect measurement values
 * @valid: only false before first measurement is taken
 * @last_update: time of last update (jiffies)
 * @temperature: cached temperature measurement value
 * @humidity: cached humidity measurement value
 */
struct hih6130 {
	struct device *hwmon_dev;
	struct mutex lock;
	bool valid;
	unsigned long last_update;
	int temperature;
	int humidity;
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
 * @client: I2C client device
 *
 * Returns 0 on success, else negative errno.
 */
static int hih6130_update_measurements(struct i2c_client *client)
{
	int ret = 0;
	int t;
	struct hih6130 *hih6130 = i2c_get_clientdata(client);
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

		/* write to slave address, no data, to request a measurement */
		ret = i2c_master_send(client, tmp, 0);
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
 * hih6130_show_temperature() - show temperature measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to temp1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t hih6130_show_temperature(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hih6130 *hih6130 = i2c_get_clientdata(client);
	int ret = hih6130_update_measurements(client);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", hih6130->temperature);
}

/**
 * hih6130_show_humidity() - show humidity measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to humidity1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t hih6130_show_humidity(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hih6130 *hih6130 = i2c_get_clientdata(client);
	int ret = hih6130_update_measurements(client);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", hih6130->humidity);
}

/* sysfs attributes */
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, hih6130_show_temperature,
	NULL, 0);
static SENSOR_DEVICE_ATTR(humidity1_input, S_IRUGO, hih6130_show_humidity,
	NULL, 0);

static struct attribute *hih6130_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	NULL
};

static const struct attribute_group hih6130_attr_group = {
	.attrs = hih6130_attributes,
};

/**
 * hih6130_probe() - probe device
 * @client: I2C client device
 * @id: device ID
 *
 * Called by the I2C core when an entry in the ID table matches a
 * device's name.
 * Returns 0 on success.
 */
static int __devinit hih6130_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct hih6130 *hih6130;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "adapter does not support true I2C\n");
		return -ENODEV;
	}

	hih6130 = devm_kzalloc(&client->dev, sizeof(*hih6130), GFP_KERNEL);
	if (!hih6130)
		return -ENOMEM;

	i2c_set_clientdata(client, hih6130);

	mutex_init(&hih6130->lock);

	err = sysfs_create_group(&client->dev.kobj, &hih6130_attr_group);
	if (err) {
		dev_dbg(&client->dev, "could not create sysfs files\n");
		return err;
	}

	hih6130->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(hih6130->hwmon_dev)) {
		dev_dbg(&client->dev, "unable to register hwmon device\n");
		err = PTR_ERR(hih6130->hwmon_dev);
		goto fail_remove_sysfs;
	}

	return 0;

fail_remove_sysfs:
	sysfs_remove_group(&client->dev.kobj, &hih6130_attr_group);
	return err;
}

/**
 * hih6130_remove() - remove device
 * @client: I2C client device
 */
static int __devexit hih6130_remove(struct i2c_client *client)
{
	struct hih6130 *hih6130 = i2c_get_clientdata(client);

	hwmon_device_unregister(hih6130->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &hih6130_attr_group);

	return 0;
}

/* Device ID table */
static const struct i2c_device_id hih6130_id[] = {
	{ "hih6130", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hih6130_id);

static struct i2c_driver hih6130_driver = {
	.driver.name = "hih6130",
	.probe       = hih6130_probe,
	.remove      = __devexit_p(hih6130_remove),
	.id_table    = hih6130_id,
};

module_i2c_driver(hih6130_driver);

MODULE_AUTHOR("Iain Paton <ipaton0@gmail.com>");
MODULE_DESCRIPTION("Honeywell HIH-6130 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");
