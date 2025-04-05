// SPDX-License-Identifier: GPL-2.0-only
/*
 * RPR-0521 ROHM Ambient Light and Proximity Sensor
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * IIO driver for RPR-0521RS (7-bit I2C slave address 0x38).
 *
 * TODO: illuminance channel
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/pm_runtime.h>

#define RPR0521_REG_SYSTEM_CTRL		0x40
#define RPR0521_REG_MODE_CTRL		0x41
#define RPR0521_REG_ALS_CTRL		0x42
#define RPR0521_REG_PXS_CTRL		0x43
#define RPR0521_REG_PXS_DATA		0x44 /* 16-bit, little endian */
#define RPR0521_REG_ALS_DATA0		0x46 /* 16-bit, little endian */
#define RPR0521_REG_ALS_DATA1		0x48 /* 16-bit, little endian */
#define RPR0521_REG_INTERRUPT		0x4A
#define RPR0521_REG_PS_OFFSET_LSB	0x53
#define RPR0521_REG_ID			0x92

#define RPR0521_MODE_ALS_MASK		BIT(7)
#define RPR0521_MODE_PXS_MASK		BIT(6)
#define RPR0521_MODE_MEAS_TIME_MASK	GENMASK(3, 0)
#define RPR0521_ALS_DATA0_GAIN_MASK	GENMASK(5, 4)
#define RPR0521_ALS_DATA0_GAIN_SHIFT	4
#define RPR0521_ALS_DATA1_GAIN_MASK	GENMASK(3, 2)
#define RPR0521_ALS_DATA1_GAIN_SHIFT	2
#define RPR0521_PXS_GAIN_MASK		GENMASK(5, 4)
#define RPR0521_PXS_GAIN_SHIFT		4
#define RPR0521_PXS_PERSISTENCE_MASK	GENMASK(3, 0)
#define RPR0521_INTERRUPT_INT_TRIG_PS_MASK	BIT(0)
#define RPR0521_INTERRUPT_INT_TRIG_ALS_MASK	BIT(1)
#define RPR0521_INTERRUPT_INT_REASSERT_MASK	BIT(3)
#define RPR0521_INTERRUPT_ALS_INT_STATUS_MASK	BIT(6)
#define RPR0521_INTERRUPT_PS_INT_STATUS_MASK	BIT(7)

#define RPR0521_MODE_ALS_ENABLE		BIT(7)
#define RPR0521_MODE_ALS_DISABLE	0x00
#define RPR0521_MODE_PXS_ENABLE		BIT(6)
#define RPR0521_MODE_PXS_DISABLE	0x00
#define RPR0521_PXS_PERSISTENCE_DRDY	0x00

#define RPR0521_INTERRUPT_INT_TRIG_PS_ENABLE	BIT(0)
#define RPR0521_INTERRUPT_INT_TRIG_PS_DISABLE	0x00
#define RPR0521_INTERRUPT_INT_TRIG_ALS_ENABLE	BIT(1)
#define RPR0521_INTERRUPT_INT_TRIG_ALS_DISABLE	0x00
#define RPR0521_INTERRUPT_INT_REASSERT_ENABLE	BIT(3)
#define RPR0521_INTERRUPT_INT_REASSERT_DISABLE	0x00

#define RPR0521_MANUFACT_ID		0xE0
#define RPR0521_DEFAULT_MEAS_TIME	0x06 /* ALS - 100ms, PXS - 100ms */

#define RPR0521_DRV_NAME		"RPR0521"
#define RPR0521_IRQ_NAME		"rpr0521_event"
#define RPR0521_REGMAP_NAME		"rpr0521_regmap"

#define RPR0521_SLEEP_DELAY_MS	2000

#define RPR0521_ALS_SCALE_AVAIL "0.007812 0.015625 0.5 1"
#define RPR0521_PXS_SCALE_AVAIL "0.125 0.5 1"

struct rpr0521_gain {
	int scale;
	int uscale;
};

