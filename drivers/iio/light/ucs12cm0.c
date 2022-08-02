// SPDX-License-Identifier: GPL-2.0
/*
 * UCS12CM0 illuminance and correlated color temperature sensor
 *
 * Copyright (C) 2022-2025 ROCKCHIP.
 * Author: Jason Zhang <jason.zhang@rock-chips.com>
 *
 * IIO driver for UCS12CM0 (7-bit I2C slave address 0x38)
 */
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/util_macros.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/buffer.h>

#define UCS12CM0_SYS_CTRL 0x00
#define UCS12CM0_INT_CTRL 0x01
#define UCS12CM0_INT_FLAG 0x02
#define UCS12CM0_WAIT 0x03
#define UCS12CM0_ALS_GAIN 0x04
#define UCS12CM0_ALS_TIME 0x05
#define UCS12CM0_PS_LED 0x06
#define UCS12CM0_PS_GAIN 0x07
#define UCS12CM0_PS_PULSE 0x08
#define UCS12CM0_PS_TIME 0x09
#define UCS12CM0_PS_AVERAGE 0x0a
#define UCS12CM0_PS_PERSIST 0x0b
#define UCS12CM0_ALS_THDLL 0x0c
#define UCS12CM0_ALS_THDLH 0x0d
#define UCS12CM0_ALS_THDHL 0x0e
#define UCS12CM0_ALS_THDHH 0x0f
#define UCS12CM0_PS_THDLL 0x10
#define UCS12CM0_PS_THDLH 0x11
#define UCS12CM0_PS_THDHL 0x12
#define UCS12CM0_PS_THDHH 0x13
#define UCS12CM0_PS_OFFSET_L 0x14
#define UCS12CM0_PS_OFFSET_H 0x15
#define UCS12CM0_PS_DATA_L 0x18
#define UCS12CM0_PS_DATA_H 0x19
#define UCS12CM0_CLS_R_DATA_L 0x1c
#define UCS12CM0_CLS_R_DATA_H 0x1d
#define UCS12CM0_CLS_G_DATA_L 0x1e
#define UCS12CM0_CLS_G_DATA_H 0x1f
#define UCS12CM0_CLS_B_DATA_L 0x20
#define UCS12CM0_CLS_B_DATA_H 0x21
#define UCS12CM0_CLS_W_DATA_L 0x22
#define UCS12CM0_CLS_W_DATA_H 0x23
#define UCS12CM0_IR_DATA_L 0x24
#define UCS12CM0_IR_DATA_H 0x25
#define UCS12CM0_ID 0xbc

/* bis of the SYS_CTRL register */
#define UCS12CM0_EN_CLS BIT(0)	/* Enables CLS function */
#define UCS12CM0_EN_IR BIT(1)	/* Enables IR function */
#define UCS12CM0_EN_FRST BIT(5)	/* Enables Brown Out Reset circuit */
#define UCS12CM0_EN_WAIT BIT(6)	/* Waiting time will be inserted between two
				 * measurements
				 */
#define UCS12CM0_SWRST BIT(7)	/* Software reset. Reset all register to
				 * default value
				 */

/* bis of the INT_FLAG register */
#define UCS12CM0_INT_CLS BIT(0)	/* CLS Interrupt flag. It correlation with
				 * sensor data and CLS high/low threshold.
				 * Write zero to clear the flag.
				 */
#define UCS12CM0_INT_DATA BIT(6)/* It shows if any data is invalid after
				 * completion of each conversion cycle. This
				 * bit is read-only.
				 */
#define UCS12CM0_INT_POR BIT(7)	/* Power-On-Reset Interrupt flag trigger the
				 * INT pin when the flag sets to one. Write
				 * zero to clear the flag.
				 */

#define UCS12CM0_CCT_CHANNEL(_si, _mod) { \
		.type = IIO_CCT, \
		.address = _si, \
		.channel2 = _mod, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
				      BIT(IIO_CHAN_INFO_SCALE) | \
				      BIT(IIO_CHAN_INFO_AVERAGE_RAW), \
		.modified = 1, \
		.scan_index = _si, \
		.scan_type = { \
			.sign = 'u', \
			.realbits = 16, \
			.storagebits = 16, \
		}, \
	}

