// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Anshul Dalal <anshulusr@gmail.com>
 *
 * Driver for Aosong AGS02MA
 *
 * Datasheet:
 *   https://asairsensors.com/wp-content/uploads/2021/09/AGS02MA.pdf
 * Product Page:
 *   http://www.aosong.com/m/en/products-33.html
 */

#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include <linux/iio/iio.h>

#define AGS02MA_TVOC_READ_REG		   0x00
#define AGS02MA_VERSION_REG		   0x11

#define AGS02MA_VERSION_PROCESSING_DELAY   30
#define AGS02MA_TVOC_READ_PROCESSING_DELAY 1500

#define AGS02MA_CRC8_INIT		   0xff
#define AGS02MA_CRC8_POLYNOMIAL		   0x31

DECLARE_CRC8_TABLE(ags02ma_crc8_table);

struct ags02ma_data {
	struct i2c_client *client;
};

struct ags02ma_reading {
	__be32 data;
	u8 crc;
} __packed;

static int ags02ma_register_read(struct i2c_client *client, u8 reg, u16 delay,
				 u32 *val)
{
	int ret;
	u8 crc;
	struct ags02ma_reading read_buffer;

	ret = i2c_master_send(client, &reg, sizeof(reg));
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to send data to register 0x%x: %d", reg, ret);
		return ret;
	}

	/* Processing Delay, Check Table 7.7 in the datasheet */
	msleep_interruptible(delay);

	ret = i2c_master_recv(client, (u8 *)&read_buffer, sizeof(read_buffer));
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to receive from register 0x%x: %d", reg, ret);
		return ret;
	}

	crc = crc8(ags02ma_crc8_table, (u8 *)&read_buffer.data,
		   sizeof(read_buffer.data), AGS02MA_CRC8_INIT);
	if (crc != read_buffer.crc) {
		dev_err(&client->dev, "CRC error\n");
		return -EIO;
	}

	*val = be32_to_cpu(read_buffer.data);
	return 0;
}

static int ags02ma_read_raw(struct iio_dev *iio_device,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	int ret;
	struct ags02ma_data *data = iio_priv(iio_device);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ags02ma_register_read(data->client, AGS02MA_TVOC_READ_REG,
					    AGS02MA_TVOC_READ_PROCESSING_DELAY,
					    val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* The sensor reads data as ppb */
		*val = 0;
		*val2 = 100;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ags02ma_info = {
	.read_raw = ags02ma_read_raw,
};

static const struct iio_chan_spec ags02ma_channel = {
	.type = IIO_CONCENTRATION,
	.channel2 = IIO_MOD_VOC,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE),
};

static int ags02ma_probe(struct i2c_client *client)
{
	int ret;
	struct ags02ma_data *data;
	struct iio_dev *indio_dev;
	u32 version;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	crc8_populate_msb(ags02ma_crc8_table, AGS02MA_CRC8_POLYNOMIAL);

	ret = ags02ma_register_read(client, AGS02MA_VERSION_REG,
				    AGS02MA_VERSION_PROCESSING_DELAY, &version);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret,
			      "Failed to read device version\n");
	dev_dbg(&client->dev, "Aosong AGS02MA, Version: 0x%x", version);

	data = iio_priv(indio_dev);
	data->client = client;
	indio_dev->info = &ags02ma_info;
	indio_dev->channels = &ags02ma_channel;
	indio_dev->num_channels = 1;
	indio_dev->name = "ags02ma";

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id ags02ma_id_table[] = {
	{ "ags02ma" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ags02ma_id_table);

static const struct of_device_id ags02ma_of_table[] = {
	{ .compatible = "aosong,ags02ma" },
	{ }
};
MODULE_DEVICE_TABLE(of, ags02ma_of_table);

static struct i2c_driver ags02ma_driver = {
	.driver = {
		.name = "ags02ma",
		.of_match_table = ags02ma_of_table,
	},
	.id_table = ags02ma_id_table,
	.probe = ags02ma_probe,
};
module_i2c_driver(ags02ma_driver);

MODULE_AUTHOR("Anshul Dalal <anshulusr@gmail.com>");
MODULE_DESCRIPTION("Aosong AGS02MA TVOC Driver");
MODULE_LICENSE("GPL");