static const struct rpr0521_gain rpr0521_als_gain[4] = {
	{1, 0},		/* x1 */
	{0, 500000},	/* x2 */
	{0, 15625},	/* x64 */
	{0, 7812},	/* x128 */
};

static const struct rpr0521_gain rpr0521_pxs_gain[3] = {
	{1, 0},		/* x1 */
	{0, 500000},	/* x2 */
	{0, 125000},	/* x4 */
};

enum rpr0521_channel {
	RPR0521_CHAN_PXS,
	RPR0521_CHAN_ALS_DATA0,
	RPR0521_CHAN_ALS_DATA1,
};

struct rpr0521_reg_desc {
	u8 address;
	u8 device_mask;
};

static const struct rpr0521_reg_desc rpr0521_data_reg[] = {
	[RPR0521_CHAN_PXS]	= {
		.address	= RPR0521_REG_PXS_DATA,
		.device_mask	= RPR0521_MODE_PXS_MASK,
	},
	[RPR0521_CHAN_ALS_DATA0] = {
		.address	= RPR0521_REG_ALS_DATA0,
		.device_mask	= RPR0521_MODE_ALS_MASK,
	},
	[RPR0521_CHAN_ALS_DATA1] = {
		.address	= RPR0521_REG_ALS_DATA1,
		.device_mask	= RPR0521_MODE_ALS_MASK,
	},
};

static const struct rpr0521_gain_info {
	u8 reg;
	u8 mask;
	u8 shift;
	const struct rpr0521_gain *gain;
	int size;
} rpr0521_gain[] = {
	[RPR0521_CHAN_PXS] = {
		.reg	= RPR0521_REG_PXS_CTRL,
		.mask	= RPR0521_PXS_GAIN_MASK,
		.shift	= RPR0521_PXS_GAIN_SHIFT,
		.gain	= rpr0521_pxs_gain,
		.size	= ARRAY_SIZE(rpr0521_pxs_gain),
	},
	[RPR0521_CHAN_ALS_DATA0] = {
		.reg	= RPR0521_REG_ALS_CTRL,
		.mask	= RPR0521_ALS_DATA0_GAIN_MASK,
		.shift	= RPR0521_ALS_DATA0_GAIN_SHIFT,
		.gain	= rpr0521_als_gain,
		.size	= ARRAY_SIZE(rpr0521_als_gain),
	},
	[RPR0521_CHAN_ALS_DATA1] = {
		.reg	= RPR0521_REG_ALS_CTRL,
		.mask	= RPR0521_ALS_DATA1_GAIN_MASK,
		.shift	= RPR0521_ALS_DATA1_GAIN_SHIFT,
		.gain	= rpr0521_als_gain,
		.size	= ARRAY_SIZE(rpr0521_als_gain),
	},
};

struct rpr0521_samp_freq {
	int	als_hz;
	int	als_uhz;
	int	pxs_hz;
	int	pxs_uhz;
};

static const struct rpr0521_samp_freq rpr0521_samp_freq_i[13] = {
/*	{ALS, PXS},		   W==currently writable option */
	{0, 0, 0, 0},		/* W0000, 0=standby */
	{0, 0, 100, 0},		/*  0001 */
	{0, 0, 25, 0},		/*  0010 */
	{0, 0, 10, 0},		/*  0011 */
	{0, 0, 2, 500000},	/*  0100 */
	{10, 0, 20, 0},		/*  0101 */
	{10, 0, 10, 0},		/* W0110 */
	{10, 0, 2, 500000},	/*  0111 */
	{2, 500000, 20, 0},	/*  1000, measurement 100ms, sleep 300ms */
	{2, 500000, 10, 0},	/*  1001, measurement 100ms, sleep 300ms */
	{2, 500000, 0, 0},	/*  1010, high sensitivity mode */
	{2, 500000, 2, 500000},	/* W1011, high sensitivity mode */
	{20, 0, 20, 0}	/* 1100, ALS_data x 0.5, see specification P.18 */
};

