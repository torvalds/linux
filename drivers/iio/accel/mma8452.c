// SPDX-License-Identifier: GPL-2.0
/*
 * mma8452.c - Support for following Freescale / NXP 3-axis accelerometers:
 *
 * device name	digital output	7-bit I2C slave address (pin selectable)
 * ---------------------------------------------------------------------
 * MMA8451Q	14 bit		0x1c / 0x1d
 * MMA8452Q	12 bit		0x1c / 0x1d
 * MMA8453Q	10 bit		0x1c / 0x1d
 * MMA8652FC	12 bit		0x1d
 * MMA8653FC	10 bit		0x1d
 * FXLS8471Q	14 bit		0x1e / 0x1d / 0x1c / 0x1f
 *
 * Copyright 2015 Martin Kepplinger <martink@posteo.de>
 * Copyright 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 *
 * TODO: orientation events
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/events.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>

#define MMA8452_STATUS				0x00
#define  MMA8452_STATUS_DRDY			(BIT(2) | BIT(1) | BIT(0))
#define MMA8452_OUT_X				0x01 /* MSB first */
#define MMA8452_OUT_Y				0x03
#define MMA8452_OUT_Z				0x05
#define MMA8452_INT_SRC				0x0c
#define MMA8452_WHO_AM_I			0x0d
#define MMA8452_DATA_CFG			0x0e
#define  MMA8452_DATA_CFG_FS_MASK		GENMASK(1, 0)
#define  MMA8452_DATA_CFG_FS_2G			0
#define  MMA8452_DATA_CFG_FS_4G			1
#define  MMA8452_DATA_CFG_FS_8G			2
#define  MMA8452_DATA_CFG_HPF_MASK		BIT(4)
#define MMA8452_HP_FILTER_CUTOFF		0x0f
#define  MMA8452_HP_FILTER_CUTOFF_SEL_MASK	GENMASK(1, 0)
#define MMA8452_FF_MT_CFG			0x15
#define  MMA8452_FF_MT_CFG_OAE			BIT(6)
#define  MMA8452_FF_MT_CFG_ELE			BIT(7)
#define MMA8452_FF_MT_SRC			0x16
#define  MMA8452_FF_MT_SRC_XHE			BIT(1)
#define  MMA8452_FF_MT_SRC_YHE			BIT(3)
#define  MMA8452_FF_MT_SRC_ZHE			BIT(5)
#define MMA8452_FF_MT_THS			0x17
#define  MMA8452_FF_MT_THS_MASK			0x7f
#define MMA8452_FF_MT_COUNT			0x18
#define MMA8452_FF_MT_CHAN_SHIFT	3
#define MMA8452_TRANSIENT_CFG			0x1d
#define  MMA8452_TRANSIENT_CFG_CHAN(chan)	BIT(chan + 1)
#define  MMA8452_TRANSIENT_CFG_HPF_BYP		BIT(0)
#define  MMA8452_TRANSIENT_CFG_ELE		BIT(4)
#define MMA8452_TRANSIENT_SRC			0x1e
#define  MMA8452_TRANSIENT_SRC_XTRANSE		BIT(1)
#define  MMA8452_TRANSIENT_SRC_YTRANSE		BIT(3)
#define  MMA8452_TRANSIENT_SRC_ZTRANSE		BIT(5)
#define MMA8452_TRANSIENT_THS			0x1f
#define  MMA8452_TRANSIENT_THS_MASK		GENMASK(6, 0)
#define MMA8452_TRANSIENT_COUNT			0x20
#define MMA8452_TRANSIENT_CHAN_SHIFT 1
#define MMA8452_CTRL_REG1			0x2a
#define  MMA8452_CTRL_ACTIVE			BIT(0)
#define  MMA8452_CTRL_DR_MASK			GENMASK(5, 3)
#define  MMA8452_CTRL_DR_SHIFT			3
#define  MMA8452_CTRL_DR_DEFAULT		0x4 /* 50 Hz sample frequency */
#define MMA8452_CTRL_REG2			0x2b
#define  MMA8452_CTRL_REG2_RST			BIT(6)
#define  MMA8452_CTRL_REG2_MODS_SHIFT		3
#define  MMA8452_CTRL_REG2_MODS_MASK		0x1b
#define MMA8452_CTRL_REG4			0x2d
#define MMA8452_CTRL_REG5			0x2e
#define MMA8452_OFF_X				0x2f
#define MMA8452_OFF_Y				0x30
#define MMA8452_OFF_Z				0x31

#define MMA8452_MAX_REG				0x31

#define  MMA8452_INT_DRDY			BIT(0)
#define  MMA8452_INT_FF_MT			BIT(2)
#define  MMA8452_INT_TRANS			BIT(5)

#define MMA8451_DEVICE_ID			0x1a
#define MMA8452_DEVICE_ID			0x2a
#define MMA8453_DEVICE_ID			0x3a
#define MMA8652_DEVICE_ID			0x4a
#define MMA8653_DEVICE_ID			0x5a
#define FXLS8471_DEVICE_ID			0x6a

#define MMA8452_AUTO_SUSPEND_DELAY_MS		2000

