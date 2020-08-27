// SPDX-License-Identifier: GPL-2.0+
/*
 * atlas-ezo-sensor.c - Support for Atlas Scientific EZO sensors
 *
 * Copyright (C) 2020 Konsulko Group
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/iio/iio.h>

#define ATLAS_EZO_DRV_NAME		"atlas-ezo-sensor"
#define ATLAS_CO2_INT_TIME_IN_MS	950

enum {
	ATLAS_CO2_EZO,
};

struct atlas_ezo_device {
	const struct iio_chan_spec *channels;
	int num_channels;
	int delay;
};

struct atlas_ezo_data {
	struct i2c_client *client;
	struct atlas_ezo_device *chip;

	/* lock to avoid multiple concurrent read calls */
	struct mutex lock;

	u8 buffer[8];
};

static const struct iio_chan_spec atlas_co2_ezo_channels[] = {
	{
		.type = IIO_CONCENTRATION,
		.modified = 1,
		.channel2 = IIO_MOD_CO2,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 0,
		.scan_type = {
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
		.delay = ATLAS_CO2_INT_TIME_IN_MS,
	},
};

static int atlas_ezo_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct atlas_ezo_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int ret = 0;

	if (chan->type != IIO_CONCENTRATION)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
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

		ret = kstrtol(data->buffer + 1, 10, &tmp);

		*val = tmp;

		mutex_unlock(&data->lock);

		return ret ? ret : IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 100; /* 0.0001 */
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return ret;
}

static const struct iio_info atlas_info = {
	.read_raw = atlas_ezo_read_raw,
};

static const struct i2c_device_id atlas_ezo_id[] = {
	{ "atlas-co2-ezo", ATLAS_CO2_EZO },
	{}
};
MODULE_DEVICE_TABLE(i2c, atlas_ezo_id);

static const struct of_device_id atlas_ezo_dt_ids[] = {
	{ .compatible = "atlas,co2-ezo", .data = (void *)ATLAS_CO2_EZO, },
	{}
};
MODULE_DEVICE_TABLE(of, atlas_ezo_dt_ids);

static int atlas_ezo_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct atlas_ezo_data *data;
	struct atlas_ezo_device *chip;
	const struct of_device_id *of_id;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	of_id = of_match_device(atlas_ezo_dt_ids, &client->dev);
	if (!of_id)
		chip = &atlas_ezo_devices[id->driver_data];
	else
		chip = &atlas_ezo_devices[(unsigned long)of_id->data];

	indio_dev->info = &atlas_info;
	indio_dev->name = ATLAS_EZO_DRV_NAME;
	indio_dev->channels = chip->channels;
	indio_dev->num_channels = chip->num_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = &client->dev;

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