struct rpr0521_data {
	struct i2c_client *client;

	/* protect device params updates (e.g state, gain) */
	struct mutex lock;

	/* device active status */
	bool als_dev_en;
	bool pxs_dev_en;

	struct iio_trigger *drdy_trigger0;
	s64 irq_timestamp;

	/* optimize runtime pm ops - enable/disable device only if needed */
	bool als_ps_need_en;
	bool pxs_ps_need_en;
	bool als_need_dis;
	bool pxs_need_dis;

	struct regmap *regmap;

	/*
	 * Ensure correct naturally aligned timestamp.
	 * Note that the read will put garbage data into
	 * the padding but this should not be a problem
	 */
	struct {
		__le16 channels[3];
		u8 garbage;
		aligned_s64 ts;
	} scan;
};

static IIO_CONST_ATTR(in_intensity_scale_available, RPR0521_ALS_SCALE_AVAIL);
static IIO_CONST_ATTR(in_proximity_scale_available, RPR0521_PXS_SCALE_AVAIL);

/*
 * Start with easy freq first, whole table of freq combinations is more
 * complicated.
 */
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("2.5 10");

static struct attribute *rpr0521_attributes[] = {
	&iio_const_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_in_proximity_scale_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group rpr0521_attribute_group = {
	.attrs = rpr0521_attributes,
};

/* Order of the channel data in buffer */
enum rpr0521_scan_index_order {
	RPR0521_CHAN_INDEX_PXS,
	RPR0521_CHAN_INDEX_BOTH,
	RPR0521_CHAN_INDEX_IR,
};

static const unsigned long rpr0521_available_scan_masks[] = {
	BIT(RPR0521_CHAN_INDEX_PXS) | BIT(RPR0521_CHAN_INDEX_BOTH) |
	BIT(RPR0521_CHAN_INDEX_IR),
	0
};

static const struct iio_chan_spec rpr0521_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.address = RPR0521_CHAN_PXS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = RPR0521_CHAN_INDEX_PXS,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.address = RPR0521_CHAN_ALS_DATA0,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = RPR0521_CHAN_INDEX_BOTH,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.modified = 1,
		.address = RPR0521_CHAN_ALS_DATA1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = RPR0521_CHAN_INDEX_IR,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
};

static int rpr0521_als_enable(struct rpr0521_data *data, u8 status)
{
	int ret;

	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_ALS_MASK,
				 status);
	if (ret < 0)
		return ret;

	if (status & RPR0521_MODE_ALS_MASK)
		data->als_dev_en = true;
	else
		data->als_dev_en = false;

	return 0;
}

static int rpr0521_pxs_enable(struct rpr0521_data *data, u8 status)
{
	int ret;

	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_PXS_MASK,
				 status);
	if (ret < 0)
		return ret;

	if (status & RPR0521_MODE_PXS_MASK)
		data->pxs_dev_en = true;
	else
		data->pxs_dev_en = false;

	return 0;
}

/**
 * rpr0521_set_power_state - handles runtime PM state and sensors enabled status
 *
 * @data: rpr0521 device private data
 * @on: state to be set for devices in @device_mask
 * @device_mask: bitmask specifying for which device we need to update @on state
 *
 * Calls for this function must be balanced so that each ON should have matching
 * OFF. Otherwise pm usage_count gets out of sync.
 */