struct mma8452_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 ctrl_reg1;
	u8 data_cfg;
	const struct mma_chip_info *chip_info;
};

 /**
  * struct mma8452_event_regs - chip specific data related to events
  * @ev_cfg:			event config register address
  * @ev_cfg_ele:			latch bit in event config register
  * @ev_cfg_chan_shift:		number of the bit to enable events in X
  *				direction; in event config register
  * @ev_src:			event source register address
  * @ev_ths:			event threshold register address
  * @ev_ths_mask:		mask for the threshold value
  * @ev_count:			event count (period) register address
  *
  * Since not all chips supported by the driver support comparing high pass
  * filtered data for events (interrupts), different interrupt sources are
  * used for different chips and the relevant registers are included here.
  */
struct mma8452_event_regs {
		u8 ev_cfg;
		u8 ev_cfg_ele;
		u8 ev_cfg_chan_shift;
		u8 ev_src;
		u8 ev_ths;
		u8 ev_ths_mask;
		u8 ev_count;
};

static const struct mma8452_event_regs ff_mt_ev_regs = {
		.ev_cfg = MMA8452_FF_MT_CFG,
		.ev_cfg_ele = MMA8452_FF_MT_CFG_ELE,
		.ev_cfg_chan_shift = MMA8452_FF_MT_CHAN_SHIFT,
		.ev_src = MMA8452_FF_MT_SRC,
		.ev_ths = MMA8452_FF_MT_THS,
		.ev_ths_mask = MMA8452_FF_MT_THS_MASK,
		.ev_count = MMA8452_FF_MT_COUNT
};

static const struct mma8452_event_regs trans_ev_regs = {
		.ev_cfg = MMA8452_TRANSIENT_CFG,
		.ev_cfg_ele = MMA8452_TRANSIENT_CFG_ELE,
		.ev_cfg_chan_shift = MMA8452_TRANSIENT_CHAN_SHIFT,
		.ev_src = MMA8452_TRANSIENT_SRC,
		.ev_ths = MMA8452_TRANSIENT_THS,
		.ev_ths_mask = MMA8452_TRANSIENT_THS_MASK,
		.ev_count = MMA8452_TRANSIENT_COUNT,
};

/**
 * struct mma_chip_info - chip specific data
 * @chip_id:			WHO_AM_I register's value
 * @channels:			struct iio_chan_spec matching the device's
 *				capabilities
 * @num_channels:		number of channels
 * @mma_scales:			scale factors for converting register values
 *				to m/s^2; 3 modes: 2g, 4g, 8g; 2 integers
 *				per mode: m/s^2 and micro m/s^2
 * @all_events:			all events supported by this chip
 * @enabled_events:		event flags enabled and handled by this driver
 */
struct mma_chip_info {
	u8 chip_id;
	const struct iio_chan_spec *channels;
	int num_channels;
	const int mma_scales[3][2];
	int all_events;
	int enabled_events;
};

enum {
	idx_x,
	idx_y,
	idx_z,
	idx_ts,
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

static int mma8452_set_runtime_pm_state(struct i2c_client *client, bool on)
{
#ifdef CONFIG_PM
	int ret;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
	} else {
		pm_runtime_mark_last_busy(&client->dev);
		ret = pm_runtime_put_autosuspend(&client->dev);
	}

	if (ret < 0) {
		dev_err(&client->dev,
			"failed to change power state to %d\n", on);
		if (on)
			pm_runtime_put_noidle(&client->dev);

		return ret;
	}
#endif

	return 0;
}

static int mma8452_read(struct mma8452_data *data, __be16 buf[3])
{
	int ret = mma8452_drdy(data);

	if (ret < 0)
		return ret;

	ret = mma8452_set_runtime_pm_state(data->client, true);
	if (ret)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(data->client, MMA8452_OUT_X,
					    3 * sizeof(__be16), (u8 *)buf);

	ret = mma8452_set_runtime_pm_state(data->client, false);

	return ret;
}

static ssize_t mma8452_show_int_plus_micros(char *buf, const int (*vals)[2],
					    int n)
{
	size_t len = 0;

	while (n-- > 0)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 vals[n][0], vals[n][1]);

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

static unsigned int mma8452_get_odr_index(struct mma8452_data *data)
{
	return (data->ctrl_reg1 & MMA8452_CTRL_DR_MASK) >>
			MMA8452_CTRL_DR_SHIFT;
}

static const int mma8452_samp_freq[8][2] = {
	{800, 0}, {400, 0}, {200, 0}, {100, 0}, {50, 0}, {12, 500000},
	{6, 250000}, {1, 560000}
};

/* Datasheet table: step time "Relationship with the ODR" (sample frequency) */
static const unsigned int mma8452_time_step_us[4][8] = {
	{ 1250, 2500, 5000, 10000, 20000, 20000, 20000, 20000 },  /* normal */
	{ 1250, 2500, 5000, 10000, 20000, 80000, 80000, 80000 },  /* l p l n */
	{ 1250, 2500, 2500, 2500, 2500, 2500, 2500, 2500 },	  /* high res*/
	{ 1250, 2500, 5000, 10000, 20000, 80000, 160000, 160000 } /* l p */
};

