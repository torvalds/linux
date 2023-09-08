// SPDX-License-Identifier: GPL-2.0
/*
 * All Sensors DLH series low voltage digital pressure sensors
 *
 * Copyright (c) 2019 AVL DiTEST GmbH
 *   Tomislav Denis <tomislav.denis@avl.com>
 *
 * Datasheet: https://www.allsensors.com/cad/DS-0355_Rev_B.PDF
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <asm/unaligned.h>

/* Commands */
#define DLH_START_SINGLE    0xAA

/* Status bits */
#define DLH_STATUS_OK       0x40

/* DLH  data format */
#define DLH_NUM_READ_BYTES  7
#define DLH_NUM_DATA_BYTES  3
#define DLH_NUM_PR_BITS     24
#define DLH_NUM_TEMP_BITS   24

/* DLH  timings */
#define DLH_SINGLE_DUT_MS   5

enum dhl_ids {
	dlhl60d,
	dlhl60g,
};

struct dlh_info {
	u8 osdig;           /* digital offset factor */
	unsigned int fss;   /* full scale span (inch H2O) */
};

struct dlh_state {
	struct i2c_client *client;
	struct dlh_info info;
	bool use_interrupt;
	struct completion completion;
	u8 rx_buf[DLH_NUM_READ_BYTES];
};

static struct dlh_info dlh_info_tbl[] = {
	[dlhl60d] = {
		.osdig = 2,
		.fss = 120,
	},
	[dlhl60g] = {
		.osdig = 10,
		.fss = 60,
	},
};


static int dlh_cmd_start_single(struct dlh_state *st)
{
	int ret;

	ret = i2c_smbus_write_byte(st->client, DLH_START_SINGLE);
	if (ret)
		dev_err(&st->client->dev,
			"%s: I2C write byte failed\n", __func__);

	return ret;
}

static int dlh_cmd_read_data(struct dlh_state *st)
{
	int ret;

	ret = i2c_master_recv(st->client, st->rx_buf, DLH_NUM_READ_BYTES);
	if (ret < 0) {
		dev_err(&st->client->dev,
			"%s: I2C read block failed\n", __func__);
		return ret;
	}

	if (st->rx_buf[0] != DLH_STATUS_OK) {
		dev_err(&st->client->dev,
			"%s: invalid status 0x%02x\n", __func__, st->rx_buf[0]);
		return -EBUSY;
	}

	return 0;
}

static int dlh_start_capture_and_read(struct dlh_state *st)
{
	int ret;

	if (st->use_interrupt)
		reinit_completion(&st->completion);

	ret = dlh_cmd_start_single(st);
	if (ret)
		return ret;

	if (st->use_interrupt) {
		ret = wait_for_completion_timeout(&st->completion,
			msecs_to_jiffies(DLH_SINGLE_DUT_MS));
		if (!ret) {
			dev_err(&st->client->dev,
				"%s: conversion timed out\n", __func__);
			return -ETIMEDOUT;
		}
	} else {
		mdelay(DLH_SINGLE_DUT_MS);
	}

	return dlh_cmd_read_data(st);
}

static int dlh_read_direct(struct dlh_state *st,
	unsigned int *pressure, unsigned int *temperature)
{
	int ret;

	ret = dlh_start_capture_and_read(st);
	if (ret)
		return ret;

	*pressure = get_unaligned_be24(&st->rx_buf[1]);
	*temperature = get_unaligned_be24(&st->rx_buf[4]);

	return 0;
}