static int rpr0521_set_power_state(struct rpr0521_data *data, bool on,
				   u8 device_mask)
{
#ifdef CONFIG_PM
	int ret;

	if (device_mask & RPR0521_MODE_ALS_MASK) {
		data->als_ps_need_en = on;
		data->als_need_dis = !on;
	}

	if (device_mask & RPR0521_MODE_PXS_MASK) {
		data->pxs_ps_need_en = on;
		data->pxs_need_dis = !on;
	}

	/*
	 * On: _resume() is called only when we are suspended
	 * Off: _suspend() is called after delay if _resume() is not
	 * called before that.
	 * Note: If either measurement is re-enabled before _suspend(),
	 * both stay enabled until _suspend().
	 */
	if (on) {
		ret = pm_runtime_resume_and_get(&data->client->dev);
	} else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed: rpr0521_set_power_state for %d, ret %d\n",
			on, ret);
		return ret;
	}

	if (on) {
		/* If _resume() was not called, enable measurement now. */
		if (data->als_ps_need_en) {
			ret = rpr0521_als_enable(data, RPR0521_MODE_ALS_ENABLE);
			if (ret)
				return ret;
			data->als_ps_need_en = false;
		}

		if (data->pxs_ps_need_en) {
			ret = rpr0521_pxs_enable(data, RPR0521_MODE_PXS_ENABLE);
			if (ret)
				return ret;
			data->pxs_ps_need_en = false;
		}
	}
#endif
	return 0;
}

/* Interrupt register tells if this sensor caused the interrupt or not. */
static inline bool rpr0521_is_triggered(struct rpr0521_data *data)
{
	int ret;
	int reg;

	ret = regmap_read(data->regmap, RPR0521_REG_INTERRUPT, &reg);
	if (ret < 0)
		return false;   /* Reg read failed. */
	if (reg &
	    (RPR0521_INTERRUPT_ALS_INT_STATUS_MASK |
	    RPR0521_INTERRUPT_PS_INT_STATUS_MASK))
		return true;
	else
		return false;   /* Int not from this sensor. */
}

/* IRQ to trigger handler */
static irqreturn_t rpr0521_drdy_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct rpr0521_data *data = iio_priv(indio_dev);

	data->irq_timestamp = iio_get_time_ns(indio_dev);
	/*
	 * We need to wake the thread to read the interrupt reg. It
	 * is not possible to do that here because regmap_read takes a
	 * mutex.
	 */

	return IRQ_WAKE_THREAD;
}

static irqreturn_t rpr0521_drdy_irq_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct rpr0521_data *data = iio_priv(indio_dev);

	if (rpr0521_is_triggered(data)) {
		iio_trigger_poll_nested(data->drdy_trigger0);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t rpr0521_trigger_consumer_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct rpr0521_data *data = iio_priv(indio_dev);
	int err;

	/* Use irq timestamp when reasonable. */
	if (iio_trigger_using_own(indio_dev) && data->irq_timestamp) {
		pf->timestamp = data->irq_timestamp;
		data->irq_timestamp = 0;
	}
	/* Other chained trigger polls get timestamp only here. */
	if (!pf->timestamp)
		pf->timestamp = iio_get_time_ns(indio_dev);

	err = regmap_bulk_read(data->regmap, RPR0521_REG_PXS_DATA,
		data->scan.channels,
		(3 * 2) + 1);	/* 3 * 16-bit + (discarded) int clear reg. */
	if (!err)
		iio_push_to_buffers_with_timestamp(indio_dev,
						   &data->scan, pf->timestamp);
	else
		dev_err(&data->client->dev,
			"Trigger consumer can't read from sensor.\n");
	pf->timestamp = 0;

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int rpr0521_write_int_enable(struct rpr0521_data *data)
{
	int err;

	/* Interrupt after each measurement */
	err = regmap_update_bits(data->regmap, RPR0521_REG_PXS_CTRL,
		RPR0521_PXS_PERSISTENCE_MASK,
		RPR0521_PXS_PERSISTENCE_DRDY);
	if (err) {
		dev_err(&data->client->dev, "PS control reg write fail.\n");
		return -EBUSY;
		}

	/* Ignore latch and mode because of drdy */
	err = regmap_write(data->regmap, RPR0521_REG_INTERRUPT,
		RPR0521_INTERRUPT_INT_REASSERT_DISABLE |
		RPR0521_INTERRUPT_INT_TRIG_ALS_DISABLE |
		RPR0521_INTERRUPT_INT_TRIG_PS_ENABLE
		);
	if (err) {
		dev_err(&data->client->dev, "Interrupt setup write fail.\n");
		return -EBUSY;
		}

	return 0;
}

static int rpr0521_write_int_disable(struct rpr0521_data *data)
{
	/* Don't care of clearing mode, assert and latch. */
	return regmap_write(data->regmap, RPR0521_REG_INTERRUPT,
				RPR0521_INTERRUPT_INT_TRIG_ALS_DISABLE |
				RPR0521_INTERRUPT_INT_TRIG_PS_DISABLE
				);
}

/*
 * Trigger producer enable / disable. Note that there will be trigs only when
 * measurement data is ready to be read.
 */
static int rpr0521_pxs_drdy_set_state(struct iio_trigger *trigger,
	bool enable_drdy)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trigger);
	struct rpr0521_data *data = iio_priv(indio_dev);
	int err;

	if (enable_drdy)
		err = rpr0521_write_int_enable(data);
	else
		err = rpr0521_write_int_disable(data);
	if (err)
		dev_err(&data->client->dev, "rpr0521_pxs_drdy_set_state failed\n");

	return err;
}