/* Datasheet table "High-Pass Filter Cutoff Options" */
static const int mma8452_hp_filter_cutoff[4][8][4][2] = {
	{ /* normal */
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },		/* 800 Hz sample */
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },		/* 400 Hz sample */
	{ {8, 0}, {4, 0}, {2, 0}, {1, 0} },		/* 200 Hz sample */
	{ {4, 0}, {2, 0}, {1, 0}, {0, 500000} },	/* 100 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },	/* 50 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },	/* 12.5 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },	/* 6.25 Hz sample */
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} }	/* 1.56 Hz sample */
	},
	{ /* low noise low power */
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {8, 0}, {4, 0}, {2, 0}, {1, 0} },
	{ {4, 0}, {2, 0}, {1, 0}, {0, 500000} },
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },
	{ {0, 500000}, {0, 250000}, {0, 125000}, {0, 063000} },
	{ {0, 500000}, {0, 250000}, {0, 125000}, {0, 063000} },
	{ {0, 500000}, {0, 250000}, {0, 125000}, {0, 063000} }
	},
	{ /* high resolution */
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} }
	},
	{ /* low power */
	{ {16, 0}, {8, 0}, {4, 0}, {2, 0} },
	{ {8, 0}, {4, 0}, {2, 0}, {1, 0} },
	{ {4, 0}, {2, 0}, {1, 0}, {0, 500000} },
	{ {2, 0}, {1, 0}, {0, 500000}, {0, 250000} },
	{ {1, 0}, {0, 500000}, {0, 250000}, {0, 125000} },
	{ {0, 250000}, {0, 125000}, {0, 063000}, {0, 031000} },
	{ {0, 250000}, {0, 125000}, {0, 063000}, {0, 031000} },
	{ {0, 250000}, {0, 125000}, {0, 063000}, {0, 031000} }
	}
};

/* Datasheet table "MODS Oversampling modes averaging values at each ODR" */
static const u16 mma8452_os_ratio[4][8] = {
	/* 800 Hz, 400 Hz, ... , 1.56 Hz */
	{ 2, 4, 4, 4, 4, 16, 32, 128 },		/* normal */
	{ 2, 4, 4, 4, 4, 4, 8, 32 },		/* low power low noise */
	{ 2, 4, 8, 16, 32, 128, 256, 1024 },	/* high resolution */
	{ 2, 2, 2, 2, 2, 2, 4, 16 }		/* low power */
};

static int mma8452_get_power_mode(struct mma8452_data *data)
{
	int reg;

	reg = i2c_smbus_read_byte_data(data->client,
				       MMA8452_CTRL_REG2);
	if (reg < 0)
		return reg;

	return ((reg & MMA8452_CTRL_REG2_MODS_MASK) >>
		MMA8452_CTRL_REG2_MODS_SHIFT);
}

static ssize_t mma8452_show_samp_freq_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return mma8452_show_int_plus_micros(buf, mma8452_samp_freq,
					    ARRAY_SIZE(mma8452_samp_freq));
}

static ssize_t mma8452_show_scale_avail(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mma8452_data *data = iio_priv(i2c_get_clientdata(
					     to_i2c_client(dev)));

	return mma8452_show_int_plus_micros(buf, data->chip_info->mma_scales,
		ARRAY_SIZE(data->chip_info->mma_scales));
}

static ssize_t mma8452_show_hp_cutoff_avail(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mma8452_data *data = iio_priv(indio_dev);
	int i, j;

	i = mma8452_get_odr_index(data);
	j = mma8452_get_power_mode(data);
	if (j < 0)
		return j;

	return mma8452_show_int_plus_micros(buf, mma8452_hp_filter_cutoff[j][i],
		ARRAY_SIZE(mma8452_hp_filter_cutoff[0][0]));
}

