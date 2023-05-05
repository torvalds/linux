// SPDX-License-Identifier: GPL-2.0-or-later
/* Sensirion SHT21 humidity and temperature sensor driver
 *
 * Copyright (C) 2010 Urs Fleisch <urs.fleisch@sensirion.com>
 *
 * Data sheet available at https://www.sensirion.com/file/datasheet_sht21
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
#include <linux/jiffies.h>

/* I2C command bytes */
#define SHT21_TRIG_T_MEASUREMENT_HM  0xe3
#define SHT21_TRIG_RH_MEASUREMENT_HM 0xe5
#define SHT21_READ_SNB_CMD1 0xFA
#define SHT21_READ_SNB_CMD2 0x0F
#define SHT21_READ_SNAC_CMD1 0xFC
#define SHT21_READ_SNAC_CMD2 0xC9

/**
 * struct sht21 - SHT21 device specific data
 * @client: I2C client device
 * @lock: mutex to protect measurement values
 * @last_update: time of last update (jiffies)
 * @temperature: cached temperature measurement value
 * @humidity: cached humidity measurement value
 * @valid: only 0 before first measurement is taken
 * @eic: cached electronic identification code text
 */
struct sht21 {
	struct i2c_client *client;
	struct mutex lock;
	unsigned long last_update;
	int temperature;
	int humidity;
	bool valid;
	char eic[18];
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
 * @dev: device
 *
 * Returns 0 on success, else negative errno.
 */
static int sht21_update_measurements(struct device *dev)
{
	int ret = 0;
	struct sht21 *sht21 = dev_get_drvdata(dev);
	struct i2c_client *client = sht21->client;

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
		sht21->valid = true;
	}
out:
	mutex_unlock(&sht21->lock);

	return ret >= 0 ? 0 : ret;
}

/**
 * sht21_temperature_show() - show temperature measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to temp1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht21_temperature_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct sht21 *sht21 = dev_get_drvdata(dev);
	int ret;

	ret = sht21_update_measurements(dev);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", sht21->temperature);
}

/**
 * sht21_humidity_show() - show humidity measurement value in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where measurement values are written to
 *
 * Will be called on read access to humidity1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht21_humidity_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct sht21 *sht21 = dev_get_drvdata(dev);
	int ret;

	ret = sht21_update_measurements(dev);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", sht21->humidity);
}

static ssize_t eic_read(struct sht21 *sht21)
{
	struct i2c_client *client = sht21->client;
	u8 tx[2];
	u8 rx[8];
	u8 eic[8];
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = tx,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 8,
			.buf = rx,
		},
	};
	int ret;

	tx[0] = SHT21_READ_SNB_CMD1;
	tx[1] = SHT21_READ_SNB_CMD2;
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		goto out;
	eic[2] = rx[0];
	eic[3] = rx[2];
	eic[4] = rx[4];
	eic[5] = rx[6];

	tx[0] = SHT21_READ_SNAC_CMD1;
	tx[1] = SHT21_READ_SNAC_CMD2;
	msgs[1].len = 6;
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		goto out;
	eic[0] = rx[3];
	eic[1] = rx[4];
	eic[6] = rx[0];
	eic[7] = rx[1];

	ret = snprintf(sht21->eic, sizeof(sht21->eic),
		       "%02x%02x%02x%02x%02x%02x%02x%02x\n",
		       eic[0], eic[1], eic[2], eic[3],
		       eic[4], eic[5], eic[6], eic[7]);
out:
	if (ret < 0)
		sht21->eic[0] = 0;

	return ret;
}

/**
 * eic_show() - show Electronic Identification Code in sysfs
 * @dev: device
 * @attr: device attribute
 * @buf: sysfs buffer (PAGE_SIZE) where EIC is written
 *
 * Will be called on read access to eic sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t eic_show(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct sht21 *sht21 = dev_get_drvdata(dev);
	int ret;

	ret = sizeof(sht21->eic) - 1;
	mutex_lock(&sht21->lock);
	if (!sht21->eic[0])
		ret = eic_read(sht21);
	if (ret > 0)
		memcpy(buf, sht21->eic, ret);
	mutex_unlock(&sht21->lock);
	return ret;
}

/* sysfs attributes */
static SENSOR_DEVICE_ATTR_RO(temp1_input, sht21_temperature, 0);
static SENSOR_DEVICE_ATTR_RO(humidity1_input, sht21_humidity, 0);
static DEVICE_ATTR_RO(eic);

static struct attribute *sht21_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	&dev_attr_eic.attr,
	NULL
};

ATTRIBUTE_GROUPS(sht21);

static int sht21_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct sht21 *sht21;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev,
			"adapter does not support SMBus word transactions\n");
		return -ENODEV;
	}

	sht21 = devm_kzalloc(dev, sizeof(*sht21), GFP_KERNEL);
	if (!sht21)
		return -ENOMEM;

	sht21->client = client;

	mutex_init(&sht21->lock);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   sht21, sht21_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
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
	.id_table    = sht21_id,
};

module_i2c_driver(sht21_driver);

MODULE_AUTHOR("Urs Fleisch <urs.fleisch@sensirion.com>");
MODULE_DESCRIPTION("Sensirion SHT21 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");