static const struct iio_trigger_ops rpr0521_trigger_ops = {
	.set_trigger_state = rpr0521_pxs_drdy_set_state,
	};


static int rpr0521_buffer_preenable(struct iio_dev *indio_dev)
{
	int err;
	struct rpr0521_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	err = rpr0521_set_power_state(data, true,
		(RPR0521_MODE_PXS_MASK | RPR0521_MODE_ALS_MASK));
	mutex_unlock(&data->lock);
	if (err)
		dev_err(&data->client->dev, "_buffer_preenable fail\n");

	return err;
}

static int rpr0521_buffer_postdisable(struct iio_dev *indio_dev)
{
	int err;
	struct rpr0521_data *data = iio_priv(indio_dev);

	mutex_lock(&data->lock);
	err = rpr0521_set_power_state(data, false,
		(RPR0521_MODE_PXS_MASK | RPR0521_MODE_ALS_MASK));
	mutex_unlock(&data->lock);
	if (err)
		dev_err(&data->client->dev, "_buffer_postdisable fail\n");

	return err;
}

static const struct iio_buffer_setup_ops rpr0521_buffer_setup_ops = {
	.preenable = rpr0521_buffer_preenable,
	.postdisable = rpr0521_buffer_postdisable,
};

static int rpr0521_get_gain(struct rpr0521_data *data, int chan,
			    int *val, int *val2)
{
	int ret, reg, idx;

	ret = regmap_read(data->regmap, rpr0521_gain[chan].reg, &reg);
	if (ret < 0)
		return ret;

	idx = (rpr0521_gain[chan].mask & reg) >> rpr0521_gain[chan].shift;
	*val = rpr0521_gain[chan].gain[idx].scale;
	*val2 = rpr0521_gain[chan].gain[idx].uscale;

	return 0;
}

static int rpr0521_set_gain(struct rpr0521_data *data, int chan,
			    int val, int val2)
{
	int i, idx = -EINVAL;

	/* get gain index */
	for (i = 0; i < rpr0521_gain[chan].size; i++)
		if (val == rpr0521_gain[chan].gain[i].scale &&
		    val2 == rpr0521_gain[chan].gain[i].uscale) {
			idx = i;
			break;
		}

	if (idx < 0)
		return idx;

	return regmap_update_bits(data->regmap, rpr0521_gain[chan].reg,
				  rpr0521_gain[chan].mask,
				  idx << rpr0521_gain[chan].shift);
}

