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
 * TODO: orientation / freefall events, autosleep
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/events.h>
#include <linux/delay.h>

#define MMA8452_STATUS 0x00
#define MMA8452_OUT_X 0x01 /* MSB first, 12-bit  */
#define MMA8452_OUT_Y 0x03
#define MMA8452_OUT_Z 0x05
#define MMA8452_INT_SRC 0x0c
#define MMA8452_WHO_AM_I 0x0d
#define MMA8452_DATA_CFG 0x0e
#define MMA8452_HP_FILTER_CUTOFF 0x0f
#define MMA8452_HP_FILTER_CUTOFF_SEL_MASK	(BIT(0) | BIT(1))
#define MMA8452_TRANSIENT_CFG 0x1d
#define MMA8452_TRANSIENT_CFG_ELE		BIT(4)
#define MMA8452_TRANSIENT_CFG_CHAN(chan)	BIT(chan + 1)
#define MMA8452_TRANSIENT_CFG_HPF_BYP		BIT(0)
#define MMA8452_TRANSIENT_SRC 0x1e
#define MMA8452_TRANSIENT_SRC_XTRANSE		BIT(1)
#define MMA8452_TRANSIENT_SRC_YTRANSE		BIT(3)
#define MMA8452_TRANSIENT_SRC_ZTRANSE		BIT(5)
#define MMA8452_TRANSIENT_THS 0x1f
#define MMA8452_TRANSIENT_THS_MASK	0x7f
#define MMA8452_TRANSIENT_COUNT 0x20
#define MMA8452_OFF_X 0x2f
#define MMA8452_OFF_Y 0x30
#define MMA8452_OFF_Z 0x31
#define MMA8452_CTRL_REG1 0x2a
#define MMA8452_CTRL_REG2 0x2b
#define MMA8452_CTRL_REG2_RST		BIT(6)
#define MMA8452_CTRL_REG4 0x2d
#define MMA8452_CTRL_REG5 0x2e

#define MMA8452_MAX_REG 0x31

#define MMA8452_STATUS_DRDY (BIT(2) | BIT(1) | BIT(0))

#define MMA8452_CTRL_DR_MASK (BIT(5) | BIT(4) | BIT(3))
#define MMA8452_CTRL_DR_SHIFT 3
#define MMA8452_CTRL_DR_DEFAULT 0x4 /* 50 Hz sample frequency */
#define MMA8452_CTRL_ACTIVE BIT(0)

#define MMA8452_DATA_CFG_FS_MASK (BIT(1) | BIT(0))
#define MMA8452_DATA_CFG_FS_2G 0
#define MMA8452_DATA_CFG_FS_4G 1
#define MMA8452_DATA_CFG_FS_8G 2
#define MMA8452_DATA_CFG_HPF_MASK BIT(4)

#define MMA8452_INT_DRDY	BIT(0)
#define MMA8452_INT_TRANS	BIT(5)

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

static int mma8452_get_odr_index(struct mma8452_data *data)
{
	return (data->ctrl_reg1 & MMA8452_CTRL_DR_MASK) >>
			MMA8452_CTRL_DR_SHIFT;
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

/* Datasheet table 35  (step time vs sample frequency) */
static const int mma8452_transient_time_step_us[8] = {
	1250,
	2500,
	5000,
	10000,
	20000,
	20000,
	20000,
	20000
};

/* Datasheet table 18 (normal mode) */
static const int mma8452_hp_filter_cutoff[8][4][2] = {
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },		/* 800 Hz sample */
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },		/* 400 Hz sample */
	{ {8, 0}, {4, 0}, {2, 0}, {1, 0} },		/* 200 Hz sample */
	{ {4, 0}, {2, 0}, {1, 0}, {0, 500000} },	/* 100 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },	/* 50 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },	/* 12.5 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },	/* 6.25 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} }	/* 1.56 Hz sample */
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

static ssize_t mma8452_show_hp_cutoff_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mma8452_data *data = iio_priv(indio_dev);
	int i = mma8452_get_odr_index(data);

	return mma8452_show_int_plus_micros(buf, mma8452_hp_filter_cutoff[i],
		ARRAY_SIZE(mma8452_hp_filter_cutoff[0]));
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(mma8452_show_samp_freq_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, S_IRUGO,
	mma8452_show_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_accel_filter_high_pass_3db_frequency_available,
			S_IRUGO, mma8452_show_hp_cutoff_avail, NULL, 0);

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

static int mma8452_get_hp_filter_index(struct mma8452_data *data,
				       int val, int val2)
{
	int i = mma8452_get_odr_index(data);

	return mma8452_get_int_plus_micros_index(mma8452_hp_filter_cutoff[i],
		ARRAY_SIZE(mma8452_hp_filter_cutoff[0]), val, val2);
}

