// SPDX-License-Identifier: GPL-2.0-only
/*
 * hmc6352.c - Honeywell Compass Driver
 *
 * Copyright (C) 2009 Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/nospec.h>

static DEFINE_MUTEX(compass_mutex);

static int compass_command(struct i2c_client *c, u8 cmd)
{
	int ret = i2c_master_send(c, &cmd, 1);
	if (ret < 0)
		dev_warn(&c->dev, "command '%c' failed.\n", cmd);
	return ret;
}

static int compass_store(struct device *dev, const char *buf, size_t count,
			const char *map)
{
	struct i2c_client *c = to_i2c_client(dev);
	int ret;
	unsigned long val;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val >= strlen(map))
		return -EINVAL;
	val = array_index_nospec(val, strlen(map));
	mutex_lock(&compass_mutex);
	ret = compass_command(c, map[val]);
	mutex_unlock(&compass_mutex);
	if (ret < 0)
		return ret;
	return count;
}

static ssize_t compass_calibration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return compass_store(dev, buf, count, "EC");
}

static ssize_t compass_power_mode_store(struct device *dev,
		struct device_attribute *attr, const  char *buf, size_t count)
{
	return compass_store(dev, buf, count, "SW");
}

static ssize_t compass_heading_data_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char i2c_data[2];
	int ret;

	mutex_lock(&compass_mutex);
	ret = compass_command(client, 'A');
	if (ret != 1) {
		mutex_unlock(&compass_mutex);
		return ret;
	}
	msleep(10); /* sending 'A' cmd we need to wait for 7-10 millisecs */
	ret = i2c_master_recv(client, i2c_data, 2);
	mutex_unlock(&compass_mutex);
	if (ret < 0) {
		dev_warn(dev, "i2c read data cmd failed\n");
		return ret;
	}
	ret = (i2c_data[0] << 8) | i2c_data[1];
	return sprintf(buf, "%d.%d\n", ret/10, ret%10);
}


static DEVICE_ATTR(heading0_input, S_IRUGO, compass_heading_data_show, NULL);
static DEVICE_ATTR(calibration, S_IWUSR, NULL, compass_calibration_store);
static DEVICE_ATTR(power_state, S_IWUSR, NULL, compass_power_mode_store);

static struct attribute *mid_att_compass[] = {
	&dev_attr_heading0_input.attr,
	&dev_attr_calibration.attr,
	&dev_attr_power_state.attr,
	NULL
};

static const struct attribute_group m_compass_gr = {
	.name = "hmc6352",
	.attrs = mid_att_compass
};

static int hmc6352_probe(struct i2c_client *client)
{
	int res;

	res = sysfs_create_group(&client->dev.kobj, &m_compass_gr);
	if (res) {
		dev_err(&client->dev, "device_create_file failed\n");
		return res;
	}
	dev_info(&client->dev, "%s HMC6352 compass chip found\n",
							client->name);
	return 0;
}

static void hmc6352_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &m_compass_gr);
}

static const struct i2c_device_id hmc6352_id[] = {
	{ "hmc6352", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, hmc6352_id);

static struct i2c_driver hmc6352_driver = {
	.driver = {
		.name = "hmc6352",
	},
	.probe_new = hmc6352_probe,
	.remove = hmc6352_remove,
	.id_table = hmc6352_id,
};

module_i2c_driver(hmc6352_driver);

MODULE_AUTHOR("Kalhan Trisal <kalhan.trisal@intel.com");
MODULE_DESCRIPTION("hmc6352 Compass Driver");
MODULE_LICENSE("GPL v2");