static int rpr0521_read_samp_freq(struct rpr0521_data *data,
				enum iio_chan_type chan_type,
			    int *val, int *val2)
{
	int reg, ret;

	ret = regmap_read(data->regmap, RPR0521_REG_MODE_CTRL, &reg);
	if (ret < 0)
		return ret;

	reg &= RPR0521_MODE_MEAS_TIME_MASK;
	if (reg >= ARRAY_SIZE(rpr0521_samp_freq_i))
		return -EINVAL;

	switch (chan_type) {
	case IIO_INTENSITY:
		*val = rpr0521_samp_freq_i[reg].als_hz;
		*val2 = rpr0521_samp_freq_i[reg].als_uhz;
		return 0;

	case IIO_PROXIMITY:
		*val = rpr0521_samp_freq_i[reg].pxs_hz;
		*val2 = rpr0521_samp_freq_i[reg].pxs_uhz;
		return 0;

	default:
		return -EINVAL;
	}
}

static int rpr0521_write_samp_freq_common(struct rpr0521_data *data,
				enum iio_chan_type chan_type,
				int val, int val2)
{
	int i;

	/*
	 * Ignore channel
	 * both pxs and als are setup only to same freq because of simplicity
	 */
	switch (val) {
	case 0:
		i = 0;
		break;

	case 2:
		if (val2 != 500000)
			return -EINVAL;

		i = 11;
		break;

	case 10:
		i = 6;
		break;

	default:
		return -EINVAL;
	}

	return regmap_update_bits(data->regmap,
		RPR0521_REG_MODE_CTRL,
		RPR0521_MODE_MEAS_TIME_MASK,
		i);
}

static int rpr0521_read_ps_offset(struct rpr0521_data *data, int *offset)
{
	int ret;
	__le16 buffer;

	ret = regmap_bulk_read(data->regmap,
		RPR0521_REG_PS_OFFSET_LSB, &buffer, sizeof(buffer));

	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to read PS OFFSET register\n");
		return ret;
	}
	*offset = le16_to_cpu(buffer);

	return ret;
}

static int rpr0521_write_ps_offset(struct rpr0521_data *data, int offset)
{
	int ret;
	__le16 buffer;

	buffer = cpu_to_le16(offset & 0x3ff);
	ret = regmap_raw_write(data->regmap,
		RPR0521_REG_PS_OFFSET_LSB, &buffer, sizeof(buffer));

	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to write PS OFFSET register\n");
		return ret;
	}

	return ret;
}

static int rpr0521_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;
	int busy;
	u8 device_mask;
	__le16 raw_data;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_INTENSITY && chan->type != IIO_PROXIMITY)
			return -EINVAL;

		busy = iio_device_claim_direct_mode(indio_dev);
		if (busy)
			return -EBUSY;

		device_mask = rpr0521_data_reg[chan->address].device_mask;

		mutex_lock(&data->lock);
		ret = rpr0521_set_power_state(data, true, device_mask);
		if (ret < 0)
			goto rpr0521_read_raw_out;

		ret = regmap_bulk_read(data->regmap,
				       rpr0521_data_reg[chan->address].address,
				       &raw_data, sizeof(raw_data));
		if (ret < 0) {
			rpr0521_set_power_state(data, false, device_mask);
			goto rpr0521_read_raw_out;
		}

		ret = rpr0521_set_power_state(data, false, device_mask);

rpr0521_read_raw_out:
		mutex_unlock(&data->lock);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		*val = le16_to_cpu(raw_data);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&data->lock);
		ret = rpr0521_get_gain(data, chan->address, val, val2);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->lock);
		ret = rpr0521_read_samp_freq(data, chan->type, val, val2);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_OFFSET:
		mutex_lock(&data->lock);
		ret = rpr0521_read_ps_offset(data, val);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int rpr0521_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&data->lock);
		ret = rpr0521_set_gain(data, chan->address, val, val2);
		mutex_unlock(&data->lock);

		return ret;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->lock);
		ret = rpr0521_write_samp_freq_common(data, chan->type,
						     val, val2);
		mutex_unlock(&data->lock);

		return ret;

	case IIO_CHAN_INFO_OFFSET:
		mutex_lock(&data->lock);
		ret = rpr0521_write_ps_offset(data, val);
		mutex_unlock(&data->lock);

		return ret;

	default:
		return -EINVAL;
	}
}