static int mma8452_read_hp_filter(struct mma8452_data *data, int *hz, int *uHz)
{
	int i, ret;

	ret = i2c_smbus_read_byte_data(data->client, MMA8452_HP_FILTER_CUTOFF);
	if (ret < 0)
		return ret;

	i = mma8452_get_odr_index(data);
	ret &= MMA8452_HP_FILTER_CUTOFF_SEL_MASK;
	*hz = mma8452_hp_filter_cutoff[i][ret][0];
	*uHz = mma8452_hp_filter_cutoff[i][ret][1];

	return 0;
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
		i = mma8452_get_odr_index(data);
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
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		if (data->data_cfg & MMA8452_DATA_CFG_HPF_MASK) {
			ret = mma8452_read_hp_filter(data, val, val2);
			if (ret < 0)
				return ret;
		} else {
			*val = 0;
			*val2 = 0;
		}
		return IIO_VAL_INT_PLUS_MICRO;
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

static int mma8452_set_hp_filter_frequency(struct mma8452_data *data,
					   int val, int val2)
{
	int i, reg;

	i = mma8452_get_hp_filter_index(data, val, val2);
	if (i < 0)
		return -EINVAL;

	reg = i2c_smbus_read_byte_data(data->client,
				       MMA8452_HP_FILTER_CUTOFF);
	if (reg < 0)
		return reg;
	reg &= ~MMA8452_HP_FILTER_CUTOFF_SEL_MASK;
	reg |= i;

	return mma8452_change_config(data, MMA8452_HP_FILTER_CUTOFF, reg);
}

static int mma8452_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int i, ret;

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

	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		if (val == 0 && val2 == 0) {
			data->data_cfg &= ~MMA8452_DATA_CFG_HPF_MASK;
		} else {
			data->data_cfg |= MMA8452_DATA_CFG_HPF_MASK;
			ret = mma8452_set_hp_filter_frequency(data, val, val2);
			if (ret < 0)
				return ret;
		}
		return mma8452_change_config(data, MMA8452_DATA_CFG,
						data->data_cfg);

	default:
		return -EINVAL;
	}
}

static int mma8452_read_thresh(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info,
			       int *val, int *val2)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret, us;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		ret = i2c_smbus_read_byte_data(data->client,
					       MMA8452_TRANSIENT_THS);
		if (ret < 0)
			return ret;

		*val = ret & MMA8452_TRANSIENT_THS_MASK;
		return IIO_VAL_INT;

	case IIO_EV_INFO_PERIOD:
		ret = i2c_smbus_read_byte_data(data->client,
					       MMA8452_TRANSIENT_COUNT);
		if (ret < 0)
			return ret;

		us = ret * mma8452_transient_time_step_us[
				mma8452_get_odr_index(data)];
		*val = us / USEC_PER_SEC;
		*val2 = us % USEC_PER_SEC;
		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_EV_INFO_HIGH_PASS_FILTER_3DB:
		ret = i2c_smbus_read_byte_data(data->client,
					       MMA8452_TRANSIENT_CFG);
		if (ret < 0)
			return ret;

		if (ret & MMA8452_TRANSIENT_CFG_HPF_BYP) {
			*val = 0;
			*val2 = 0;
		} else {
			ret = mma8452_read_hp_filter(data, val, val2);
			if (ret < 0)
				return ret;
		}
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int mma8452_write_thresh(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret, reg, steps;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return mma8452_change_config(data, MMA8452_TRANSIENT_THS,
					     val & MMA8452_TRANSIENT_THS_MASK);

	case IIO_EV_INFO_PERIOD:
		steps = (val * USEC_PER_SEC + val2) /
				mma8452_transient_time_step_us[
					mma8452_get_odr_index(data)];

		if (steps > 0xff)
			return -EINVAL;

		return mma8452_change_config(data, MMA8452_TRANSIENT_COUNT,
					     steps);
	case IIO_EV_INFO_HIGH_PASS_FILTER_3DB:
		reg = i2c_smbus_read_byte_data(data->client,
					       MMA8452_TRANSIENT_CFG);
		if (reg < 0)
			return reg;

		if (val == 0 && val2 == 0) {
			reg |= MMA8452_TRANSIENT_CFG_HPF_BYP;
		} else {
			reg &= ~MMA8452_TRANSIENT_CFG_HPF_BYP;
			ret = mma8452_set_hp_filter_frequency(data, val, val2);
			if (ret < 0)
				return ret;
		}
		return mma8452_change_config(data, MMA8452_TRANSIENT_CFG, reg);

	default:
		return -EINVAL;
	}
}

