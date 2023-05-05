// SPDX-License-Identifier: GPL-2.0-or-later
/* Sensirion SHTC1 humidity and temperature sensor driver
 *
 * Copyright (C) 2014 Sensirion AG, Switzerland
 * Author: Johannes Winkelmann <johannes.winkelmann@sensirion.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_data/shtc1.h>
#include <linux/of.h>

/* commands (high precision mode) */
static const unsigned char shtc1_cmd_measure_blocking_hpm[]    = { 0x7C, 0xA2 };
static const unsigned char shtc1_cmd_measure_nonblocking_hpm[] = { 0x78, 0x66 };

/* commands (low precision mode) */
static const unsigned char shtc1_cmd_measure_blocking_lpm[]    = { 0x64, 0x58 };
static const unsigned char shtc1_cmd_measure_nonblocking_lpm[] = { 0x60, 0x9c };

/* command for reading the ID register */
static const unsigned char shtc1_cmd_read_id_reg[]             = { 0xef, 0xc8 };

/*
 * constants for reading the ID register
 * SHTC1: 0x0007 with mask 0x003f
 * SHTW1: 0x0007 with mask 0x003f
 * SHTC3: 0x0807 with mask 0x083f
 */
#define SHTC3_ID      0x0807
#define SHTC3_ID_MASK 0x083f
#define SHTC1_ID      0x0007
#define SHTC1_ID_MASK 0x003f

/* delays for non-blocking i2c commands, both in us */
#define SHTC1_NONBLOCKING_WAIT_TIME_HPM  14400
#define SHTC1_NONBLOCKING_WAIT_TIME_LPM   1000
#define SHTC3_NONBLOCKING_WAIT_TIME_HPM  12100
#define SHTC3_NONBLOCKING_WAIT_TIME_LPM    800

#define SHTC1_CMD_LENGTH      2
#define SHTC1_RESPONSE_LENGTH 6

enum shtcx_chips {
	shtc1,
	shtc3,
};

struct shtc1_data {
	struct i2c_client *client;
	struct mutex update_lock;
	bool valid;
	unsigned long last_updated; /* in jiffies */

	const unsigned char *command;
	unsigned int nonblocking_wait_time; /* in us */

	struct shtc1_platform_data setup;
	enum shtcx_chips chip;

	int temperature; /* 1000 * temperature in dgr C */
	int humidity; /* 1000 * relative humidity in %RH */
};

