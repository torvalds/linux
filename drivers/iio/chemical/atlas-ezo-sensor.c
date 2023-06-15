// SPDX-License-Identifier: GPL-2.0+
/*
 * atlas-ezo-sensor.c - Support for Atlas Scientific EZO sensors
 *
 * Copyright (C) 2020 Konsulko Group
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/err.h>
#include <linux/i2c.h>

#include <linux/iio/iio.h>

#define ATLAS_EZO_DRV_NAME		"atlas-ezo-sensor"
#define ATLAS_INT_TIME_IN_MS		950
#define ATLAS_INT_HUM_TIME_IN_MS	350

enum {
	ATLAS_CO2_EZO,
	ATLAS_O2_EZO,
	ATLAS_HUM_EZO,
};

struct atlas_ezo_device {
	const struct iio_chan_spec *channels;
	int num_channels;
	int delay;
};

struct atlas_ezo_data {
	struct i2c_client *client;
	const struct atlas_ezo_device *chip;

	/* lock to avoid multiple concurrent read calls */
	struct mutex lock;

	u8 buffer[8];
};

#define ATLAS_CONCENTRATION_CHANNEL(_modifier) \
	{ \
		.type = IIO_CONCENTRATION, \
		.modified = 1,\
		.channel2 = _modifier, \
		.info_mask_separate = \
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), \
		.scan_index = 0, \
		.scan_type =  { \
			.sign = 'u', \
			.realbits = 32, \
			.storagebits = 32, \
			.endianness = IIO_CPU, \
		}, \
	}

static const struct iio_chan_spec atlas_co2_ezo_channels[] = {
	ATLAS_CONCENTRATION_CHANNEL(IIO_MOD_CO2),
};

static const struct iio_chan_spec atlas_o2_ezo_channels[] = {
	ATLAS_CONCENTRATION_CHANNEL(IIO_MOD_O2),
};

static const struct iio_chan_spec atlas_hum_ezo_channels[] = {
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 0,
		.scan_type =  {
			.sign = 'u',
			.realbits = 32,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
};

static struct atlas_ezo_device atlas_ezo_devices[] = {
	[ATLAS_CO2_EZO] = {
		.channels = atlas_co2_ezo_channels,
		.num_channels = 1,
		.delay = ATLAS_INT_TIME_IN_MS,
	},
	[ATLAS_O2_EZO] = {
		.channels = atlas_o2_ezo_channels,
		.num_channels = 1,
		.delay = ATLAS_INT_TIME_IN_MS,
	},
	[ATLAS_HUM_EZO] = {
		.channels = atlas_hum_ezo_channels,
		.num_channels = 1,
		.delay = ATLAS_INT_HUM_TIME_IN_MS,
	},
};

static void atlas_ezo_sanitize(char *buf)
{
	char *ptr = strchr(buf, '.');

	if (!ptr)
		return;

	memmove(ptr, ptr + 1, strlen(ptr));
}

static int atlas_ezo_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct atlas_ezo_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;

	if (chan->type != IIO_CONCENTRATION)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int ret;
		long tmp;

		mutex_lock(&data->lock);

		tmp = i2c_smbus_write_byte(client, 'R');

		if (tmp < 0) {
			mutex_unlock(&data->lock);
			return tmp;
		}

		msleep(data->chip->delay);

		tmp = i2c_master_recv(client, data->buffer, sizeof(data->buffer));

		if (tmp < 0 || data->buffer[0] != 1) {
			mutex_unlock(&data->lock);
			return -EBUSY;
		}

		/* removing floating point for fixed number representation */
		atlas_ezo_sanitize(data->buffer + 2);

		ret = kstrtol(data->buffer + 1, 10, &tmp);

		*val = tmp;

		mutex_unlock(&data->lock);

		return ret ? ret : IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_HUMIDITYRELATIVE:
			*val = 10;
			return IIO_VAL_INT;
		case IIO_CONCENTRATION:
			break;
		default:
			return -EINVAL;
		}

		/* IIO_CONCENTRATION modifiers */
		switch (chan->channel2) {
		case IIO_MOD_CO2:
			*val = 0;
			*val2 = 100; /* 0.0001 */
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MOD_O2:
			*val = 100;
			return IIO_VAL_INT;
		}
		return -EINVAL;
	}

	return 0;
}

static const struct iio_info atlas_info = {
	.read_raw = atlas_ezo_read_raw,
};

static const struct i2c_device_id atlas_ezo_id[] = {
	{ "atlas-co2-ezo", (kernel_ulong_t)&atlas_ezo_devices[ATLAS_CO2_EZO] },
	{ "atlas-o2-ezo", (kernel_ulong_t)&atlas_ezo_devices[ATLAS_O2_EZO] },
	{ "atlas-hum-ezo", (kernel_ulong_t)&atlas_ezo_devices[ATLAS_HUM_EZO] },
	{}
};
MODULE_DEVICE_TABLE(i2c, atlas_ezo_id);

static const struct of_device_id atlas_ezo_dt_ids[] = {
	{ .compatible = "atlas,co2-ezo", .data = &atlas_ezo_devices[ATLAS_CO2_EZO], },
	{ .compatible = "atlas,o2-ezo", .data = &atlas_ezo_devices[ATLAS_O2_EZO], },
	{ .compatible = "atlas,hum-ezo", .data = &atlas_ezo_devices[ATLAS_HUM_EZO], },
	{}
};
MODULE_DEVICE_TABLE(of, atlas_ezo_dt_ids);

static int atlas_ezo_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	const struct atlas_ezo_device *chip;
	struct atlas_ezo_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	if (dev_fwnode(&client->dev))
		chip = device_get_match_data(&client->dev);
	else
		chip = (const struct atlas_ezo_device *)id->driver_data;
	if (!chip)
		return -EINVAL;

	indio_dev->info = &atlas_info;
	indio_dev->name = ATLAS_EZO_DRV_NAME;
	indio_dev->channels = chip->channels;
	indio_dev->num_channels = chip->num_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data = iio_priv(indio_dev);
	data->client = client;
	data->chip = chip;
	mutex_init(&data->lock);

	return devm_iio_device_register(&client->dev, indio_dev);
};

static struct i2c_driver atlas_ezo_driver = {
	.driver = {
		.name	= ATLAS_EZO_DRV_NAME,
		.of_match_table	= atlas_ezo_dt_ids,
	},
	.probe		= atlas_ezo_probe,
	.id_table	= atlas_ezo_id,
};
module_i2c_driver(atlas_ezo_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("Atlas Scientific EZO sensors");
MODULE_LICENSE("GPL");