enum {
	UCS12CM0_CCT_READ,
	UCS12CM0_CCT_GREEN,
	UCS12CM0_CCT_BLUE,
	UCS12CM0_CCT_WHITE,
	UCS12CM0_CCT_ALL
};

struct ucs12cm0_scan {
	u16 chans[4];
	/* Ensure natural alignment of timestamp */
	s64 timestamp;
};

struct ucs12cm0_data {
	struct i2c_client *client;
	int calibrated;
	u32 raw[UCS12CM0_CCT_ALL];
	u32 average[UCS12CM0_CCT_ALL];
};

static const u8 ucs12cm0_chan_regs[UCS12CM0_CCT_ALL] = {
	UCS12CM0_CLS_R_DATA_L,
	UCS12CM0_CLS_G_DATA_L,
	UCS12CM0_CLS_B_DATA_L,
	UCS12CM0_CLS_W_DATA_L
};

static const struct iio_chan_spec ucs12cm0_channels[] = {
	UCS12CM0_CCT_CHANNEL(UCS12CM0_CCT_READ, IIO_MOD_LIGHT_RED),
	UCS12CM0_CCT_CHANNEL(UCS12CM0_CCT_GREEN, IIO_MOD_LIGHT_GREEN),
	UCS12CM0_CCT_CHANNEL(UCS12CM0_CCT_BLUE, IIO_MOD_LIGHT_BLUE),
	UCS12CM0_CCT_CHANNEL(UCS12CM0_CCT_WHITE, IIO_MOD_LIGHT_CLEAR),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int ucs12cm0_read(struct i2c_client *client, u8 cmd, void *databuf,
		       u8 len)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.len = sizeof(cmd),
			.buf = (u8 *) &cmd
		}, {
			.addr = client->addr,
			.len = len,
			.buf = databuf,
			.flags = I2C_M_RD
		}
	};
	int ret;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		dev_err(&client->dev, "failed reading register 0x%04x\n", cmd);

	return ret;
}

static int ucs12cm0_read_byte(struct i2c_client *client, u8 cmd)
{
	u8 data;
	int ret;

	ret = ucs12cm0_read(client, cmd, &data, sizeof(data));
	if (ret < 0)
		return ret;

	return data;
}

static int ucs12cm0_read_word(struct i2c_client *client, u8 cmd)
{
	__le16 data;
	int ret;

	ret = ucs12cm0_read(client, cmd, &data, sizeof(data));
	if (ret < 0)
		return ret;

	return le16_to_cpu(data);
}

static int ucs12cm0_read_average(struct ucs12cm0_data *data, int chan)
{
	u8 cmd;
	int sum = 0;
	int average;
	int i;
	int ret;

	cmd = ucs12cm0_chan_regs[chan];
	for (i = 0; i < 10; ++i) {
		ret = ucs12cm0_read_word(data->client, cmd);
		if (ret < 0)
			return ret;

		sum += ret;
	}

	average = sum / 10;

	return average;
}

static int ucs12cm0_write_byte(struct i2c_client *client, u8 cmd, u8 val)
{
	u8 buf[2];
	struct i2c_msg msgs[1] = {
		{
			.addr = client->addr,
			.len = sizeof(buf),
			.buf = (u8 *) &buf
		}
	};
	int ret;

	buf[0] = cmd;
	buf[1] = val;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0) {
		dev_err(&client->dev, "failed writing register 0x%04x\n", cmd);
		return ret;
	}

	return 0;
}

static int ucs12cm0_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct ucs12cm0_data *data = iio_priv(indio_dev);
	u8 cmd;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		cmd = ucs12cm0_chan_regs[chan->address];
		ret = ucs12cm0_read_word(data->client, cmd);
		if (ret < 0)
			return ret;
		*val = ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		if (data->calibrated) {
			*val = data->average[chan->address];
			*val2 = data->raw[chan->address];
		} else {
			*val = 1;
			*val2 = 1;
		}

		return IIO_VAL_FRACTIONAL;

	case IIO_CHAN_INFO_AVERAGE_RAW:
		*val = data->average[chan->address];

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int ucs12cm0_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct ucs12cm0_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		data->raw[chan->address] = val;

		return 0;

	case IIO_CHAN_INFO_SCALE:
		return -EPERM;

	case IIO_CHAN_INFO_AVERAGE_RAW:
		return -EPERM;

	default:
		return -EINVAL;
	}
}

