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

#define ADS1015_CONFIG_CHANNELS 8
#define ADS1015_DEFAULT_CHANNELS 0xff

struct ads1015_data {
	struct device *hwmon_dev;
	struct mutex update_lock; /* mutex protect updates */
};

static s32 ads1015_read_reg(struct i2c_client *client, unsigned int reg)
{
	s32 data = i2c_smbus_read_word_data(client, reg);

	return (data < 0) ? data : swab16(data);
}

static s32 ads1015_write_reg(struct i2c_client *client, unsigned int reg,
			     u16 val)
{
	return i2c_smbus_write_word_data(client, reg, swab16(val));
}

static int ads1015_read_value(struct i2c_client *client, unsigned int channel,
			      int *value)
{
	u16 config;
	s16 conversion;
	unsigned int pga;
	int fullscale;
	unsigned int k;
	struct ads1015_data *data = i2c_get_clientdata(client);
	int res;

	mutex_lock(&data->update_lock);

	/* get fullscale voltage */
	res = ads1015_read_reg(client, ADS1015_CONFIG);
	if (res < 0)
		goto err_unlock;
	config = res;
	pga = (config >> 9) & 0x0007;
	fullscale = fullscale_table[pga];

	/* set channel and start single conversion */
	config &= ~(0x0007 << 12);
	config |= (1 << 15) | (1 << 8) | (channel & 0x0007) << 12;

	/* wait until conversion finished */
	res = ads1015_write_reg(client, ADS1015_CONFIG, config);
	if (res < 0)
		goto err_unlock;
	for (k = 0; k < 5; ++k) {
		msleep(1);
		res = ads1015_read_reg(client, ADS1015_CONFIG);
		if (res < 0)
			goto err_unlock;
		config = res;
		if (config & (1 << 15))
			break;
	}
	if (k == 5) {
		res = -EIO;
		goto err_unlock;
	}

	res = ads1015_read_reg(client, ADS1015_CONVERSION);
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
	for (k = 0; k < ADS1015_CONFIG_CHANNELS; ++k)
		device_remove_file(&client->dev, &ads1015_in[k].dev_attr);
	kfree(data);
	return 0;
}

static unsigned int ads1015_get_exported_channels(struct i2c_client *client)
{
	struct ads1015_platform_data *pdata = dev_get_platdata(&client->dev);
#ifdef CONFIG_OF
	struct device_node *np = client->dev.of_node;
	const __be32 *of_channels;
	int of_channels_size;
#endif

	/* prefer platform data */
	if (pdata)
		return pdata->exported_channels;

#ifdef CONFIG_OF
	/* fallback on OF */
	of_channels = of_get_property(np, "exported-channels",
				      &of_channels_size);
	if (of_channels && (of_channels_size == sizeof(*of_channels)))
		return be32_to_cpup(of_channels);
#endif

	/* fallback on default configuration */
	return ADS1015_DEFAULT_CHANNELS;
}

static int ads1015_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ads1015_data *data;
	int err;
	unsigned int exported_channels;
	unsigned int k;

	data = kzalloc(sizeof(struct ads1015_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* build sysfs attribute group */
	exported_channels = ads1015_get_exported_channels(client);
	for (k = 0; k < ADS1015_CONFIG_CHANNELS; ++k) {
		if (!(exported_channels & (1<<k)))
			continue;
		err = device_create_file(&client->dev, &ads1015_in[k].dev_attr);
		if (err)
			goto exit_free;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	for (k = 0; k < ADS1015_CONFIG_CHANNELS; ++k)
		device_remove_file(&client->dev, &ads1015_in[k].dev_attr);
exit_free:
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

static int __init sensors_ads1015_init(void)
{
	return i2c_add_driver(&ads1015_driver);
}

static void __exit sensors_ads1015_exit(void)
{
	i2c_del_driver(&ads1015_driver);
}

MODULE_AUTHOR("Dirk Eibach <eibach@gdsys.de>");
MODULE_DESCRIPTION("ADS1015 driver");
MODULE_LICENSE("GPL");

module_init(sensors_ads1015_init);
module_exit(sensors_ads1015_exit);