static ssize_t mma8452_show_os_ratio_avail(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mma8452_data *data = iio_priv(indio_dev);
	int i = mma8452_get_odr_index(data);
	int j;
	u16 val = 0;
	size_t len = 0;

	for (j = 0; j < ARRAY_SIZE(mma8452_os_ratio); j++) {
		if (val == mma8452_os_ratio[j][i])
			continue;

		val = mma8452_os_ratio[j][i];

		len += scnprintf(buf + len, PAGE_SIZE - len, "%d ", val);
	}
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(mma8452_show_samp_freq_avail);
static IIO_DEVICE_ATTR(in_accel_scale_available, 0444,
		       mma8452_show_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_accel_filter_high_pass_3db_frequency_available,
		       0444, mma8452_show_hp_cutoff_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_accel_oversampling_ratio_available, 0444,
		       mma8452_show_os_ratio_avail, NULL, 0);

static int mma8452_get_samp_freq_index(struct mma8452_data *data,
				       int val, int val2)
{
	return mma8452_get_int_plus_micros_index(mma8452_samp_freq,
						 ARRAY_SIZE(mma8452_samp_freq),
						 val, val2);
}

static int mma8452_get_scale_index(struct mma8452_data *data, int val, int val2)
{
	return mma8452_get_int_plus_micros_index(data->chip_info->mma_scales,
			ARRAY_SIZE(data->chip_info->mma_scales), val, val2);
}

static int mma8452_get_hp_filter_index(struct mma8452_data *data,
				       int val, int val2)
{
	int i, j;

	i = mma8452_get_odr_index(data);
	j = mma8452_get_power_mode(data);
	if (j < 0)
		return j;

	return mma8452_get_int_plus_micros_index(mma8452_hp_filter_cutoff[j][i],
		ARRAY_SIZE(mma8452_hp_filter_cutoff[0][0]), val, val2);
}

static int mma8452_read_hp_filter(struct mma8452_data *data, int *hz, int *uHz)
{
	int j, i, ret;

	ret = i2c_smbus_read_byte_data(data->client, MMA8452_HP_FILTER_CUTOFF);
	if (ret < 0)
		return ret;

	i = mma8452_get_odr_index(data);
	j = mma8452_get_power_mode(data);
	if (j < 0)
		return j;

	ret &= MMA8452_HP_FILTER_CUTOFF_SEL_MASK;
	*hz = mma8452_hp_filter_cutoff[j][i][ret][0];
	*uHz = mma8452_hp_filter_cutoff[j][i][ret][1];

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
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		mutex_lock(&data->lock);
		ret = mma8452_read(data, buffer);
		mutex_unlock(&data->lock);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		*val = sign_extend32(be16_to_cpu(
			buffer[chan->scan_index]) >> chan->scan_type.shift,
			chan->scan_type.realbits - 1);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		i = data->data_cfg & MMA8452_DATA_CFG_FS_MASK;
		*val = data->chip_info->mma_scales[i][0];
		*val2 = data->chip_info->mma_scales[i][1];

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = mma8452_get_odr_index(data);
		*val = mma8452_samp_freq[i][0];
		*val2 = mma8452_samp_freq[i][1];

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = i2c_smbus_read_byte_data(data->client,
					       MMA8452_OFF_X +
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
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = mma8452_get_power_mode(data);
		if (ret < 0)
			return ret;

		i = mma8452_get_odr_index(data);

		*val = mma8452_os_ratio[ret][i];
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

/* returns >0 if active, 0 if in standby and <0 on error */
static int mma8452_is_active(struct mma8452_data *data)
{
	int reg;

	reg = i2c_smbus_read_byte_data(data->client, MMA8452_CTRL_REG1);
	if (reg < 0)
		return reg;

	return reg & MMA8452_CTRL_ACTIVE;
}

static int mma8452_change_config(struct mma8452_data *data, u8 reg, u8 val)
{
	int ret;
	int is_active;

	mutex_lock(&data->lock);

	is_active = mma8452_is_active(data);
	if (is_active < 0) {
		ret = is_active;
		goto fail;
	}

	/* config can only be changed when in standby */
	if (is_active > 0) {
		ret = mma8452_standby(data);
		if (ret < 0)
			goto fail;
	}

	ret = i2c_smbus_write_byte_data(data->client, reg, val);
	if (ret < 0)
		goto fail;

	if (is_active > 0) {
		ret = mma8452_active(data);
		if (ret < 0)
			goto fail;
	}

	ret = 0;
fail:
	mutex_unlock(&data->lock);

	return ret;
}

static int mma8452_set_power_mode(struct mma8452_data *data, u8 mode)
{
	int reg;

	reg = i2c_smbus_read_byte_data(data->client,
				       MMA8452_CTRL_REG2);
	if (reg < 0)
		return reg;

	reg &= ~MMA8452_CTRL_REG2_MODS_MASK;
	reg |= mode << MMA8452_CTRL_REG2_MODS_SHIFT;

	return mma8452_change_config(data, MMA8452_CTRL_REG2, reg);
}

/* returns >0 if in freefall mode, 0 if not or <0 if an error occurred */
static int mma8452_freefall_mode_enabled(struct mma8452_data *data)
{
	int val;

	val = i2c_smbus_read_byte_data(data->client, MMA8452_FF_MT_CFG);
	if (val < 0)
		return val;

	return !(val & MMA8452_FF_MT_CFG_OAE);
}

static int mma8452_set_freefall_mode(struct mma8452_data *data, bool state)
{
	int val;

	if ((state && mma8452_freefall_mode_enabled(data)) ||
	    (!state && !(mma8452_freefall_mode_enabled(data))))
		return 0;

	val = i2c_smbus_read_byte_data(data->client, MMA8452_FF_MT_CFG);
	if (val < 0)
		return val;

	if (state) {
		val |= BIT(idx_x + MMA8452_FF_MT_CHAN_SHIFT);
		val |= BIT(idx_y + MMA8452_FF_MT_CHAN_SHIFT);
		val |= BIT(idx_z + MMA8452_FF_MT_CHAN_SHIFT);
		val &= ~MMA8452_FF_MT_CFG_OAE;
	} else {
		val &= ~BIT(idx_x + MMA8452_FF_MT_CHAN_SHIFT);
		val &= ~BIT(idx_y + MMA8452_FF_MT_CHAN_SHIFT);
		val &= ~BIT(idx_z + MMA8452_FF_MT_CHAN_SHIFT);
		val |= MMA8452_FF_MT_CFG_OAE;
	}

	return mma8452_change_config(data, MMA8452_FF_MT_CFG, val);
}

static int mma8452_set_hp_filter_frequency(struct mma8452_data *data,
					   int val, int val2)
{
	int i, reg;

	i = mma8452_get_hp_filter_index(data, val, val2);
	if (i < 0)
		return i;

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

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = mma8452_get_samp_freq_index(data, val, val2);
		if (i < 0) {
			ret = i;
			break;
		}
		data->ctrl_reg1 &= ~MMA8452_CTRL_DR_MASK;
		data->ctrl_reg1 |= i << MMA8452_CTRL_DR_SHIFT;

		ret = mma8452_change_config(data, MMA8452_CTRL_REG1,
					    data->ctrl_reg1);
		break;
	case IIO_CHAN_INFO_SCALE:
		i = mma8452_get_scale_index(data, val, val2);
		if (i < 0) {
			ret = i;
			break;
		}

		data->data_cfg &= ~MMA8452_DATA_CFG_FS_MASK;
		data->data_cfg |= i;

		ret = mma8452_change_config(data, MMA8452_DATA_CFG,
					    data->data_cfg);
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < -128 || val > 127) {
			ret = -EINVAL;
			break;
		}

		ret = mma8452_change_config(data,
					    MMA8452_OFF_X + chan->scan_index,
					    val);
		break;

	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		if (val == 0 && val2 == 0) {
			data->data_cfg &= ~MMA8452_DATA_CFG_HPF_MASK;
		} else {
			data->data_cfg |= MMA8452_DATA_CFG_HPF_MASK;
			ret = mma8452_set_hp_filter_frequency(data, val, val2);
			if (ret < 0)
				break;
		}

		ret = mma8452_change_config(data, MMA8452_DATA_CFG,
					     data->data_cfg);
		break;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = mma8452_get_odr_index(data);

		for (i = 0; i < ARRAY_SIZE(mma8452_os_ratio); i++) {
			if (mma8452_os_ratio[i][ret] == val) {
				ret = mma8452_set_power_mode(data, i);
				break;
			}
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(indio_dev);
	return ret;
}

static int mma8452_get_event_regs(struct mma8452_data *data,
		const struct iio_chan_spec *chan, enum iio_event_direction dir,
		const struct mma8452_event_regs **ev_reg)
{
	if (!chan)
		return -EINVAL;

	switch (chan->type) {
	case IIO_ACCEL:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			if ((data->chip_info->all_events
					& MMA8452_INT_TRANS) &&
				(data->chip_info->enabled_events
					& MMA8452_INT_TRANS))
				*ev_reg = &trans_ev_regs;
			else
				*ev_reg = &ff_mt_ev_regs;
			return 0;
		case IIO_EV_DIR_FALLING:
			*ev_reg = &ff_mt_ev_regs;
			return 0;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int mma8452_read_event_value(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info,
			       int *val, int *val2)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret, us, power_mode;
	const struct mma8452_event_regs *ev_regs;

	ret = mma8452_get_event_regs(data, chan, dir, &ev_regs);
	if (ret)
		return ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		ret = i2c_smbus_read_byte_data(data->client, ev_regs->ev_ths);
		if (ret < 0)
			return ret;

		*val = ret & ev_regs->ev_ths_mask;

		return IIO_VAL_INT;

	case IIO_EV_INFO_PERIOD:
		ret = i2c_smbus_read_byte_data(data->client, ev_regs->ev_count);
		if (ret < 0)
			return ret;

		power_mode = mma8452_get_power_mode(data);
		if (power_mode < 0)
			return power_mode;

		us = ret * mma8452_time_step_us[power_mode][
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

static int mma8452_write_event_value(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret, reg, steps;
	const struct mma8452_event_regs *ev_regs;

	ret = mma8452_get_event_regs(data, chan, dir, &ev_regs);
	if (ret)
		return ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val < 0 || val > ev_regs->ev_ths_mask)
			return -EINVAL;

		return mma8452_change_config(data, ev_regs->ev_ths, val);

	case IIO_EV_INFO_PERIOD:
		ret = mma8452_get_power_mode(data);
		if (ret < 0)
			return ret;

		steps = (val * USEC_PER_SEC + val2) /
				mma8452_time_step_us[ret][
					mma8452_get_odr_index(data)];

		if (steps < 0 || steps > 0xff)
			return -EINVAL;

		return mma8452_change_config(data, ev_regs->ev_count, steps);

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
	const struct mma8452_event_regs *ev_regs;

	ret = mma8452_get_event_regs(data, chan, dir, &ev_regs);
	if (ret)
		return ret;

	switch (dir) {
	case IIO_EV_DIR_FALLING:
		return mma8452_freefall_mode_enabled(data);
	case IIO_EV_DIR_RISING:
		ret = i2c_smbus_read_byte_data(data->client,
				ev_regs->ev_cfg);
		if (ret < 0)
			return ret;

		return !!(ret & BIT(chan->scan_index +
				ev_regs->ev_cfg_chan_shift));
	default:
		return -EINVAL;
	}
}

static int mma8452_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	int val, ret;
	const struct mma8452_event_regs *ev_regs;

	ret = mma8452_get_event_regs(data, chan, dir, &ev_regs);
	if (ret)
		return ret;

	ret = mma8452_set_runtime_pm_state(data->client, state);
	if (ret)
		return ret;

	switch (dir) {
	case IIO_EV_DIR_FALLING:
		return mma8452_set_freefall_mode(data, state);
	case IIO_EV_DIR_RISING:
		val = i2c_smbus_read_byte_data(data->client, ev_regs->ev_cfg);
		if (val < 0)
			return val;

		if (state) {
			if (mma8452_freefall_mode_enabled(data)) {
				val &= ~BIT(idx_x + ev_regs->ev_cfg_chan_shift);
				val &= ~BIT(idx_y + ev_regs->ev_cfg_chan_shift);
				val &= ~BIT(idx_z + ev_regs->ev_cfg_chan_shift);
				val |= MMA8452_FF_MT_CFG_OAE;
			}
			val |= BIT(chan->scan_index +
					ev_regs->ev_cfg_chan_shift);
		} else {
			if (mma8452_freefall_mode_enabled(data))
				return 0;

			val &= ~BIT(chan->scan_index +
					ev_regs->ev_cfg_chan_shift);
		}

		val |= ev_regs->ev_cfg_ele;

		return mma8452_change_config(data, ev_regs->ev_cfg, val);
	default:
		return -EINVAL;
	}
}

static void mma8452_transient_interrupt(struct iio_dev *indio_dev)
{
	struct mma8452_data *data = iio_priv(indio_dev);
	s64 ts = iio_get_time_ns(indio_dev);
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

	if (!(src & data->chip_info->enabled_events))
		return IRQ_NONE;

	if (src & MMA8452_INT_DRDY) {
		iio_trigger_poll_chained(indio_dev->trig);
		ret = IRQ_HANDLED;
	}

	if (src & MMA8452_INT_FF_MT) {
		if (mma8452_freefall_mode_enabled(data)) {
			s64 ts = iio_get_time_ns(indio_dev);

			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0,
							  IIO_MOD_X_AND_Y_AND_Z,
							  IIO_EV_TYPE_MAG,
							  IIO_EV_DIR_FALLING),
					ts);
		}
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

	ret = mma8452_read(data, (__be16 *)buffer);
	if (ret < 0)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
					   iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int mma8452_reg_access_dbg(struct iio_dev *indio_dev,
				  unsigned int reg, unsigned int writeval,
				  unsigned int *readval)
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

static const struct iio_event_spec mma8452_freefall_event[] = {
	{
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
					BIT(IIO_EV_INFO_PERIOD) |
					BIT(IIO_EV_INFO_HIGH_PASS_FILTER_3DB)
	},
};

static const struct iio_event_spec mma8652_freefall_event[] = {
	{
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
					BIT(IIO_EV_INFO_PERIOD)
	},
};

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

static const struct iio_event_spec mma8452_motion_event[] = {
	{
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
					BIT(IIO_EV_INFO_PERIOD)
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
};

#define MMA8452_FREEFALL_CHANNEL(modifier) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = modifier, \
	.scan_index = -1, \
	.event_spec = mma8452_freefall_event, \
	.num_event_specs = ARRAY_SIZE(mma8452_freefall_event), \
}

#define MMA8652_FREEFALL_CHANNEL(modifier) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = modifier, \
	.scan_index = -1, \
	.event_spec = mma8652_freefall_event, \
	.num_event_specs = ARRAY_SIZE(mma8652_freefall_event), \
}

#define MMA8452_CHANNEL(axis, idx, bits) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_##axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			      BIT(IIO_CHAN_INFO_CALIBBIAS), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
			BIT(IIO_CHAN_INFO_SCALE) | \
			BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY) | \
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = idx, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 16 - (bits), \
		.endianness = IIO_BE, \
	}, \
	.event_spec = mma8452_transient_event, \
	.num_event_specs = ARRAY_SIZE(mma8452_transient_event), \
}