static int ucs12cm0_write_raw_get_fmt(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_AVERAGE_RAW:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static ssize_t start_calibrating_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ucs12cm0_data *data = iio_priv(indio_dev);
	int i;
	long chans = 0;
	int ret;

	if (!strncmp(buf, "red", 3)) {
		set_bit(UCS12CM0_CCT_READ, &chans);
	} else if (!strncmp(buf, "green", 5)) {
		set_bit(UCS12CM0_CCT_GREEN, &chans);
	} else if (!strncmp(buf, "blue", 4)) {
		set_bit(UCS12CM0_CCT_BLUE, &chans);
	} else if (!strncmp(buf, "white", 5)) {
		set_bit(UCS12CM0_CCT_WHITE, &chans);
	} else if (!strncmp(buf, "all", 3)) {
		set_bit(UCS12CM0_CCT_READ, &chans);
		set_bit(UCS12CM0_CCT_GREEN, &chans);
		set_bit(UCS12CM0_CCT_BLUE, &chans);
		set_bit(UCS12CM0_CCT_WHITE, &chans);
	} else {
		return -EINVAL;
	}

	for_each_set_bit(i, &chans, UCS12CM0_CCT_ALL) {
		if (!data->raw[i])
			return -EPERM;

		dev_info(&data->client->dev, "raw = %d\n",
			 data->raw[i]);
	}

	for_each_set_bit(i, &chans, UCS12CM0_CCT_ALL) {
		ret = ucs12cm0_read_average(data, i);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			return -EINVAL;

		data->average[i] = ret;

		dev_info(&data->client->dev, "average = %d\n",
			 data->average[i]);
	}

	/*
	 * TODO: store the calibration data in the ROM because UCS12CM0
	 * doesn't have any rom-related register.
	 */
	data->calibrated = 1;

	return len;
}

static IIO_DEVICE_ATTR_WO(start_calibrating, 0);

static struct attribute *ucs12cm0_attributes[] = {
	&iio_dev_attr_start_calibrating.dev_attr.attr,
	NULL
};

static const struct attribute_group ucs12cm0_attribute_group = {
	.attrs = ucs12cm0_attributes,
};

static const struct iio_info ucs12cm0_info = {
	.read_raw = ucs12cm0_read_raw,
	.write_raw = ucs12cm0_write_raw,
	.write_raw_get_fmt = ucs12cm0_write_raw_get_fmt,
	.attrs = &ucs12cm0_attribute_group,
};

/**
 * ucs12cm0_active - enable or disable the CLS
 * @client: the i2c client used by the driver.
 * @enable: enable/disable the CLS of ucs12cm0.
 *
 * Returns negative errno, else the number of messages executed.
 */
static int ucs12cm0_active(struct i2c_client *client, int enable)
{
	u8 val;
	int ret = 0;

	ret = ucs12cm0_read_byte(client, UCS12CM0_SYS_CTRL);
	if (ret < 0)
		goto out;

	val = ret;
	if (enable)
		val |= UCS12CM0_EN_CLS;
	else
		val &= ~UCS12CM0_EN_CLS;

	ret = ucs12cm0_write_byte(client, UCS12CM0_SYS_CTRL, val);
	if (ret < 0)
		dev_err(&client->dev, "Failed to active sensor\n");

out:
	return ret;
}

static int ucs12cm0_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ucs12cm0_data *data = iio_priv(indio_dev);

	return ucs12cm0_active(data->client, 1);
}

static int ucs12cm0_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ucs12cm0_data *data = iio_priv(indio_dev);

	return ucs12cm0_active(data->client, 0);
}

static const struct iio_buffer_setup_ops ucs12cm0_buffer_setup_ops = {
	.postenable = ucs12cm0_buffer_postenable,
	.predisable = ucs12cm0_buffer_predisable,
};