static int mma8452_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, MMA8452_TRANSIENT_CFG);
	if (ret < 0)
		return ret;

	return ret & MMA8452_TRANSIENT_CFG_CHAN(chan->scan_index) ? 1 : 0;
}

static int mma8452_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int val;

	val = i2c_smbus_read_byte_data(data->client, MMA8452_TRANSIENT_CFG);
	if (val < 0)
		return val;

	if (state)
		val |= MMA8452_TRANSIENT_CFG_CHAN(chan->scan_index);
	else
		val &= ~MMA8452_TRANSIENT_CFG_CHAN(chan->scan_index);

	val |= MMA8452_TRANSIENT_CFG_ELE;

	return mma8452_change_config(data, MMA8452_TRANSIENT_CFG, val);
}

static void mma8452_transient_interrupt(struct iio_dev *indio_dev)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	s64 ts = iio_get_time_ns();
	int src;

	src = i2c_smbus_read_byte_data(data->client, MMA8452_TRANSIENT_SRC);
	if (src < 0)
		return;

	if (src & MMA8452_TRANSIENT_SRC_XTRANSE)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_X,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_RISING),
			       ts);

	if (src & MMA8452_TRANSIENT_SRC_YTRANSE)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_Y,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_RISING),
			       ts);

	if (src & MMA8452_TRANSIENT_SRC_ZTRANSE)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_Z,
						  IIO_EV_TYPE_MAG,
						  IIO_EV_DIR_RISING),
			       ts);
}

static irqreturn_t mma8452_interrupt(int irq, void *p)
{
	struct iio_dev *indio_dev = p;
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret = IRQ_NONE;
	int src;

	src = i2c_smbus_read_byte_data(data->client, MMA8452_INT_SRC);
	if (src < 0)
		return IRQ_NONE;

	if (src & MMA8452_INT_DRDY) {
		iio_trigger_poll_chained(indio_dev->trig);
		ret = IRQ_HANDLED;
	}

	if (src & MMA8452_INT_TRANS) {
		mma8452_transient_interrupt(indio_dev);
		ret = IRQ_HANDLED;
	}

	return ret;
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

static int mma8452_reg_access_dbg(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval)
{
	int ret;
	struct mma8452_data *data = iio_priv(indio_dev);

	if (reg > MMA8452_MAX_REG)
		return -EINVAL;

	if (!readval)
		return mma8452_change_config(data, reg, writeval);

	ret = i2c_smbus_read_byte_data(data->client, reg);
	if (ret < 0)
		return ret;

	*readval = ret;

	return 0;
}

static const struct iio_event_spec mma8452_transient_event[] = {
	{
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
					BIT(IIO_EV_INFO_PERIOD) |
					BIT(IIO_EV_INFO_HIGH_PASS_FILTER_3DB)
	},
};

/*
 * Threshold is configured in fixed 8G/127 steps regardless of
 * currently selected scale for measurement.
 */
static IIO_CONST_ATTR_NAMED(accel_transient_scale, in_accel_scale, "0.617742");

static struct attribute *mma8452_event_attributes[] = {
	&iio_const_attr_accel_transient_scale.dev_attr.attr,
	NULL,
};

static struct attribute_group mma8452_event_attribute_group = {
	.attrs = mma8452_event_attributes,
	.name = "events",
};

#define MMA8452_CHANNEL(axis, idx) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_##axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY), \
	.scan_index = idx, \
	.scan_type = { \
		.sign = 's', \
		.realbits = 12, \
		.storagebits = 16, \
		.shift = 4, \
		.endianness = IIO_BE, \
	}, \
	.event_spec = mma8452_transient_event, \
	.num_event_specs = ARRAY_SIZE(mma8452_transient_event), \
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
	&iio_dev_attr_in_accel_filter_high_pass_3db_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group mma8452_group = {
	.attrs = mma8452_attributes,
};

static const struct iio_info mma8452_info = {
	.attrs = &mma8452_group,
	.read_raw = &mma8452_read_raw,
	.write_raw = &mma8452_write_raw,
	.event_attrs = &mma8452_event_attribute_group,
	.read_event_value = &mma8452_read_thresh,
	.write_event_value = &mma8452_write_thresh,
	.read_event_config = &mma8452_read_event_config,
	.write_event_config = &mma8452_write_event_config,
	.debugfs_reg_access = &mma8452_reg_access_dbg,
	.driver_module = THIS_MODULE,
};

static const unsigned long mma8452_scan_masks[] = {0x7, 0};

static int mma8452_data_rdy_trigger_set_state(struct iio_trigger *trig,
					      bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mma8452_data *data = iio_priv(indio_dev);
	int reg;

	reg = i2c_smbus_read_byte_data(data->client, MMA8452_CTRL_REG4);
	if (reg < 0)
		return reg;

	if (state)
		reg |= MMA8452_INT_DRDY;
	else
		reg &= ~MMA8452_INT_DRDY;

	return mma8452_change_config(data, MMA8452_CTRL_REG4, reg);
}

static int mma8452_validate_device(struct iio_trigger *trig,
				   struct iio_dev *indio_dev)
{
	struct iio_dev *indio = iio_trigger_get_drvdata(trig);

	if (indio != indio_dev)
		return -EINVAL;

	return 0;
}

static const struct iio_trigger_ops mma8452_trigger_ops = {
	.set_trigger_state = mma8452_data_rdy_trigger_set_state,
	.validate_device = mma8452_validate_device,
	.owner = THIS_MODULE,
};

static int mma8452_trigger_setup(struct iio_dev *indio_dev)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(&data->client->dev, "%s-dev%d",
				      indio_dev->name,
				      indio_dev->id);
	if (!trig)
		return -ENOMEM;

	trig->dev.parent = &data->client->dev;
	trig->ops = &mma8452_trigger_ops;
	iio_trigger_set_drvdata(trig, indio_dev);

	ret = iio_trigger_register(trig);
	if (ret)
		return ret;

	indio_dev->trig = trig;
	return 0;
}