#define MMA8652_CHANNEL(axis, idx, bits) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_##axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.scan_index = idx, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 16 - (bits), \
		.endianness = IIO_BE, \
	}, \
	.event_spec = mma8452_motion_event, \
	.num_event_specs = ARRAY_SIZE(mma8452_motion_event), \
}

static const struct iio_chan_spec mma8451_channels[] = {
	MMA8452_CHANNEL(X, idx_x, 14),
	MMA8452_CHANNEL(Y, idx_y, 14),
	MMA8452_CHANNEL(Z, idx_z, 14),
	IIO_CHAN_SOFT_TIMESTAMP(idx_ts),
	MMA8452_FREEFALL_CHANNEL(IIO_MOD_X_AND_Y_AND_Z),
};

static const struct iio_chan_spec mma8452_channels[] = {
	MMA8452_CHANNEL(X, idx_x, 12),
	MMA8452_CHANNEL(Y, idx_y, 12),
	MMA8452_CHANNEL(Z, idx_z, 12),
	IIO_CHAN_SOFT_TIMESTAMP(idx_ts),
	MMA8452_FREEFALL_CHANNEL(IIO_MOD_X_AND_Y_AND_Z),
};

static const struct iio_chan_spec mma8453_channels[] = {
	MMA8452_CHANNEL(X, idx_x, 10),
	MMA8452_CHANNEL(Y, idx_y, 10),
	MMA8452_CHANNEL(Z, idx_z, 10),
	IIO_CHAN_SOFT_TIMESTAMP(idx_ts),
	MMA8452_FREEFALL_CHANNEL(IIO_MOD_X_AND_Y_AND_Z),
};

