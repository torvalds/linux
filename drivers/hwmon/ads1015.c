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
#include <linux/of_device.h>
#include <linux/of.h>

#include <linux/platform_data/ads1015.h>

/* ADS1015 registers */
enum {
	ADS1015_CONVERSION = 0,
	ADS1015_CONFIG = 1,
};

/* PGA fullscale voltages in mV */
static const unsigned int fullscale_table[8] = {
	6144, 4096, 2048, 1024, 512, 256, 256, 256 };

/* Data rates in samples per second */
static const unsigned int data_rate_table_1015[8] = {
	128, 250, 490, 920, 1600, 2400, 3300, 3300
};

static const unsigned int data_rate_table_1115[8] = {
	8, 16, 32, 64, 128, 250, 475, 860
};

#define ADS1015_DEFAULT_CHANNELS 0xff
#define ADS1015_DEFAULT_PGA 2
#define ADS1015_DEFAULT_DATA_RATE 4

enum ads1015_chips {
	ads1015,
	ads1115,
};

struct ads1015_data {
	struct device *hwmon_dev;
	struct mutex update_lock; /* mutex protect updates */
	struct ads1015_channel_data channel_data[ADS1015_CHANNELS];
	enum ads1015_chips id;
};

static int ads1015_read_adc(struct i2c_client *client, unsigned int channel)
{
	u16 config;
	struct ads1015_data *data = i2c_get_clientdata(client);
	unsigned int pga = data->channel_data[channel].pga;
	unsigned int data_rate = data->channel_data[channel].data_rate;
	unsigned int conversion_time_ms;
	const unsigned int * const rate_table = data->id == ads1115 ?
		data_rate_table_1115 : data_rate_table_1015;
	int res;

	mutex_lock(&data->update_lock);

	/* get channel parameters */
	res = i2c_smbus_read_word_swapped(client, ADS1015_CONFIG);
	if (res < 0)
		goto err_unlock;
	config = res;
	conversion_time_ms = DIV_ROUND_UP(1000, rate_table[data_rate]);

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

err_unlock:
	mutex_unlock(&data->update_lock);
	return res;
}

static int ads1015_reg_to_mv(struct i2c_client *client, unsigned int channel,
			     s16 reg)
{
	struct ads1015_data *data = i2c_get_clientdata(client);
	unsigned int pga = data->channel_data[channel].pga;
	int fullscale = fullscale_table[pga];
	const int mask = data->id == ads1115 ? 0x7fff : 0x7ff0;

	return DIV_ROUND_CLOSEST(reg * fullscale, mask);
}

/* sysfs callback function */
static ssize_t in_show(struct device *dev, struct device_attribute *da,
		       char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	int res;
	int index = attr->index;

	res = ads1015_read_adc(client, index);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", ads1015_reg_to_mv(client, index, res));
}

static const struct sensor_device_attribute ads1015_in[] = {
	SENSOR_ATTR_RO(in0_input, in, 0),
	SENSOR_ATTR_RO(in1_input, in, 1),
	SENSOR_ATTR_RO(in2_input, in, 2),
	SENSOR_ATTR_RO(in3_input, in, 3),
	SENSOR_ATTR_RO(in4_input, in, 4),
	SENSOR_ATTR_RO(in5_input, in, 5),
	SENSOR_ATTR_RO(in6_input, in, 6),
	SENSOR_ATTR_RO(in7_input, in, 7),
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
		u32 pval;
		unsigned int channel;
		unsigned int pga = ADS1015_DEFAULT_PGA;
		unsigned int data_rate = ADS1015_DEFAULT_DATA_RATE;

		if (of_property_read_u32(node, "reg", &pval)) {
			dev_err(&client->dev, "invalid reg on %pOF\n", node);
			continue;
		}

		channel = pval;
		if (channel >= ADS1015_CHANNELS) {
			dev_err(&client->dev,
				"invalid channel index %d on %pOF\n",
				channel, node);
			continue;
		}

		if (!of_property_read_u32(node, "ti,gain", &pval)) {
			pga = pval;
			if (pga > 6) {
				dev_err(&client->dev, "invalid gain on %pOF\n",
					node);
				return -EINVAL;
			}
		}

		if (!of_property_read_u32(node, "ti,datarate", &pval)) {
			data_rate = pval;
			if (data_rate > 7) {
				dev_err(&client->dev,
					"invalid data_rate on %pOF\n", node);
				return -EINVAL;
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

	data = devm_kzalloc(&client->dev, sizeof(struct ads1015_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (client->dev.of_node)
		data->id = (enum ads1015_chips)
			of_device_get_match_data(&client->dev);
	else
		data->id = id->driver_data;
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
	return err;
}

static const struct i2c_device_id ads1015_id[] = {
	{ "ads1015",  ads1015},
	{ "ads1115",  ads1115},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads1015_id);

static const struct of_device_id __maybe_unused ads1015_of_match[] = {
	{
		.compatible = "ti,ads1015",
		.data = (void *)ads1015
	},
	{
		.compatible = "ti,ads1115",
		.data = (void *)ads1115
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ads1015_of_match);

static struct i2c_driver ads1015_driver = {
	.driver = {
		.name = "ads1015",
		.of_match_table = of_match_ptr(ads1015_of_match),
	},
	.probe = ads1015_probe,
	.remove = ads1015_remove,
	.id_table = ads1015_id,
};

module_i2c_driver(ads1015_driver);

MODULE_AUTHOR("Dirk Eibach <eibach@gdsys.de>");
MODULE_DESCRIPTION("ADS1015 driver");
MODULE_LICENSE("GPL");