static const struct iio_info rpr0521_info = {
	.read_raw	= rpr0521_read_raw,
	.write_raw	= rpr0521_write_raw,
	.attrs		= &rpr0521_attribute_group,
};

static int rpr0521_init(struct rpr0521_data *data)
{
	int ret;
	int id;

	ret = regmap_read(data->regmap, RPR0521_REG_ID, &id);
	if (ret < 0) {
		dev_err(&data->client->dev, "Failed to read REG_ID register\n");
		return ret;
	}

	if (id != RPR0521_MANUFACT_ID) {
		dev_err(&data->client->dev, "Wrong id, got %x, expected %x\n",
			id, RPR0521_MANUFACT_ID);
		return -ENODEV;
	}

	/* set default measurement time - 100 ms for both ALS and PS */
	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_MEAS_TIME_MASK,
				 RPR0521_DEFAULT_MEAS_TIME);
	if (ret) {
		pr_err("regmap_update_bits returned %d\n", ret);
		return ret;
	}

#ifndef CONFIG_PM
	ret = rpr0521_als_enable(data, RPR0521_MODE_ALS_ENABLE);
	if (ret < 0)
		return ret;
	ret = rpr0521_pxs_enable(data, RPR0521_MODE_PXS_ENABLE);
	if (ret < 0)
		return ret;
#endif

	data->irq_timestamp = 0;

	return 0;
}

static int rpr0521_poweroff(struct rpr0521_data *data)
{
	int ret;
	int tmp;

	ret = regmap_update_bits(data->regmap, RPR0521_REG_MODE_CTRL,
				 RPR0521_MODE_ALS_MASK |
				 RPR0521_MODE_PXS_MASK,
				 RPR0521_MODE_ALS_DISABLE |
				 RPR0521_MODE_PXS_DISABLE);
	if (ret < 0)
		return ret;

	data->als_dev_en = false;
	data->pxs_dev_en = false;

	/*
	 * Int pin keeps state after power off. Set pin to high impedance
	 * mode to prevent power drain.
	 */
	ret = regmap_read(data->regmap, RPR0521_REG_INTERRUPT, &tmp);
	if (ret) {
		dev_err(&data->client->dev, "Failed to reset int pin.\n");
		return ret;
	}

	return 0;
}

static bool rpr0521_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RPR0521_REG_MODE_CTRL:
	case RPR0521_REG_ALS_CTRL:
	case RPR0521_REG_PXS_CTRL:
		return false;
	default:
		return true;
	}
}

static const struct regmap_config rpr0521_regmap_config = {
	.name		= RPR0521_REGMAP_NAME,

	.reg_bits	= 8,
	.val_bits	= 8,

	.max_register	= RPR0521_REG_ID,
	.cache_type	= REGCACHE_RBTREE,
	.volatile_reg	= rpr0521_is_volatile_reg,
};