static const struct iio_chan_spec mma8652_channels[] = {
	MMA8652_CHANNEL(X, idx_x, 12),
	MMA8652_CHANNEL(Y, idx_y, 12),
	MMA8652_CHANNEL(Z, idx_z, 12),
	IIO_CHAN_SOFT_TIMESTAMP(idx_ts),
	MMA8652_FREEFALL_CHANNEL(IIO_MOD_X_AND_Y_AND_Z),
};

static const struct iio_chan_spec mma8653_channels[] = {
	MMA8652_CHANNEL(X, idx_x, 10),
	MMA8652_CHANNEL(Y, idx_y, 10),
	MMA8652_CHANNEL(Z, idx_z, 10),
	IIO_CHAN_SOFT_TIMESTAMP(idx_ts),
	MMA8652_FREEFALL_CHANNEL(IIO_MOD_X_AND_Y_AND_Z),
};

enum {
	mma8451,
	mma8452,
	mma8453,
	mma8652,
	mma8653,
	fxls8471,
};

static const struct mma_chip_info mma_chip_info_table[] = {
	[mma8451] = {
		.chip_id = MMA8451_DEVICE_ID,
		.channels = mma8451_channels,
		.num_channels = ARRAY_SIZE(mma8451_channels),
		/*
		 * Hardware has fullscale of -2G, -4G, -8G corresponding to
		 * raw value -8192 for 14 bit, -2048 for 12 bit or -512 for 10
		 * bit.
		 * The userspace interface uses m/s^2 and we declare micro units
		 * So scale factor for 12 bit here is given by:
		 *	g * N * 1000000 / 2048 for N = 2, 4, 8 and g=9.80665
		 */
		.mma_scales = { {0, 2394}, {0, 4788}, {0, 9577} },
		/*
		 * Although we enable the interrupt sources once and for
		 * all here the event detection itself is not enabled until
		 * userspace asks for it by mma8452_write_event_config()
		 */
		.all_events = MMA8452_INT_DRDY |
					MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
		.enabled_events = MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
	},
	[mma8452] = {
		.chip_id = MMA8452_DEVICE_ID,
		.channels = mma8452_channels,
		.num_channels = ARRAY_SIZE(mma8452_channels),
		.mma_scales = { {0, 9577}, {0, 19154}, {0, 38307} },
		/*
		 * Although we enable the interrupt sources once and for
		 * all here the event detection itself is not enabled until
		 * userspace asks for it by mma8452_write_event_config()
		 */
		.all_events = MMA8452_INT_DRDY |
					MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
		.enabled_events = MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
	},
	[mma8453] = {
		.chip_id = MMA8453_DEVICE_ID,
		.channels = mma8453_channels,
		.num_channels = ARRAY_SIZE(mma8453_channels),
		.mma_scales = { {0, 38307}, {0, 76614}, {0, 153228} },
		/*
		 * Although we enable the interrupt sources once and for
		 * all here the event detection itself is not enabled until
		 * userspace asks for it by mma8452_write_event_config()
		 */
		.all_events = MMA8452_INT_DRDY |
					MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
		.enabled_events = MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
	},
	[mma8652] = {
		.chip_id = MMA8652_DEVICE_ID,
		.channels = mma8652_channels,
		.num_channels = ARRAY_SIZE(mma8652_channels),
		.mma_scales = { {0, 9577}, {0, 19154}, {0, 38307} },
		.all_events = MMA8452_INT_DRDY |
					MMA8452_INT_FF_MT,
		.enabled_events = MMA8452_INT_FF_MT,
	},
	[mma8653] = {
		.chip_id = MMA8653_DEVICE_ID,
		.channels = mma8653_channels,
		.num_channels = ARRAY_SIZE(mma8653_channels),
		.mma_scales = { {0, 38307}, {0, 76614}, {0, 153228} },
		/*
		 * Although we enable the interrupt sources once and for
		 * all here the event detection itself is not enabled until
		 * userspace asks for it by mma8452_write_event_config()
		 */
		.all_events = MMA8452_INT_DRDY |
					MMA8452_INT_FF_MT,
		.enabled_events = MMA8452_INT_FF_MT,
	},
	[fxls8471] = {
		.chip_id = FXLS8471_DEVICE_ID,
		.channels = mma8451_channels,
		.num_channels = ARRAY_SIZE(mma8451_channels),
		.mma_scales = { {0, 2394}, {0, 4788}, {0, 9577} },
		/*
		 * Although we enable the interrupt sources once and for
		 * all here the event detection itself is not enabled until
		 * userspace asks for it by mma8452_write_event_config()
		 */
		.all_events = MMA8452_INT_DRDY |
					MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
		.enabled_events = MMA8452_INT_TRANS |
					MMA8452_INT_FF_MT,
	},
};