static int shtc1_update_values(struct i2c_client *client,
			       struct shtc1_data *data,
			       char *buf, int bufsize)
{
	int ret = i2c_master_send(client, data->command, SHTC1_CMD_LENGTH);
	if (ret != SHTC1_CMD_LENGTH) {
		dev_err(&client->dev, "failed to send command: %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	/*
	 * In blocking mode (clock stretching mode) the I2C bus
	 * is blocked for other traffic, thus the call to i2c_master_recv()
	 * will wait until the data is ready. For non blocking mode, we
	 * have to wait ourselves.
	 */
	if (!data->setup.blocking_io)
		usleep_range(data->nonblocking_wait_time,
			     data->nonblocking_wait_time + 1000);

	ret = i2c_master_recv(client, buf, bufsize);
	if (ret != bufsize) {
		dev_err(&client->dev, "failed to read values: %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

/* sysfs attributes */
static struct shtc1_data *shtc1_update_client(struct device *dev)
{
	struct shtc1_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned char buf[SHTC1_RESPONSE_LENGTH];
	int val;
	int ret = 0;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ / 10) || !data->valid) {
		ret = shtc1_update_values(client, data, buf, sizeof(buf));
		if (ret)
			goto out;

		/*
		 * From datasheet:
		 * T = -45 + 175 * ST / 2^16
		 * RH = 100 * SRH / 2^16
		 *
		 * Adapted for integer fixed point (3 digit) arithmetic.
		 */
		val = be16_to_cpup((__be16 *)buf);
		data->temperature = ((21875 * val) >> 13) - 45000;
		val = be16_to_cpup((__be16 *)(buf + 3));
		data->humidity = ((12500 * val) >> 13);

		data->last_updated = jiffies;
		data->valid = true;
	}

out:
	mutex_unlock(&data->update_lock);

	return ret == 0 ? data : ERR_PTR(ret);
}

static ssize_t temp1_input_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct shtc1_data *data = shtc1_update_client(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->temperature);
}

static ssize_t humidity1_input_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct shtc1_data *data = shtc1_update_client(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->humidity);
}

static DEVICE_ATTR_RO(temp1_input);
static DEVICE_ATTR_RO(humidity1_input);

static struct attribute *shtc1_attrs[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_humidity1_input.attr,
	NULL
};

ATTRIBUTE_GROUPS(shtc1);

static void shtc1_select_command(struct shtc1_data *data)
{
	if (data->setup.high_precision) {
		data->command = data->setup.blocking_io ?
				shtc1_cmd_measure_blocking_hpm :
				shtc1_cmd_measure_nonblocking_hpm;
		data->nonblocking_wait_time = (data->chip == shtc1) ?
				SHTC1_NONBLOCKING_WAIT_TIME_HPM :
				SHTC3_NONBLOCKING_WAIT_TIME_HPM;
	} else {
		data->command = data->setup.blocking_io ?
				shtc1_cmd_measure_blocking_lpm :
				shtc1_cmd_measure_nonblocking_lpm;
		data->nonblocking_wait_time = (data->chip == shtc1) ?
				SHTC1_NONBLOCKING_WAIT_TIME_LPM :
				SHTC3_NONBLOCKING_WAIT_TIME_LPM;
	}
}

static const struct i2c_device_id shtc1_id[];

static int shtc1_probe(struct i2c_client *client)
{
	int ret;
	u16 id_reg;
	char id_reg_buf[2];
	struct shtc1_data *data;
	struct device *hwmon_dev;
	enum shtcx_chips chip = i2c_match_id(shtc1_id, client)->driver_data;
	struct i2c_adapter *adap = client->adapter;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;

	if (!i2c_check_functionality(adap, I2C_FUNC_I2C)) {
		dev_err(dev, "plain i2c transactions not supported\n");
		return -ENODEV;
	}

	ret = i2c_master_send(client, shtc1_cmd_read_id_reg, SHTC1_CMD_LENGTH);
	if (ret != SHTC1_CMD_LENGTH) {
		dev_err(dev, "could not send read_id_reg command: %d\n", ret);
		return ret < 0 ? ret : -ENODEV;
	}
	ret = i2c_master_recv(client, id_reg_buf, sizeof(id_reg_buf));
	if (ret != sizeof(id_reg_buf)) {
		dev_err(dev, "could not read ID register: %d\n", ret);
		return -ENODEV;
	}

	id_reg = be16_to_cpup((__be16 *)id_reg_buf);
	if (chip == shtc3) {
		if ((id_reg & SHTC3_ID_MASK) != SHTC3_ID) {
			dev_err(dev, "SHTC3 ID register does not match\n");
			return -ENODEV;
		}
	} else if ((id_reg & SHTC1_ID_MASK) != SHTC1_ID) {
		dev_err(dev, "SHTC1 ID register does not match\n");
		return -ENODEV;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->setup.blocking_io = false;
	data->setup.high_precision = true;
	data->client = client;
	data->chip = chip;

	if (np) {
		data->setup.blocking_io = of_property_read_bool(np, "sensirion,blocking-io");
		data->setup.high_precision = !of_property_read_bool(np, "sensicon,low-precision");
	} else {
		if (client->dev.platform_data)
			data->setup = *(struct shtc1_platform_data *)dev->platform_data;
	}

	shtc1_select_command(data);
	mutex_init(&data->update_lock);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name,
							   data,
							   shtc1_groups);
	if (IS_ERR(hwmon_dev))
		dev_dbg(dev, "unable to register hwmon device\n");

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/* device ID table */
static const struct i2c_device_id shtc1_id[] = {
	{ "shtc1", shtc1 },
	{ "shtw1", shtc1 },
	{ "shtc3", shtc3 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, shtc1_id);

static const struct of_device_id shtc1_of_match[] = {
	{ .compatible = "sensirion,shtc1" },
	{ .compatible = "sensirion,shtw1" },
	{ .compatible = "sensirion,shtc3" },
	{ }
};
MODULE_DEVICE_TABLE(of, shtc1_of_match);

static struct i2c_driver shtc1_i2c_driver = {
	.driver = {
		.name = "shtc1",
		.of_match_table = shtc1_of_match,
	},
	.probe        = shtc1_probe,
	.id_table     = shtc1_id,
};

module_i2c_driver(shtc1_i2c_driver);

MODULE_AUTHOR("Johannes Winkelmann <johannes.winkelmann@sensirion.com>");
MODULE_DESCRIPTION("Sensirion SHTC1 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");
