/*
 * mma8452.c - Support for Freescale MMA8452Q 3-axis 12-bit accelerometer
 *
 * Copyright 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * 7-bit I2C slave address 0x1c/0x1d (pin selectable)
 *
 * TODO: interrupt, thresholding, orientation / freefall events, autosleep
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/delay.h>

#define MMA8452_STATUS 0x00
#define MMA8452_OUT_X 0x01 /* MSB first, 12-bit  */
#define MMA8452_OUT_Y 0x03
#define MMA8452_OUT_Z 0x05
#define MMA8452_WHO_AM_I 0x0d
#define MMA8452_DATA_CFG 0x0e
#define MMA8452_OFF_X 0x2f
#define MMA8452_OFF_Y 0x30
#define MMA8452_OFF_Z 0x31
#define MMA8452_CTRL_REG1 0x2a
#define MMA8452_CTRL_REG2 0x2b

#define MMA8452_STATUS_DRDY (BIT(2) | BIT(1) | BIT(0))

#define MMA8452_CTRL_DR_MASK (BIT(5) | BIT(4) | BIT(3))
#define MMA8452_CTRL_DR_SHIFT 3
#define MMA8452_CTRL_DR_DEFAULT 0x4 /* 50 Hz sample frequency */
#define MMA8452_CTRL_ACTIVE BIT(0)

#define MMA8452_DATA_CFG_FS_MASK (BIT(1) | BIT(0))
#define MMA8452_DATA_CFG_FS_2G 0
#define MMA8452_DATA_CFG_FS_4G 1
#define MMA8452_DATA_CFG_FS_8G 2

#define MMA8452_DEVICE_ID 0x2a

struct mma8452_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 ctrl_reg1;
	u8 data_cfg;
};

static int mma8452_drdy(struct mma8452_data *data)
{
	int tries = 150;

	while (tries-- > 0) {
		int ret = i2c_smbus_read_byte_data(data->client,
			MMA8452_STATUS);
		if (ret < 0)
			return ret;
		if ((ret & MMA8452_STATUS_DRDY) == MMA8452_STATUS_DRDY)
			return 0;
		msleep(20);
	}

	dev_err(&data->client->dev, "data not ready\n");
	return -EIO;
}

static int mma8452_read(struct mma8452_data *data, __be16 buf[3])
{
	int ret = mma8452_drdy(data);
	if (ret < 0)
		return ret;
	return i2c_smbus_read_i2c_block_data(data->client,
		MMA8452_OUT_X, 3 * sizeof(__be16), (u8 *) buf);
}

static ssize_t mma8452_show_int_plus_micros(char *buf,
	const int (*vals)[2], int n)
{
	size_t len = 0;

	while (n-- > 0)
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"%d.%06d ", vals[n][0], vals[n][1]);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static int mma8452_get_int_plus_micros_index(const int (*vals)[2], int n,
					int val, int val2)
{
	while (n-- > 0)
		if (val == vals[n][0] && val2 == vals[n][1])
			return n;

	return -EINVAL;
}

static const int mma8452_samp_freq[8][2] = {
	{800, 0}, {400, 0}, {200, 0}, {100, 0}, {50, 0}, {12, 500000},
	{6, 250000}, {1, 560000}
};

/* 
 * Hardware has fullscale of -2G, -4G, -8G corresponding to raw value -2048
 * The userspace interface uses m/s^2 and we declare micro units
 * So scale factor is given by:
 * 	g * N * 1000000 / 2048 for N = 2, 4, 8 and g=9.80665
 */
static const int mma8452_scales[3][2] = {
	{0, 9577}, {0, 19154}, {0, 38307}
};

static ssize_t mma8452_show_samp_freq_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return mma8452_show_int_plus_micros(buf, mma8452_samp_freq,
		ARRAY_SIZE(mma8452_samp_freq));
}

static ssize_t mma8452_show_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return mma8452_show_int_plus_micros(buf, mma8452_scales,
		ARRAY_SIZE(mma8452_scales));
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(mma8452_show_samp_freq_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, S_IRUGO,
	mma8452_show_scale_avail, NULL, 0);

static int mma8452_get_samp_freq_index(struct mma8452_data *data,
	int val, int val2)
{
	return mma8452_get_int_plus_micros_index(mma8452_samp_freq,
		ARRAY_SIZE(mma8452_samp_freq), val, val2);
}

static int mma8452_get_scale_index(struct mma8452_data *data,
	int val, int val2)
{
	return mma8452_get_int_plus_micros_index(mma8452_scales,
		ARRAY_SIZE(mma8452_scales), val, val2);
}

