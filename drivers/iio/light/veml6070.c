/*
 * veml6070.c - Support for Vishay VEML6070 UV A light sensor
 *
 * Copyright 2016 Peter Meerwald-Stadler <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for VEML6070 (7-bit I2C slave addresses 0x38 and 0x39)
 *
 * TODO: integration time, ACK signal
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define VEML6070_DRV_NAME "veml6070"

#define VEML6070_ADDR_CONFIG_DATA_MSB 0x38 /* read: MSB data, write: config */
#define VEML6070_ADDR_DATA_LSB	0x39 /* LSB data */

#define VEML6070_COMMAND_ACK	BIT(5) /* raise interrupt when over threshold */
#define VEML6070_COMMAND_IT	GENMASK(3, 2) /* bit mask integration time */
#define VEML6070_COMMAND_RSRVD	BIT(1) /* reserved, set to 1 */
#define VEML6070_COMMAND_SD	BIT(0) /* shutdown mode when set */

#define VEML6070_IT_10	0x04 /* integration time 1x */

struct veml6070_data {
	struct i2c_client *client1;
	struct i2c_client *client2;
	u8 config;
	struct mutex lock;
};

static int veml6070_read(struct veml6070_data *data)
{
	int ret;
	u8 msb, lsb;

	mutex_lock(&data->lock);

	/* disable shutdown */
	ret = i2c_smbus_write_byte(data->client1,
	    data->config & ~VEML6070_COMMAND_SD);
	if (ret < 0)
		goto out;

	msleep(125 + 10); /* measurement takes up to 125 ms for IT 1x */

	ret = i2c_smbus_read_byte(data->client2); /* read MSB, address 0x39 */
	if (ret < 0)
		goto out;
	msb = ret;

	ret = i2c_smbus_read_byte(data->client1); /* read LSB, address 0x38 */
	if (ret < 0)
		goto out;
	lsb = ret;

	/* shutdown again */
	ret = i2c_smbus_write_byte(data->client1, data->config);
	if (ret < 0)
		goto out;

	ret = (msb << 8) | lsb;

out:
	mutex_unlock(&data->lock);
	return ret;
}

static const struct iio_chan_spec veml6070_channels[] = {
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_UV,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
	{
		.type = IIO_UVINDEX,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
	}
};

static int veml6070_to_uv_index(unsigned val)
{
	/*
	 * conversion of raw UV intensity values to UV index depends on
	 * integration time (IT) and value of the resistor connected to
	 * the RSET pin (default: 270 KOhm)
	 */
	unsigned uvi[11] = {
		187, 373, 560, /* low */
		746, 933, 1120, /* moderate */
		1308, 1494, /* high */
		1681, 1868, 2054}; /* very high */
	int i;

	for (i = 0; i < ARRAY_SIZE(uvi); i++)
		if (val <= uvi[i])
			return i;

	return 11; /* extreme */
}

static int veml6070_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct veml6070_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		ret = veml6070_read(data);
		if (ret < 0)
			return ret;
		if (mask == IIO_CHAN_INFO_PROCESSED)
			*val = veml6070_to_uv_index(ret);
		else
			*val = ret;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info veml6070_info = {
	.read_raw = veml6070_read_raw,
	.driver_module = THIS_MODULE,
};

static int veml6070_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct veml6070_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client1 = client;
	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &veml6070_info;
	indio_dev->channels = veml6070_channels;
	indio_dev->num_channels = ARRAY_SIZE(veml6070_channels);
	indio_dev->name = VEML6070_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data->client2 = i2c_new_dummy(client->adapter, VEML6070_ADDR_DATA_LSB);
	if (!data->client2) {
		dev_err(&client->dev, "i2c device for second chip address failed\n");
		return -ENODEV;
	}

	data->config = VEML6070_IT_10 | VEML6070_COMMAND_RSRVD |
		VEML6070_COMMAND_SD;
	ret = i2c_smbus_write_byte(data->client1, data->config);
	if (ret < 0)
		goto fail;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto fail;

	return ret;

fail:
	i2c_unregister_device(data->client2);
	return ret;
}

static int veml6070_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct veml6070_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	i2c_unregister_device(data->client2);

	return 0;
}

static const struct i2c_device_id veml6070_id[] = {
	{ "veml6070", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml6070_id);

static struct i2c_driver veml6070_driver = {
	.driver = {
		.name   = VEML6070_DRV_NAME,
	},
	.probe  = veml6070_probe,
	.remove  = veml6070_remove,
	.id_table = veml6070_id,
};

module_i2c_driver(veml6070_driver);

MODULE_AUTHOR("Peter Meerwald-Stadler <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Vishay VEML6070 UV A light sensor driver");
MODULE_LICENSE("GPL");