static int ucs12cm0_init(struct ucs12cm0_data *data)
{
	int ret;
	struct i2c_client *client = data->client;

	ret = ucs12cm0_write_byte(client, UCS12CM0_SYS_CTRL, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_INT_CTRL, 0x13);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_INT_FLAG, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_WAIT, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_ALS_GAIN, 0x84);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_ALS_TIME, 0x33);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_LED, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_GAIN, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_PULSE, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_TIME, 0x0f);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_AVERAGE, 0x0f);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_PERSIST, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_PS_OFFSET_L, 0x0000);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_ALS_THDHL, 0xff);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_ALS_THDHH, 0xff);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_ALS_THDLL, 0x00);
	if (ret < 0)
		goto err;

	ret = ucs12cm0_write_byte(client, UCS12CM0_ALS_THDLH, 0x00);
	if (ret < 0)
		goto err;

	return 0;

err:
	return ret;
}

static irqreturn_t ucs12cm0_interrupt_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct ucs12cm0_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	struct ucs12cm0_scan scan;
	int ret;

	ret = ucs12cm0_read_byte(client, UCS12CM0_INT_FLAG);
	if (ret < 0)
		goto out;

	if (ret & UCS12CM0_INT_CLS) {
		if (ucs12cm0_read(client, UCS12CM0_CLS_R_DATA_L, scan.chans,
				  sizeof(scan.chans)) < 0)
			goto clear_irq;

		iio_push_to_buffers_with_timestamp(indio_dev, &scan,
						   ktime_get_boottime_ns());
	}

clear_irq:
	if (ret & UCS12CM0_INT_CLS) {
		ret &= ~UCS12CM0_INT_CLS;
		ucs12cm0_write_byte(client, UCS12CM0_INT_FLAG, ret);
	}

	if (ret & UCS12CM0_INT_POR) {
		ret &= ~UCS12CM0_INT_POR;
		ucs12cm0_write_byte(client, UCS12CM0_INT_FLAG, ret);
	}

out:
	return IRQ_HANDLED;
}

static int ucs12cm0_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct ucs12cm0_data *data;
	struct iio_dev *indio_dev;
	struct iio_buffer *buffer;
	u32 type;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	buffer = devm_iio_kfifo_allocate(&client->dev);
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(indio_dev, buffer);

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->info = &ucs12cm0_info;
	indio_dev->channels = ucs12cm0_channels;
	indio_dev->num_channels = ARRAY_SIZE(ucs12cm0_channels);
	indio_dev->name = "ucs12cm0";
	indio_dev->modes = (INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE);
	indio_dev->setup_ops = &ucs12cm0_buffer_setup_ops;

	ret = ucs12cm0_init(data);
	if (ret < 0)
		return ret;

	if (client->irq <= 0) {
		dev_err(&client->dev, "no valid irq defined\n");
		return -EINVAL;
	}

	type = irqd_get_trigger_type(irq_get_irq_data(client->irq));
	if (type != IRQF_TRIGGER_LOW && type != IRQF_TRIGGER_FALLING) {
		dev_err(&client->dev,
			"unsupported IRQ trigger specified (%x)\n", type);
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, ucs12cm0_interrupt_handler,
					type | IRQF_ONESHOT, "ucs12cm0_irq",
					indio_dev);
	if (ret) {
		dev_err(&client->dev, "request irq (%d) failed\n",
			client->irq);
		return ret;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id ucs12cm0_of_match[] = {
	{ .compatible = "ultracapteur,ucs12cm0", },
	{ },
};
MODULE_DEVICE_TABLE(of, ucs12cm0_of_match);

static const struct i2c_device_id ucs12cm0_id[] = {
	{ "ucs12cm0", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ucs12cm0_id);

static int ucs12cm0_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ucs12cm0_data *data = iio_priv(indio_dev);
	int ret;

	ret = ucs12cm0_init(data);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct dev_pm_ops ucs12cm0_pm_ops = {
	.resume  = ucs12cm0_resume,
};

static struct i2c_driver ucs12cm0_driver = {
	.driver = {
		.name = "ucs12cm0",
		.of_match_table = ucs12cm0_of_match,
		.pm = &ucs12cm0_pm_ops,
	},
	.probe  = ucs12cm0_probe,
	.id_table = ucs12cm0_id,
};

module_i2c_driver(ucs12cm0_driver);

MODULE_AUTHOR("Jason Zhang <jason.zhang@rock-chips.com>");
MODULE_DESCRIPTION("UCS12CM0 illuminance and correlated color temperature sensor driver");
MODULE_LICENSE("GPL");
