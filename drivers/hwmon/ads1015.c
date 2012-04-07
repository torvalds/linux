/*
 * ads1015.c - lm_sensors driver for ads1015 12-bit 4-input ADC
 * (C) Copyright 2010
 * Dirk Eibach, Guntermann & Drunck GmbH <eibach@gdsys.de>
 *
 * Based on the ads7828 driver by Steve Hardy.
 *
 * Datasheet available at: http://focus.ti.com/lit/ds/symlink/ads1015.pdf
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include <linux/i2c/ads1015.h>

/* ADS1015 registers */
enum {
	ADS1015_CONVERSION = 0,
	ADS1015_CONFIG = 1,
};

/* PGA fullscale voltages in mV */
static const unsigned int fullscale_table[8] = {
	6144, 4096, 2048, 1024, 512, 256, 256, 256 };

/* Data rates in samples per second */
static const unsigned int data_rate_table[8] = {
	128, 250, 490, 920, 1600, 2400, 3300, 3300 };

#define ADS1015_DEFAULT_CHANNELS 0xff
#define ADS1015_DEFAULT_PGA 2
#define ADS1015_DEFAULT_DATA_RATE 4

struct ads1015_data {
	struct device *hwmon_dev;
	struct mutex update_lock; /* mutex protect updates */
	struct ads1015_channel_data channel_data[ADS1015_CHANNELS];
};

static int ads1015_read_value(struct i2c_client *client, unsigned int channel,
			      int *value)
{
	u16 config;
	s16 conversion;
	struct ads1015_data *data = i2c_get_clientdata(client);
	unsigned int pga = data->channel_data[channel].pga;
	int fullscale;
	unsigned int data_rate = data->channel_data[channel].data_rate;
	unsigned int conversion_time_ms;
	int res;

	mutex_lock(&data->update_lock);

	/* get channel parameters */
	res = i2c_smbus_read_word_swapped(client, ADS1015_CONFIG);
	if (res < 0)
		goto err_unlock;
	config = res;
	fullscale = fullscale_table[pga];
	conversion_time_ms = DIV_ROUND_UP(1000, data_rate_table[data_rate]);

	/* setup and start single conversion */
	config &= 0x001f;
	config |= (1 << 15) | (1 << 8);
	config |= (channel & 0x0007) << 12;
	config |= (pga & 0x0007) << 9;
	config |= (data_rate & 0x0007) << 5;

	res = i2c_smbus_write_word_swapped(client, ADS1015_CONFIG, config);
	if (res < 0)
		goto err_unlock;

	/* wait until conversion finished */
	msleep(conversion_time_ms);
	res = i2c_smbus_read_word_swapped(client, ADS1015_CONFIG);
	if (res < 0)
		goto err_unlock;
	config = res;
	if (!(config & (1 << 15))) {
		/* conversion not finished in time */
		res = -EIO;
		goto err_unlock;
	}

	res = i2c_smbus_read_word_swapped(client, ADS1015_CONVERSION);
	if (res < 0)
		goto err_unlock;
	conversion = res;

	mutex_unlock(&data->update_lock);

	*value = DIV_ROUND_CLOSEST(conversion * fullscale, 0x7ff0);

	return 0;

err_unlock:
	mutex_unlock(&data->update_lock);
	return res;
}

/* sysfs callback function */
static ssize_t show_in(struct device *dev, struct device_attribute *da,
	char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	int in;
	int res;

	res = ads1015_read_value(client, attr->index, &in);

	return (res < 0) ? res : sprintf(buf, "%d\n", in);
}

static const struct sensor_device_attribute ads1015_in[] = {
	SENSOR_ATTR(in0_input, S_IRUGO, show_in, NULL, 0),
	SENSOR_ATTR(in1_input, S_IRUGO, show_in, NULL, 1),
	SENSOR_ATTR(in2_input, S_IRUGO, show_in, NULL, 2),
	SENSOR_ATTR(in3_input, S_IRUGO, show_in, NULL, 3),
	SENSOR_ATTR(in4_input, S_IRUGO, show_in, NULL, 4),
	SENSOR_ATTR(in5_input, S_IRUGO, show_in, NULL, 5),
	SENSOR_ATTR(in6_input, S_IRUGO, show_in, NULL, 6),
	SENSOR_ATTR(in7_input, S_IRUGO, show_in, NULL, 7),
};

