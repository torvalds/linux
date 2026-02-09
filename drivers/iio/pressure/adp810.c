// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Akhilesh Patil <akhilesh@ee.iitb.ac.in>
 *
 * Driver for adp810 pressure and temperature sensor
 * Datasheet:
 *   https://aosong.com/userfiles/files/media/Datasheet%20ADP810-Digital.pdf
 */

#include <linux/array_size.h>
#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/types.h>

/*
 * Refer section 5.4 checksum calculation from datasheet.
 * This sensor uses CRC polynomial x^8 + x^5 + x^4 + 1 (0x31)
 */
#define ADP810_CRC8_POLYNOMIAL		0x31

DECLARE_CRC8_TABLE(crc_table);

/*
 * Buffer declaration which holds 9 bytes of measurement data read
 * from the sensor. Use __packed to avoid any paddings, as data sent
 * from the sensor is strictly contiguous 9 bytes.
 */
struct adp810_read_buf {
	__be16 dp;
	u8 dp_crc;
	__be16 tmp;
	u8 tmp_crc;
	__be16 sf;
	u8 sf_crc;
} __packed;

struct adp810_data {
	struct i2c_client *client;
	/* Use lock to synchronize access to device during read sequence */
	struct mutex lock;
};

static int adp810_measure(struct adp810_data *data, struct adp810_read_buf *buf)
{
	struct i2c_client *client = data->client;
	struct device *dev = &client->dev;
	int ret;
	u8 trig_cmd[2] = {0x37, 0x2d};

	/* Send trigger command to the sensor for measurement */
	ret = i2c_master_send(client, trig_cmd, sizeof(trig_cmd));
	if (ret < 0) {
		dev_err(dev, "Error sending trigger command\n");
		return ret;
	}
	if (ret != sizeof(trig_cmd))
		return -EIO;

	/*
	 * Wait for the sensor to acquire data. As per datasheet section 5.3.1,
	 * at least 10ms delay before reading from the sensor is recommended.
	 * Here, we wait for 20ms to have some safe margin on the top
	 * of recommendation and to compensate for any possible variations.
	 */
	msleep(20);

	/* Read sensor values */
	ret = i2c_master_recv(client, (char *)buf, sizeof(*buf));
	if (ret < 0) {
		dev_err(dev, "Error reading from sensor\n");
		return ret;
	}
	if (ret != sizeof(*buf))
		return -EIO;

	/* CRC checks */
	crc8_populate_msb(crc_table, ADP810_CRC8_POLYNOMIAL);
	if (buf->dp_crc != crc8(crc_table, (u8 *)&buf->dp, 0x2, CRC8_INIT_VALUE)) {
		dev_err(dev, "CRC error for pressure\n");
		return -EIO;
	}

	if (buf->tmp_crc != crc8(crc_table, (u8 *)&buf->tmp, 0x2, CRC8_INIT_VALUE)) {
		dev_err(dev, "CRC error for temperature\n");
		return -EIO;
	}

	if (buf->sf_crc != crc8(crc_table, (u8 *)&buf->sf, 0x2, CRC8_INIT_VALUE)) {
		dev_err(dev, "CRC error for scale\n");
		return -EIO;
	}

	return 0;
}

static int adp810_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct adp810_data *data = iio_priv(indio_dev);
	struct device *dev = &data->client->dev;
	struct adp810_read_buf buf = { };
	int ret;

	scoped_guard(mutex, &data->lock) {
		ret = adp810_measure(data, &buf);
		if (ret) {
			dev_err(dev, "Failed to read from device\n");
			return ret;
		}
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_PRESSURE:
			*val = get_unaligned_be16(&buf.dp);
			return IIO_VAL_INT;
		case IIO_TEMP:
			*val = get_unaligned_be16(&buf.tmp);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PRESSURE:
			*val = get_unaligned_be16(&buf.sf);
			return IIO_VAL_INT;
		case IIO_TEMP:
			*val = 200;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info adp810_info = {
	.read_raw = adp810_read_raw,
};

static const struct iio_chan_spec adp810_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int adp810_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct adp810_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	indio_dev->name = "adp810";
	indio_dev->channels = adp810_channels;
	indio_dev->num_channels = ARRAY_SIZE(adp810_channels);
	indio_dev->info = &adp810_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register IIO device\n");

	return 0;
}

static const struct i2c_device_id adp810_id_table[] = {
	{ "adp810" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp810_id_table);

static const struct of_device_id adp810_of_table[] = {
	{ .compatible = "aosong,adp810" },
	{ }
};
MODULE_DEVICE_TABLE(of, adp810_of_table);

static struct i2c_driver adp810_driver = {
	.driver = {
		.name = "adp810",
		.of_match_table = adp810_of_table,
	},
	.probe	= adp810_probe,
	.id_table = adp810_id_table,
};
module_i2c_driver(adp810_driver);

MODULE_AUTHOR("Akhilesh Patil <akhilesh@ee.iitb.ac.in>");
MODULE_DESCRIPTION("Driver for Aosong ADP810 sensor");
MODULE_LICENSE("GPL");