static int dlh_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *channel, int *value,
	int *value2, long mask)
{
	struct dlh_state *st = iio_priv(indio_dev);
	unsigned int pressure, temperature;
	int ret;
	s64 tmp;
	s32 rem;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = dlh_read_direct(st, &pressure, &temperature);
		iio_device_release_direct_mode(indio_dev);
		if (ret)
			return ret;

		switch (channel->type) {
		case IIO_PRESSURE:
			*value = pressure;
			return IIO_VAL_INT;

		case IIO_TEMP:
			*value = temperature;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (channel->type) {
		case IIO_PRESSURE:
			tmp = div_s64(125LL * st->info.fss * 24909 * 100,
				1 << DLH_NUM_PR_BITS);
			tmp = div_s64_rem(tmp, 1000000000LL, &rem);
			*value = tmp;
			*value2 = rem;
			return IIO_VAL_INT_PLUS_NANO;

		case IIO_TEMP:
			*value = 125 * 1000;
			*value2 = DLH_NUM_TEMP_BITS;
			return IIO_VAL_FRACTIONAL_LOG2;

		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (channel->type) {
		case IIO_PRESSURE:
			*value = -125 * st->info.fss * 24909;
			*value2 = 100 * st->info.osdig * 100000;
			return IIO_VAL_FRACTIONAL;

		case IIO_TEMP:
			*value = -40 * 1000;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static const struct iio_info dlh_info = {
	.read_raw = dlh_read_raw,
};

static const struct iio_chan_spec dlh_channels[] = {
	{
		.type = IIO_PRESSURE,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type =
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = DLH_NUM_PR_BITS,
			.storagebits = 32,
			.shift = 8,
			.endianness = IIO_BE,
		},
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type =
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = DLH_NUM_TEMP_BITS,
			.storagebits = 32,
			.shift = 8,
			.endianness = IIO_BE,
		},
	}
};

static irqreturn_t dlh_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct dlh_state *st = iio_priv(indio_dev);
	int ret;
	unsigned int chn, i = 0;
	__be32 tmp_buf[2];

	ret = dlh_start_capture_and_read(st);
	if (ret)
		goto out;

	for_each_set_bit(chn, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		memcpy(tmp_buf + i,
			&st->rx_buf[1] + chn * DLH_NUM_DATA_BYTES,
			DLH_NUM_DATA_BYTES);
		i++;
	}

	iio_push_to_buffers(indio_dev, tmp_buf);

out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t dlh_interrupt(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct dlh_state *st = iio_priv(indio_dev);

	complete(&st->completion);

	return IRQ_HANDLED;
};

static int dlh_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct dlh_state *st;
	struct iio_dev *indio_dev;
	int ret;

	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_I2C | I2C_FUNC_SMBUS_WRITE_BYTE)) {
		dev_err(&client->dev,
			"adapter doesn't support required i2c functionality\n");
		return -EOPNOTSUPP;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev) {
		dev_err(&client->dev, "failed to allocate iio device\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, indio_dev);

	st = iio_priv(indio_dev);
	st->info = dlh_info_tbl[id->driver_data];
	st->client = client;
	st->use_interrupt = false;

	indio_dev->name = id->name;
	indio_dev->info = &dlh_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels =  dlh_channels;
	indio_dev->num_channels = ARRAY_SIZE(dlh_channels);

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
			dlh_interrupt, NULL,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			id->name, indio_dev);
		if (ret) {
			dev_err(&client->dev, "failed to allocate threaded irq");
			return ret;
		}

		st->use_interrupt = true;
		init_completion(&st->completion);
	}

	ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev,
		NULL, &dlh_trigger_handler, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to setup iio buffer\n");
		return ret;
	}

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		dev_err(&client->dev, "failed to register iio device\n");

	return ret;
}

static const struct of_device_id dlh_of_match[] = {
	{ .compatible = "asc,dlhl60d" },
	{ .compatible = "asc,dlhl60g" },
	{}
};
MODULE_DEVICE_TABLE(of, dlh_of_match);

static const struct i2c_device_id dlh_id[] = {
	{ "dlhl60d",    dlhl60d },
	{ "dlhl60g",    dlhl60g },
	{}
};
MODULE_DEVICE_TABLE(i2c, dlh_id);

static struct i2c_driver dlh_driver = {
	.driver = {
		.name = "dlhl60d",
		.of_match_table = dlh_of_match,
	},
	.probe_new = dlh_probe,
	.id_table = dlh_id,
};
module_i2c_driver(dlh_driver);

MODULE_AUTHOR("Tomislav Denis <tomislav.denis@avl.com>");
MODULE_DESCRIPTION("Driver for All Sensors DLH series pressure sensors");
MODULE_LICENSE("GPL v2");