static struct attribute *mma8452_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_scale_available.dev_attr.attr,
	&iio_dev_attr_in_accel_filter_high_pass_3db_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_oversampling_ratio_available.dev_attr.attr,
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
	.read_event_value = &mma8452_read_event_value,
	.write_event_value = &mma8452_write_event_value,
	.read_event_config = &mma8452_read_event_config,
	.write_event_config = &mma8452_write_event_config,
	.debugfs_reg_access = &mma8452_reg_access_dbg,
};

static const unsigned long mma8452_scan_masks[] = {0x7, 0};

static int mma8452_data_rdy_trigger_set_state(struct iio_trigger *trig,
					      bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mma8452_data *data = iio_priv(indio_dev);
	int reg, ret;

	ret = mma8452_set_runtime_pm_state(data->client, state);
	if (ret)
		return ret;

	reg = i2c_smbus_read_byte_data(data->client, MMA8452_CTRL_REG4);
	if (reg < 0)
		return reg;

	if (state)
		reg |= MMA8452_INT_DRDY;
	else
		reg &= ~MMA8452_INT_DRDY;

	return mma8452_change_config(data, MMA8452_CTRL_REG4, reg);
}

static const struct iio_trigger_ops mma8452_trigger_ops = {
	.set_trigger_state = mma8452_data_rdy_trigger_set_state,
	.validate_device = iio_trigger_validate_own_device,
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

static const struct of_device_id mma8452_dt_ids[] = {
	{ .compatible = "fsl,mma8451", .data = &mma_chip_info_table[mma8451] },
	{ .compatible = "fsl,mma8452", .data = &mma_chip_info_table[mma8452] },
	{ .compatible = "fsl,mma8453", .data = &mma_chip_info_table[mma8453] },
	{ .compatible = "fsl,mma8652", .data = &mma_chip_info_table[mma8652] },
	{ .compatible = "fsl,mma8653", .data = &mma_chip_info_table[mma8653] },
	{ .compatible = "fsl,fxls8471", .data = &mma_chip_info_table[fxls8471] },
	{ }
};
MODULE_DEVICE_TABLE(of, mma8452_dt_ids);

static int mma8452_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mma8452_data *data;
	struct iio_dev *indio_dev;
	int ret;
	const struct of_device_id *match;

	match = of_match_device(mma8452_dt_ids, &client->dev);
	if (!match) {
		dev_err(&client->dev, "unknown device model\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);
	data->chip_info = match->data;

	ret = i2c_smbus_read_byte_data(client, MMA8452_WHO_AM_I);
	if (ret < 0)
		return ret;

	switch (ret) {
	case MMA8451_DEVICE_ID:
	case MMA8452_DEVICE_ID:
	case MMA8453_DEVICE_ID:
	case MMA8652_DEVICE_ID:
	case MMA8653_DEVICE_ID:
	case FXLS8471_DEVICE_ID:
		if (ret == data->chip_info->chip_id)
			break;
	default:
		return -ENODEV;
	}

	dev_info(&client->dev, "registering %s accelerometer; ID 0x%x\n",
		 match->compatible, data->chip_info->chip_id);

	i2c_set_clientdata(client, indio_dev);
	indio_dev->info = &mma8452_info;
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = data->chip_info->channels;
	indio_dev->num_channels = data->chip_info->num_channels;
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
		int irq2;

		irq2 = of_irq_get_byname(client->dev.of_node, "INT2");

		if (irq2 == client->irq) {
			dev_dbg(&client->dev, "using interrupt line INT2\n");
		} else {
			ret = i2c_smbus_write_byte_data(client,
						MMA8452_CTRL_REG5,
						data->chip_info->all_events);
			if (ret < 0)
				return ret;

			dev_dbg(&client->dev, "using interrupt line INT1\n");
		}

		ret = i2c_smbus_write_byte_data(client,
					MMA8452_CTRL_REG4,
					data->chip_info->enabled_events);
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

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto buffer_cleanup;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev,
					 MMA8452_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;

	ret = mma8452_set_freefall_mode(data, false);
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

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	iio_triggered_buffer_cleanup(indio_dev);
	mma8452_trigger_cleanup(indio_dev);
	mma8452_standby(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM
static int mma8452_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = mma8452_standby(data);
	mutex_unlock(&data->lock);
	if (ret < 0) {
		dev_err(&data->client->dev, "powering off device failed\n");
		return -EAGAIN;
	}

	return 0;
}

static int mma8452_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct mma8452_data *data = iio_priv(indio_dev);
	int ret, sleep_val;

	ret = mma8452_active(data);
	if (ret < 0)
		return ret;

	ret = mma8452_get_odr_index(data);
	sleep_val = 1000 / mma8452_samp_freq[ret][0];
	if (sleep_val < 20)
		usleep_range(sleep_val * 1000, 20000);
	else
		msleep_interruptible(sleep_val);

	return 0;
}
#endif

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
#endif

static const struct dev_pm_ops mma8452_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mma8452_suspend, mma8452_resume)
	SET_RUNTIME_PM_OPS(mma8452_runtime_suspend,
			   mma8452_runtime_resume, NULL)
};

static const struct i2c_device_id mma8452_id[] = {
	{ "mma8451", mma8451 },
	{ "mma8452", mma8452 },
	{ "mma8453", mma8453 },
	{ "mma8652", mma8652 },
	{ "mma8653", mma8653 },
	{ "fxls8471", fxls8471 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mma8452_id);

static struct i2c_driver mma8452_driver = {
	.driver = {
		.name	= "mma8452",
		.of_match_table = of_match_ptr(mma8452_dt_ids),
		.pm	= &mma8452_pm_ops,
	},
	.probe = mma8452_probe,
	.remove = mma8452_remove,
	.id_table = mma8452_id,
};
module_i2c_driver(mma8452_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Freescale / NXP MMA8452 accelerometer driver");
MODULE_LICENSE("GPL");