static int mma8452_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	__be16 buffer[3];
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;

		mutex_lock(&data->lock);
		ret = mma8452_read(data, buffer);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;
		*val = sign_extend32(
			be16_to_cpu(buffer[chan->scan_index]) >> 4, 11);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		i = data->data_cfg & MMA8452_DATA_CFG_FS_MASK;
		*val = mma8452_scales[i][0];
		*val2 = mma8452_scales[i][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = (data->ctrl_reg1 & MMA8452_CTRL_DR_MASK) >>
			MMA8452_CTRL_DR_SHIFT;
		*val = mma8452_samp_freq[i][0];
		*val2 = mma8452_samp_freq[i][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = i2c_smbus_read_byte_data(data->client, MMA8452_OFF_X +
			chan->scan_index);
		if (ret < 0)
			return ret;
		*val = sign_extend32(ret, 7);
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static int mma8452_standby(struct mma8452_data *data)
{
	return i2c_smbus_write_byte_data(data->client, MMA8452_CTRL_REG1,
		data->ctrl_reg1 & ~MMA8452_CTRL_ACTIVE);
}

static int mma8452_active(struct mma8452_data *data)
{
	return i2c_smbus_write_byte_data(data->client, MMA8452_CTRL_REG1,
		data->ctrl_reg1);
}

static int mma8452_change_config(struct mma8452_data *data, u8 reg, u8 val)
{
	int ret;

	mutex_lock(&data->lock);

	/* config can only be changed when in standby */
	ret = mma8452_standby(data);
	if (ret < 0)
		goto fail;

	ret = i2c_smbus_write_byte_data(data->client, reg, val);
	if (ret < 0)
		goto fail;

	ret = mma8452_active(data);
	if (ret < 0)
		goto fail;

	ret = 0;
fail:
	mutex_unlock(&data->lock);
	return ret;
}

static int mma8452_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int i;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = mma8452_get_samp_freq_index(data, val, val2);
		if (i < 0)
			return -EINVAL;

		data->ctrl_reg1 &= ~MMA8452_CTRL_DR_MASK;
		data->ctrl_reg1 |= i << MMA8452_CTRL_DR_SHIFT;
		return mma8452_change_config(data, MMA8452_CTRL_REG1,
			data->ctrl_reg1);
	case IIO_CHAN_INFO_SCALE:
		i = mma8452_get_scale_index(data, val, val2);
		if (i < 0)
			return -EINVAL;
		data->data_cfg &= ~MMA8452_DATA_CFG_FS_MASK;
		data->data_cfg |= i;
		return mma8452_change_config(data, MMA8452_DATA_CFG,
			data->data_cfg);
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < -128 || val > 127)
			return -EINVAL;
		return mma8452_change_config(data, MMA8452_OFF_X +
			chan->scan_index, val);
	default:
		return -EINVAL;
	}
}

static irqreturn_t mma8452_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mma8452_data *data = iio_priv(indio_dev);
	u8 buffer[16]; /* 3 16-bit channels + padding + ts */
	int ret;

	ret = mma8452_read(data, (__be16 *) buffer);
	if (ret < 0)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
		iio_get_time_ns());

done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

#define MMA8452_CHANNEL(axis, idx) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_##axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.scan_index = idx, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 12, \
		.storagebits = 16, \
		.shift = 4, \
		.endianness = IIO_BE, \
	}, \
}

static const struct iio_chan_spec mma8452_channels[] = {
	MMA8452_CHANNEL(X, 0),
	MMA8452_CHANNEL(Y, 1),
	MMA8452_CHANNEL(Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static struct attribute *mma8452_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group mma8452_group = {
	.attrs = mma8452_attributes,
};

static const struct iio_info mma8452_info = {
	.attrs = &mma8452_group,
	.read_raw = &mma8452_read_raw,
	.write_raw = &mma8452_write_raw,
	.driver_module = THIS_MODULE,
};

static const unsigned long mma8452_scan_masks[] = {0x7, 0};

static int mma8452_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mma8452_data *data;
	struct iio_dev *indio_dev;
	int ret;

	ret = i2c_smbus_read_byte_data(client, MMA8452_WHO_AM_I);
	if (ret < 0)
		return ret;
	if (ret != MMA8452_DEVICE_ID)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	i2c_set_clientdata(client, indio_dev);
	indio_dev->info = &mma8452_info;
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mma8452_channels;
	indio_dev->num_channels = ARRAY_SIZE(mma8452_channels);
	indio_dev->available_scan_masks = mma8452_scan_masks;

	data->ctrl_reg1 = MMA8452_CTRL_ACTIVE |
		(MMA8452_CTRL_DR_DEFAULT << MMA8452_CTRL_DR_SHIFT);
	ret = i2c_smbus_write_byte_data(client, MMA8452_CTRL_REG1,
		data->ctrl_reg1);
	if (ret < 0)
		return ret;

	data->data_cfg = MMA8452_DATA_CFG_FS_2G;
	ret = i2c_smbus_write_byte_data(client, MMA8452_DATA_CFG,
		data->data_cfg);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		mma8452_trigger_handler, NULL);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;
	return 0;

buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
	return ret;
}

static int mma8452_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	mma8452_standby(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mma8452_suspend(struct device *dev)
{
	return mma8452_standby(iio_priv(i2c_get_clientdata(
		to_i2c_client(dev))));
}

static int mma8452_resume(struct device *dev)
{
	return mma8452_active(iio_priv(i2c_get_clientdata(
		to_i2c_client(dev))));
}

static SIMPLE_DEV_PM_OPS(mma8452_pm_ops, mma8452_suspend, mma8452_resume);
#define MMA8452_PM_OPS (&mma8452_pm_ops)
#else
#define MMA8452_PM_OPS NULL
#endif

static const struct i2c_device_id mma8452_id[] = {
	{ "mma8452", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mma8452_id);

static const struct of_device_id mma8452_dt_ids[] = {
	{ .compatible = "fsl,mma8452" },
	{ }
};

static struct i2c_driver mma8452_driver = {
	.driver = {
		.name	= "mma8452",
		.of_match_table = of_match_ptr(mma8452_dt_ids),
		.pm	= MMA8452_PM_OPS,
	},
	.probe = mma8452_probe,
	.remove = mma8452_remove,
	.id_table = mma8452_id,
};
module_i2c_driver(mma8452_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Freescale MMA8452 accelerometer driver");
MODULE_LICENSE("GPL");
