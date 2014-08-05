/*
 * itg3200_core.c -- support InvenSense ITG3200
 *                   Digital 3-Axis Gyroscope driver
 *
 * Copyright (c) 2011 Christian Strobel <christian.strobel@iis.fraunhofer.de>
 * Copyright (c) 2011 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2012 Thorsten Nowak <thorsten.nowak@iis.fraunhofer.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO:
 * - Support digital low pass filter
 * - Support power management
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>

#include <linux/iio/gyro/itg3200.h>


int itg3200_write_reg_8(struct iio_dev *indio_dev,
		u8 reg_address, u8 val)
{
	struct itg3200 *st = iio_priv(indio_dev);

	return i2c_smbus_write_byte_data(st->i2c, 0x80 | reg_address, val);
}

int itg3200_read_reg_8(struct iio_dev *indio_dev,
		u8 reg_address, u8 *val)
{
	struct itg3200 *st = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(st->i2c, reg_address);
	if (ret < 0)
		return ret;
	*val = ret;
	return 0;
}

static int itg3200_read_reg_s16(struct iio_dev *indio_dev, u8 lower_reg_address,
		int *val)
{
	struct itg3200 *st = iio_priv(indio_dev);
	struct i2c_client *client = st->i2c;
	int ret;
	s16 out;

	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = client->flags,
			.len = 1,
			.buf = (char *)&lower_reg_address,
		},
		{
			.addr = client->addr,
			.flags = client->flags | I2C_M_RD,
			.len = 2,
			.buf = (char *)&out,
		},
	};

	lower_reg_address |= 0x80;
	ret = i2c_transfer(client->adapter, msg, 2);
	be16_to_cpus(&out);
	*val = out;

	return (ret == 2) ? 0 : ret;
}

static int itg3200_read_raw(struct iio_dev *indio_dev,
		const struct iio_chan_spec *chan,
		int *val, int *val2, long info)
{
	int ret = 0;
	u8 reg;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		reg = (u8)chan->address;
		ret = itg3200_read_reg_s16(indio_dev, reg, val);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		if (chan->type == IIO_TEMP)
			*val2 = 1000000000/280;
		else
			*val2 = 1214142; /* (1 / 14,375) * (PI / 180) */
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OFFSET:
		/* Only the temperature channel has an offset */
		*val = 23000;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static ssize_t itg3200_read_frequency(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	int ret, sps;
	u8 val;

	ret = itg3200_read_reg_8(indio_dev, ITG3200_REG_DLPF, &val);
	if (ret)
		return ret;

	sps = (val & ITG3200_DLPF_CFG_MASK) ? 1000 : 8000;

	ret = itg3200_read_reg_8(indio_dev, ITG3200_REG_SAMPLE_RATE_DIV, &val);
	if (ret)
		return ret;

	sps /= val + 1;

	return sprintf(buf, "%d\n", sps);
}

static ssize_t itg3200_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	unsigned val;
	int ret;
	u8 t;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);

	ret = itg3200_read_reg_8(indio_dev, ITG3200_REG_DLPF, &t);
	if (ret)
		goto err_ret;

	if (val == 0) {
		ret = -EINVAL;
		goto err_ret;
	}
	t = ((t & ITG3200_DLPF_CFG_MASK) ? 1000u : 8000u) / val - 1;

	ret = itg3200_write_reg_8(indio_dev, ITG3200_REG_SAMPLE_RATE_DIV, t);

err_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

/*
 * Reset device and internal registers to the power-up-default settings
 * Use the gyro clock as reference, as suggested by the datasheet
 */
static int itg3200_reset(struct iio_dev *indio_dev)
{
	struct itg3200 *st = iio_priv(indio_dev);
	int ret;

	dev_dbg(&st->i2c->dev, "reset device");

	ret = itg3200_write_reg_8(indio_dev,
			ITG3200_REG_POWER_MANAGEMENT,
			ITG3200_RESET);
	if (ret) {
		dev_err(&st->i2c->dev, "error resetting device");
		goto error_ret;
	}

	/* Wait for PLL (1ms according to datasheet) */
	udelay(1500);

	ret = itg3200_write_reg_8(indio_dev,
			ITG3200_REG_IRQ_CONFIG,
			ITG3200_IRQ_ACTIVE_HIGH |
			ITG3200_IRQ_PUSH_PULL |
			ITG3200_IRQ_LATCH_50US_PULSE |
			ITG3200_IRQ_LATCH_CLEAR_ANY);

	if (ret)
		dev_err(&st->i2c->dev, "error init device");

error_ret:
	return ret;
}