/*
 * Driver interface
 */

static int ads1015_remove(struct i2c_client *client)
{
	struct ads1015_data *data = i2c_get_clientdata(client);
	int k;

	hwmon_device_unregister(data->hwmon_dev);
	for (k = 0; k < ADS1015_CHANNELS; ++k)
		device_remove_file(&client->dev, &ads1015_in[k].dev_attr);
	kfree(data);
	return 0;
}

#ifdef CONFIG_OF
static int ads1015_get_channels_config_of(struct i2c_client *client)
{
	struct ads1015_data *data = i2c_get_clientdata(client);
	struct device_node *node;

	if (!client->dev.of_node
	    || !of_get_next_child(client->dev.of_node, NULL))
		return -EINVAL;

	for_each_child_of_node(client->dev.of_node, node) {
		const __be32 *property;
		int len;
		unsigned int channel;
		unsigned int pga = ADS1015_DEFAULT_PGA;
		unsigned int data_rate = ADS1015_DEFAULT_DATA_RATE;

		property = of_get_property(node, "reg", &len);
		if (!property || len != sizeof(int)) {
			dev_err(&client->dev, "invalid reg on %s\n",
				node->full_name);
			continue;
		}

		channel = be32_to_cpup(property);
		if (channel > ADS1015_CHANNELS) {
			dev_err(&client->dev,
				"invalid channel index %d on %s\n",
				channel, node->full_name);
			continue;
		}

		property = of_get_property(node, "ti,gain", &len);
		if (property && len == sizeof(int)) {
			pga = be32_to_cpup(property);
			if (pga > 6) {
				dev_err(&client->dev,
					"invalid gain on %s\n",
					node->full_name);
			}
		}

		property = of_get_property(node, "ti,datarate", &len);
		if (property && len == sizeof(int)) {
			data_rate = be32_to_cpup(property);
			if (data_rate > 7) {
				dev_err(&client->dev,
					"invalid data_rate on %s\n",
					node->full_name);
			}
		}

		data->channel_data[channel].enabled = true;
		data->channel_data[channel].pga = pga;
		data->channel_data[channel].data_rate = data_rate;
	}

	return 0;
}
#endif

static void ads1015_get_channels_config(struct i2c_client *client)
{
	unsigned int k;
	struct ads1015_data *data = i2c_get_clientdata(client);
	struct ads1015_platform_data *pdata = dev_get_platdata(&client->dev);

	/* prefer platform data */
	if (pdata) {
		memcpy(data->channel_data, pdata->channel_data,
		       sizeof(data->channel_data));
		return;
	}

#ifdef CONFIG_OF
	if (!ads1015_get_channels_config_of(client))
		return;
#endif

	/* fallback on default configuration */
	for (k = 0; k < ADS1015_CHANNELS; ++k) {
		data->channel_data[k].enabled = true;
		data->channel_data[k].pga = ADS1015_DEFAULT_PGA;
		data->channel_data[k].data_rate = ADS1015_DEFAULT_DATA_RATE;
	}
}

static int ads1015_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ads1015_data *data;
	int err;
	unsigned int k;

	data = kzalloc(sizeof(struct ads1015_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* build sysfs attribute group */
	ads1015_get_channels_config(client);
	for (k = 0; k < ADS1015_CHANNELS; ++k) {
		if (!data->channel_data[k].enabled)
			continue;
		err = device_create_file(&client->dev, &ads1015_in[k].dev_attr);
		if (err)
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	for (k = 0; k < ADS1015_CHANNELS; ++k)
		device_remove_file(&client->dev, &ads1015_in[k].dev_attr);
	kfree(data);
exit:
	return err;
}

static const struct i2c_device_id ads1015_id[] = {
	{ "ads1015", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads1015_id);

static struct i2c_driver ads1015_driver = {
	.driver = {
		.name = "ads1015",
	},
	.probe = ads1015_probe,
	.remove = ads1015_remove,
	.id_table = ads1015_id,
};

module_i2c_driver(ads1015_driver);

MODULE_AUTHOR("Dirk Eibach <eibach@gdsys.de>");
MODULE_DESCRIPTION("ADS1015 driver");
MODULE_LICENSE("GPL");