static int rpr0521_probe(struct i2c_client *client)
{
	struct rpr0521_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &rpr0521_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "regmap_init failed!\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	mutex_init(&data->lock);

	indio_dev->info = &rpr0521_info;
	indio_dev->name = RPR0521_DRV_NAME;
	indio_dev->channels = rpr0521_channels;
	indio_dev->num_channels = ARRAY_SIZE(rpr0521_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = rpr0521_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "rpr0521 chip init failed\n");
		return ret;
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto err_poweroff;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, RPR0521_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	/*
	 * If sensor write/read is needed in _probe after _use_autosuspend,
	 * sensor needs to be _resumed first using rpr0521_set_power_state().
	 */

	/* IRQ to trigger setup */
	if (client->irq) {
		/* Trigger0 producer setup */
		data->drdy_trigger0 = devm_iio_trigger_alloc(
			indio_dev->dev.parent,
			"%s-dev%d", indio_dev->name, iio_device_id(indio_dev));
		if (!data->drdy_trigger0) {
			ret = -ENOMEM;
			goto err_pm_disable;
		}
		data->drdy_trigger0->ops = &rpr0521_trigger_ops;
		indio_dev->available_scan_masks = rpr0521_available_scan_masks;
		iio_trigger_set_drvdata(data->drdy_trigger0, indio_dev);

		/* Ties irq to trigger producer handler. */
		ret = devm_request_threaded_irq(&client->dev, client->irq,
			rpr0521_drdy_irq_handler, rpr0521_drdy_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			RPR0521_IRQ_NAME, indio_dev);
		if (ret < 0) {
			dev_err(&client->dev, "request irq %d for trigger0 failed\n",
				client->irq);
			goto err_pm_disable;
			}

		ret = devm_iio_trigger_register(indio_dev->dev.parent,
						data->drdy_trigger0);
		if (ret) {
			dev_err(&client->dev, "iio trigger register failed\n");
			goto err_pm_disable;
		}

		/*
		 * Now whole pipe from physical interrupt (irq defined by
		 * devicetree to device) to trigger0 output is set up.
		 */

		/* Trigger consumer setup */
		ret = devm_iio_triggered_buffer_setup(indio_dev->dev.parent,
			indio_dev,
			iio_pollfunc_store_time,
			rpr0521_trigger_consumer_handler,
			&rpr0521_buffer_setup_ops);
		if (ret < 0) {
			dev_err(&client->dev, "iio triggered buffer setup failed\n");
			goto err_pm_disable;
		}
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_pm_disable;

	return 0;

err_pm_disable:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
err_poweroff:
	rpr0521_poweroff(data);

	return ret;
}

static void rpr0521_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	rpr0521_poweroff(iio_priv(indio_dev));
}

static int rpr0521_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	/* If measurements are enabled, enable them on resume */
	if (!data->als_need_dis)
		data->als_ps_need_en = data->als_dev_en;
	if (!data->pxs_need_dis)
		data->pxs_ps_need_en = data->pxs_dev_en;

	/* disable channels and sets {als,pxs}_dev_en to false */
	ret = rpr0521_poweroff(data);
	regcache_mark_dirty(data->regmap);
	mutex_unlock(&data->lock);

	return ret;
}

static int rpr0521_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct rpr0521_data *data = iio_priv(indio_dev);
	int ret;

	regcache_sync(data->regmap);
	if (data->als_ps_need_en) {
		ret = rpr0521_als_enable(data, RPR0521_MODE_ALS_ENABLE);
		if (ret < 0)
			return ret;
		data->als_ps_need_en = false;
	}

	if (data->pxs_ps_need_en) {
		ret = rpr0521_pxs_enable(data, RPR0521_MODE_PXS_ENABLE);
		if (ret < 0)
			return ret;
		data->pxs_ps_need_en = false;
	}
	msleep(100);	//wait for first measurement result

	return 0;
}

static const struct dev_pm_ops rpr0521_pm_ops = {
	RUNTIME_PM_OPS(rpr0521_runtime_suspend, rpr0521_runtime_resume, NULL)
};

static const struct acpi_device_id rpr0521_acpi_match[] = {
	{"RPR0521", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, rpr0521_acpi_match);

static const struct i2c_device_id rpr0521_id[] = {
	{ "rpr0521" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, rpr0521_id);

static struct i2c_driver rpr0521_driver = {
	.driver = {
		.name	= RPR0521_DRV_NAME,
		.pm	= pm_ptr(&rpr0521_pm_ops),
		.acpi_match_table = rpr0521_acpi_match,
	},
	.probe		= rpr0521_probe,
	.remove		= rpr0521_remove,
	.id_table	= rpr0521_id,
};

module_i2c_driver(rpr0521_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("RPR0521 ROHM Ambient Light and Proximity Sensor driver");
MODULE_LICENSE("GPL v2");