/* itg3200_enable_full_scale() - Disables the digital low pass filter */
static int itg3200_enable_full_scale(struct iio_dev *indio_dev)
{
	u8 val;
	int ret;

	ret = itg3200_read_reg_8(indio_dev, ITG3200_REG_DLPF, &val);
	if (ret)
		goto err_ret;

	val |= ITG3200_DLPF_FS_SEL_2000;
	return itg3200_write_reg_8(indio_dev, ITG3200_REG_DLPF, val);

err_ret:
	return ret;
}

static int itg3200_initial_setup(struct iio_dev *indio_dev)
{
	struct itg3200 *st = iio_priv(indio_dev);
	int ret;
	u8 val;

	ret = itg3200_read_reg_8(indio_dev, ITG3200_REG_ADDRESS, &val);
	if (ret)
		goto err_ret;

	if (((val >> 1) & 0x3f) != 0x34) {
		dev_err(&st->i2c->dev, "invalid reg value 0x%02x", val);
		ret = -ENXIO;
		goto err_ret;
	}

	ret = itg3200_reset(indio_dev);
	if (ret)
		goto err_ret;

	ret = itg3200_enable_full_scale(indio_dev);
err_ret:
	return ret;
}

#define ITG3200_ST						\
	{ .sign = 's', .realbits = 16, .storagebits = 16, .endianness = IIO_BE }

#define ITG3200_GYRO_CHAN(_mod) { \
	.type = IIO_ANGL_VEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.address = ITG3200_REG_GYRO_ ## _mod ## OUT_H, \
	.scan_index = ITG3200_SCAN_GYRO_ ## _mod, \
	.scan_type = ITG3200_ST, \
}

static const struct iio_chan_spec itg3200_channels[] = {
	{
		.type = IIO_TEMP,
		.channel2 = IIO_NO_MOD,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SCALE),
		.address = ITG3200_REG_TEMP_OUT_H,
		.scan_index = ITG3200_SCAN_TEMP,
		.scan_type = ITG3200_ST,
	},
	ITG3200_GYRO_CHAN(X),
	ITG3200_GYRO_CHAN(Y),
	ITG3200_GYRO_CHAN(Z),
	IIO_CHAN_SOFT_TIMESTAMP(ITG3200_SCAN_ELEMENTS),
};

/* IIO device attributes */
static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO, itg3200_read_frequency,
		itg3200_write_frequency);

static struct attribute *itg3200_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL
};

static const struct attribute_group itg3200_attribute_group = {
	.attrs = itg3200_attributes,
};

static const struct iio_info itg3200_info = {
	.attrs = &itg3200_attribute_group,
	.read_raw = &itg3200_read_raw,
	.driver_module = THIS_MODULE,
};

static const unsigned long itg3200_available_scan_masks[] = { 0xffffffff, 0x0 };

static int itg3200_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret;
	struct itg3200 *st;
	struct iio_dev *indio_dev;

	dev_dbg(&client->dev, "probe I2C dev with IRQ %i", client->irq);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	i2c_set_clientdata(client, indio_dev);
	st->i2c = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = client->dev.driver->name;
	indio_dev->channels = itg3200_channels;
	indio_dev->num_channels = ARRAY_SIZE(itg3200_channels);
	indio_dev->available_scan_masks = itg3200_available_scan_masks;
	indio_dev->info = &itg3200_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = itg3200_buffer_configure(indio_dev);
	if (ret)
		return ret;

	if (client->irq) {
		ret = itg3200_probe_trigger(indio_dev);
		if (ret)
			goto error_unconfigure_buffer;
	}

	ret = itg3200_initial_setup(indio_dev);
	if (ret)
		goto error_remove_trigger;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_remove_trigger;

	return 0;

error_remove_trigger:
	if (client->irq)
		itg3200_remove_trigger(indio_dev);
error_unconfigure_buffer:
	itg3200_buffer_unconfigure(indio_dev);
	return ret;
}

static int itg3200_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	if (client->irq)
		itg3200_remove_trigger(indio_dev);

	itg3200_buffer_unconfigure(indio_dev);

	return 0;
}

static const struct i2c_device_id itg3200_id[] = {
	{ "itg3200", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, itg3200_id);

static struct i2c_driver itg3200_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name	= "itg3200",
	},
	.id_table	= itg3200_id,
	.probe		= itg3200_probe,
	.remove		= itg3200_remove,
};

module_i2c_driver(itg3200_driver);

MODULE_AUTHOR("Christian Strobel <christian.strobel@iis.fraunhofer.de>");
MODULE_DESCRIPTION("ITG3200 Gyroscope I2C driver");
MODULE_LICENSE("GPL v2");