static void mma8452_trigger_cleanup(struct iio_dev *indio_dev)
{
	if (indio_dev->trig)
		iio_trigger_unregister(indio_dev->trig);
}

static int mma8452_reset(struct i2c_client *client)
{
	int i;
	int ret;

	ret = i2c_smbus_write_byte_data(client,	MMA8452_CTRL_REG2,
					MMA8452_CTRL_REG2_RST);
	if (ret < 0)
		return ret;

	for (i = 0; i < 10; i++) {
		usleep_range(100, 200);
		ret = i2c_smbus_read_byte_data(client, MMA8452_CTRL_REG2);
		if (ret == -EIO)
			continue; /* I2C comm reset */
		if (ret < 0)
			return ret;
		if (!(ret & MMA8452_CTRL_REG2_RST))
			return 0;
	}

	return -ETIMEDOUT;
}

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

	ret = mma8452_reset(client);
	if (ret < 0)
		return ret;

	data->data_cfg = MMA8452_DATA_CFG_FS_2G;
	ret = i2c_smbus_write_byte_data(client, MMA8452_DATA_CFG,
		data->data_cfg);
	if (ret < 0)
		return ret;

	/*
	 * By default set transient threshold to max to avoid events if
	 * enabling without configuring threshold.
	 */
	ret = i2c_smbus_write_byte_data(client, MMA8452_TRANSIENT_THS,
					MMA8452_TRANSIENT_THS_MASK);
	if (ret < 0)
		return ret;

	if (client->irq) {
		/*
		 * Although we enable the transient interrupt source once and
		 * for all here the transient event detection itself is not
		 * enabled until userspace asks for it by
		 * mma8452_write_event_config()
		 */
		int supported_interrupts = MMA8452_INT_DRDY | MMA8452_INT_TRANS;
		int enabled_interrupts = MMA8452_INT_TRANS;

		/* Assume wired to INT1 pin */
		ret = i2c_smbus_write_byte_data(client,
						MMA8452_CTRL_REG5,
						supported_interrupts);
		if (ret < 0)
			return ret;

		ret = i2c_smbus_write_byte_data(client,
						MMA8452_CTRL_REG4,
						enabled_interrupts);
		if (ret < 0)
			return ret;

		ret = mma8452_trigger_setup(indio_dev);
		if (ret < 0)
			return ret;
	}

	data->ctrl_reg1 = MMA8452_CTRL_ACTIVE |
		(MMA8452_CTRL_DR_DEFAULT << MMA8452_CTRL_DR_SHIFT);
	ret = i2c_smbus_write_byte_data(client, MMA8452_CTRL_REG1,
					data->ctrl_reg1);
	if (ret < 0)
		goto trigger_cleanup;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		mma8452_trigger_handler, NULL);
	if (ret < 0)
		goto trigger_cleanup;

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev,
						client->irq,
						NULL, mma8452_interrupt,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						client->name, indio_dev);
		if (ret)
			goto buffer_cleanup;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;

	return 0;

buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);

trigger_cleanup:
	mma8452_trigger_cleanup(indio_dev);

	return ret;
}

static int mma8452_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	mma8452_trigger_cleanup(indio_dev);
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
