// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC.
 *
 * Driver for Semtech's SX9310/SX9311 capacitive proximity/button solution.
 * Based on SX9500 driver and Semtech driver using the input framework
 * <https://my.syncplicity.com/share/teouwsim8niiaud/
 *          linux-driver-SX9310_NoSmartHSensing>.
 * Reworked in April 2019 by Evan Green <evgreen@chromium.org>
 * and in January 2020 by Daniel Campello <campello@chromium.org>.
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

/* Register definitions. */
#define SX9310_REG_IRQ_SRC				0x00
#define SX9310_REG_STAT0				0x01
#define SX9310_REG_STAT1				0x02
#define SX9310_REG_STAT1_COMPSTAT_MASK			GENMASK(3, 0)
#define SX9310_REG_IRQ_MSK				0x03
#define   SX9310_CONVDONE_IRQ				BIT(3)
#define   SX9310_FAR_IRQ				BIT(5)
#define   SX9310_CLOSE_IRQ				BIT(6)
#define SX9310_REG_IRQ_FUNC				0x04

#define SX9310_REG_PROX_CTRL0				0x10
#define   SX9310_REG_PROX_CTRL0_SENSOREN_MASK		GENMASK(3, 0)
#define   SX9310_REG_PROX_CTRL0_SCANPERIOD_MASK		GENMASK(7, 4)
#define   SX9310_REG_PROX_CTRL0_SCANPERIOD_15MS		0x01
#define SX9310_REG_PROX_CTRL1				0x11
#define SX9310_REG_PROX_CTRL2				0x12
#define   SX9310_REG_PROX_CTRL2_COMBMODE_MASK		GENMASK(7, 6)
#define   SX9310_REG_PROX_CTRL2_COMBMODE_CS0_CS1_CS2_CS3 (0x03 << 6)
#define   SX9310_REG_PROX_CTRL2_COMBMODE_CS1_CS2	(0x02 << 6)
#define   SX9310_REG_PROX_CTRL2_COMBMODE_CS0_CS1	(0x01 << 6)
#define   SX9310_REG_PROX_CTRL2_COMBMODE_CS3		(0x00 << 6)
#define   SX9310_REG_PROX_CTRL2_SHIELDEN_MASK		GENMASK(3, 2)
#define   SX9310_REG_PROX_CTRL2_SHIELDEN_DYNAMIC	(0x01 << 2)
#define   SX9310_REG_PROX_CTRL2_SHIELDEN_GROUND		(0x02 << 2)
#define SX9310_REG_PROX_CTRL3				0x13
#define   SX9310_REG_PROX_CTRL3_GAIN0_MASK		GENMASK(3, 2)
#define   SX9310_REG_PROX_CTRL3_GAIN0_X8		(0x03 << 2)
#define   SX9310_REG_PROX_CTRL3_GAIN12_MASK		GENMASK(1, 0)
#define   SX9310_REG_PROX_CTRL3_GAIN12_X4		0x02
#define SX9310_REG_PROX_CTRL4				0x14
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_MASK		GENMASK(2, 0)
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_FINEST	0x07
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_VERY_FINE	0x06
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_FINE		0x05
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_MEDIUM	0x04
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_MEDIUM_COARSE 0x03
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_COARSE	0x02
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_VERY_COARSE	0x01
#define   SX9310_REG_PROX_CTRL4_RESOLUTION_COARSEST	0x00
#define SX9310_REG_PROX_CTRL5				0x15
#define   SX9310_REG_PROX_CTRL5_RANGE_SMALL		(0x03 << 6)
#define   SX9310_REG_PROX_CTRL5_STARTUPSENS_MASK	GENMASK(3, 2)
#define   SX9310_REG_PROX_CTRL5_STARTUPSENS_CS1		(0x01 << 2)
#define   SX9310_REG_PROX_CTRL5_RAWFILT_MASK		GENMASK(1, 0)
#define   SX9310_REG_PROX_CTRL5_RAWFILT_SHIFT		0
#define   SX9310_REG_PROX_CTRL5_RAWFILT_1P25		0x02
#define SX9310_REG_PROX_CTRL6				0x16
#define   SX9310_REG_PROX_CTRL6_AVGTHRESH_DEFAULT	0x20
#define SX9310_REG_PROX_CTRL7				0x17
#define   SX9310_REG_PROX_CTRL7_AVGNEGFILT_2		(0x01 << 3)
#define   SX9310_REG_PROX_CTRL7_AVGPOSFILT_MASK		GENMASK(2, 0)
#define   SX9310_REG_PROX_CTRL7_AVGPOSFILT_SHIFT	0
#define   SX9310_REG_PROX_CTRL7_AVGPOSFILT_512		0x05
#define SX9310_REG_PROX_CTRL8				0x18
#define   SX9310_REG_PROX_CTRL8_9_PTHRESH_MASK		GENMASK(7, 3)
#define SX9310_REG_PROX_CTRL9				0x19
#define   SX9310_REG_PROX_CTRL8_9_PTHRESH_28		(0x08 << 3)
#define   SX9310_REG_PROX_CTRL8_9_PTHRESH_96		(0x11 << 3)
#define   SX9310_REG_PROX_CTRL8_9_BODYTHRESH_900	0x03
#define   SX9310_REG_PROX_CTRL8_9_BODYTHRESH_1500	0x05
#define SX9310_REG_PROX_CTRL10				0x1a
#define   SX9310_REG_PROX_CTRL10_HYST_MASK		GENMASK(5, 4)
#define   SX9310_REG_PROX_CTRL10_HYST_6PCT		(0x01 << 4)
#define   SX9310_REG_PROX_CTRL10_CLOSE_DEBOUNCE_MASK	GENMASK(3, 2)
#define   SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_MASK	GENMASK(1, 0)
#define   SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_2		0x01
#define SX9310_REG_PROX_CTRL11				0x1b
#define SX9310_REG_PROX_CTRL12				0x1c
#define SX9310_REG_PROX_CTRL13				0x1d
#define SX9310_REG_PROX_CTRL14				0x1e
#define SX9310_REG_PROX_CTRL15				0x1f
#define SX9310_REG_PROX_CTRL16				0x20
#define SX9310_REG_PROX_CTRL17				0x21
#define SX9310_REG_PROX_CTRL18				0x22
#define SX9310_REG_PROX_CTRL19				0x23
#define SX9310_REG_SAR_CTRL0				0x2a
#define   SX9310_REG_SAR_CTRL0_SARDEB_4_SAMPLES		(0x02 << 5)
#define   SX9310_REG_SAR_CTRL0_SARHYST_8		(0x02 << 3)
#define SX9310_REG_SAR_CTRL1				0x2b
/* Each increment of the slope register is 0.0078125. */
#define   SX9310_REG_SAR_CTRL1_SLOPE(_hnslope)		(_hnslope / 78125)
#define SX9310_REG_SAR_CTRL2				0x2c
#define   SX9310_REG_SAR_CTRL2_SAROFFSET_DEFAULT	0x3c

#define SX9310_REG_SENSOR_SEL				0x30
#define SX9310_REG_USE_MSB				0x31
#define SX9310_REG_USE_LSB				0x32
#define SX9310_REG_AVG_MSB				0x33
#define SX9310_REG_AVG_LSB				0x34
#define SX9310_REG_DIFF_MSB				0x35
#define SX9310_REG_DIFF_LSB				0x36
#define SX9310_REG_OFFSET_MSB				0x37
#define SX9310_REG_OFFSET_LSB				0x38
#define SX9310_REG_SAR_MSB				0x39
#define SX9310_REG_SAR_LSB				0x3a
#define SX9310_REG_I2C_ADDR				0x40
#define SX9310_REG_PAUSE				0x41
#define SX9310_REG_WHOAMI				0x42
#define   SX9310_WHOAMI_VALUE				0x01
#define   SX9311_WHOAMI_VALUE				0x02
#define SX9310_REG_RESET				0x7f
#define   SX9310_SOFT_RESET				0xde


/* 4 hardware channels, as defined in STAT0: COMB, CS2, CS1 and CS0. */
#define SX9310_NUM_CHANNELS				4
static_assert(SX9310_NUM_CHANNELS < BITS_PER_LONG);

struct sx9310_data {
	/* Serialize access to registers and channel configuration */
	struct mutex mutex;
	struct i2c_client *client;
	struct iio_trigger *trig;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[2];
	/*
	 * Last reading of the proximity status for each channel.
	 * We only send an event to user space when this changes.
	 */
	unsigned long chan_prox_stat;
	bool trigger_enabled;
	/* Ensure correct alignment of timestamp when present. */
	struct {
		__be16 channels[SX9310_NUM_CHANNELS];
		s64 ts __aligned(8);
	} buffer;
	/* Remember enabled channels and sample rate during suspend. */
	unsigned int suspend_ctrl0;
	struct completion completion;
	unsigned long chan_read;
	unsigned long chan_event;
	unsigned int whoami;
};

static const struct iio_event_spec sx9310_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_shared_by_all = BIT(IIO_EV_INFO_PERIOD),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_shared_by_all = BIT(IIO_EV_INFO_PERIOD),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_HYSTERESIS) |
				 BIT(IIO_EV_INFO_VALUE),
	},
};

#define SX9310_NAMED_CHANNEL(idx, name)					 \
	{								 \
		.type = IIO_PROXIMITY,					 \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		 \
				      BIT(IIO_CHAN_INFO_HARDWAREGAIN),   \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
		.info_mask_separate_available =				 \
			BIT(IIO_CHAN_INFO_HARDWAREGAIN),		 \
		.indexed = 1,						 \
		.channel = idx,						 \
		.extend_name = name,					 \
		.address = SX9310_REG_DIFF_MSB,				 \
		.event_spec = sx9310_events,				 \
		.num_event_specs = ARRAY_SIZE(sx9310_events),		 \
		.scan_index = idx,					 \
		.scan_type = {						 \
			.sign = 's',					 \
			.realbits = 12,					 \
			.storagebits = 16,				 \
			.endianness = IIO_BE,				 \
		},							 \
	}
#define SX9310_CHANNEL(idx) SX9310_NAMED_CHANNEL(idx, NULL)

static const struct iio_chan_spec sx9310_channels[] = {
	SX9310_CHANNEL(0),			/* CS0 */
	SX9310_CHANNEL(1),			/* CS1 */
	SX9310_CHANNEL(2),			/* CS2 */
	SX9310_NAMED_CHANNEL(3, "comb"),	/* COMB */

	IIO_CHAN_SOFT_TIMESTAMP(4),
};

/*
 * Each entry contains the integer part (val) and the fractional part, in micro
 * seconds. It conforms to the IIO output IIO_VAL_INT_PLUS_MICRO.
 */
static const struct {
	int val;
	int val2;
} sx9310_samp_freq_table[] = {
	{ 500, 0 }, /* 0000: Min (no idle time) */
	{ 66, 666666 }, /* 0001: 15 ms */
	{ 33, 333333 }, /* 0010: 30 ms (Typ.) */
	{ 22, 222222 }, /* 0011: 45 ms */
	{ 16, 666666 }, /* 0100: 60 ms */
	{ 11, 111111 }, /* 0101: 90 ms */
	{ 8, 333333 }, /* 0110: 120 ms */
	{ 5, 0 }, /* 0111: 200 ms */
	{ 2, 500000 }, /* 1000: 400 ms */
	{ 1, 666666 }, /* 1001: 600 ms */
	{ 1, 250000 }, /* 1010: 800 ms */
	{ 1, 0 }, /* 1011: 1 s */
	{ 0, 500000 }, /* 1100: 2 s */
	{ 0, 333333 }, /* 1101: 3 s */
	{ 0, 250000 }, /* 1110: 4 s */
	{ 0, 200000 }, /* 1111: 5 s */
};
static const unsigned int sx9310_scan_period_table[] = {
	2,   15,  30,  45,   60,   90,	 120,  200,
	400, 600, 800, 1000, 2000, 3000, 4000, 5000,
};

static ssize_t sx9310_show_samp_freq_avail(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	size_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sx9310_samp_freq_table); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%d ",
				 sx9310_samp_freq_table[i].val,
				 sx9310_samp_freq_table[i].val2);
	buf[len - 1] = '\n';
	return len;
}
static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(sx9310_show_samp_freq_avail);

static const struct regmap_range sx9310_writable_reg_ranges[] = {
	regmap_reg_range(SX9310_REG_IRQ_MSK, SX9310_REG_IRQ_FUNC),
	regmap_reg_range(SX9310_REG_PROX_CTRL0, SX9310_REG_PROX_CTRL19),
	regmap_reg_range(SX9310_REG_SAR_CTRL0, SX9310_REG_SAR_CTRL2),
	regmap_reg_range(SX9310_REG_SENSOR_SEL, SX9310_REG_SENSOR_SEL),
	regmap_reg_range(SX9310_REG_OFFSET_MSB, SX9310_REG_OFFSET_LSB),
	regmap_reg_range(SX9310_REG_PAUSE, SX9310_REG_PAUSE),
	regmap_reg_range(SX9310_REG_RESET, SX9310_REG_RESET),
};

static const struct regmap_access_table sx9310_writeable_regs = {
	.yes_ranges = sx9310_writable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9310_writable_reg_ranges),
};

static const struct regmap_range sx9310_readable_reg_ranges[] = {
	regmap_reg_range(SX9310_REG_IRQ_SRC, SX9310_REG_IRQ_FUNC),
	regmap_reg_range(SX9310_REG_PROX_CTRL0, SX9310_REG_PROX_CTRL19),
	regmap_reg_range(SX9310_REG_SAR_CTRL0, SX9310_REG_SAR_CTRL2),
	regmap_reg_range(SX9310_REG_SENSOR_SEL, SX9310_REG_SAR_LSB),
	regmap_reg_range(SX9310_REG_I2C_ADDR, SX9310_REG_WHOAMI),
	regmap_reg_range(SX9310_REG_RESET, SX9310_REG_RESET),
};

static const struct regmap_access_table sx9310_readable_regs = {
	.yes_ranges = sx9310_readable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9310_readable_reg_ranges),
};

static const struct regmap_range sx9310_volatile_reg_ranges[] = {
	regmap_reg_range(SX9310_REG_IRQ_SRC, SX9310_REG_STAT1),
	regmap_reg_range(SX9310_REG_USE_MSB, SX9310_REG_DIFF_LSB),
	regmap_reg_range(SX9310_REG_SAR_MSB, SX9310_REG_SAR_LSB),
	regmap_reg_range(SX9310_REG_RESET, SX9310_REG_RESET),
};

static const struct regmap_access_table sx9310_volatile_regs = {
	.yes_ranges = sx9310_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9310_volatile_reg_ranges),
};

static const struct regmap_config sx9310_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SX9310_REG_RESET,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &sx9310_writeable_regs,
	.rd_table = &sx9310_readable_regs,
	.volatile_table = &sx9310_volatile_regs,
};

static int sx9310_update_chan_en(struct sx9310_data *data,
				 unsigned long chan_read,
				 unsigned long chan_event)
{
	int ret;
	unsigned long channels = chan_read | chan_event;

	if ((data->chan_read | data->chan_event) != channels) {
		ret = regmap_update_bits(data->regmap, SX9310_REG_PROX_CTRL0,
					 SX9310_REG_PROX_CTRL0_SENSOREN_MASK,
					 channels);
		if (ret)
			return ret;
	}
	data->chan_read = chan_read;
	data->chan_event = chan_event;
	return 0;
}

static int sx9310_get_read_channel(struct sx9310_data *data, int channel)
{
	return sx9310_update_chan_en(data, data->chan_read | BIT(channel),
				     data->chan_event);
}

static int sx9310_put_read_channel(struct sx9310_data *data, int channel)
{
	return sx9310_update_chan_en(data, data->chan_read & ~BIT(channel),
				     data->chan_event);
}

static int sx9310_get_event_channel(struct sx9310_data *data, int channel)
{
	return sx9310_update_chan_en(data, data->chan_read,
				     data->chan_event | BIT(channel));
}

static int sx9310_put_event_channel(struct sx9310_data *data, int channel)
{
	return sx9310_update_chan_en(data, data->chan_read,
				     data->chan_event & ~BIT(channel));
}

static int sx9310_enable_irq(struct sx9310_data *data, unsigned int irq)
{
	if (!data->client->irq)
		return 0;
	return regmap_update_bits(data->regmap, SX9310_REG_IRQ_MSK, irq, irq);
}

static int sx9310_disable_irq(struct sx9310_data *data, unsigned int irq)
{
	if (!data->client->irq)
		return 0;
	return regmap_update_bits(data->regmap, SX9310_REG_IRQ_MSK, irq, 0);
}

static int sx9310_read_prox_data(struct sx9310_data *data,
				 const struct iio_chan_spec *chan, __be16 *val)
{
	int ret;

	ret = regmap_write(data->regmap, SX9310_REG_SENSOR_SEL, chan->channel);
	if (ret)
		return ret;

	return regmap_bulk_read(data->regmap, chan->address, val, sizeof(*val));
}

/*
 * If we have no interrupt support, we have to wait for a scan period
 * after enabling a channel to get a result.
 */
static int sx9310_wait_for_sample(struct sx9310_data *data)
{
	int ret;
	unsigned int val;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL0, &val);
	if (ret)
		return ret;

	val = FIELD_GET(SX9310_REG_PROX_CTRL0_SCANPERIOD_MASK, val);

	msleep(sx9310_scan_period_table[val]);

	return 0;
}

static int sx9310_read_proximity(struct sx9310_data *data,
				 const struct iio_chan_spec *chan, int *val)
{
	int ret;
	__be16 rawval;

	mutex_lock(&data->mutex);

	ret = sx9310_get_read_channel(data, chan->channel);
	if (ret)
		goto out;

	ret = sx9310_enable_irq(data, SX9310_CONVDONE_IRQ);
	if (ret)
		goto out_put_channel;

	mutex_unlock(&data->mutex);

	if (data->client->irq) {
		ret = wait_for_completion_interruptible(&data->completion);
		reinit_completion(&data->completion);
	} else {
		ret = sx9310_wait_for_sample(data);
	}

	mutex_lock(&data->mutex);

	if (ret)
		goto out_disable_irq;

	ret = sx9310_read_prox_data(data, chan, &rawval);
	if (ret)
		goto out_disable_irq;

	*val = sign_extend32(be16_to_cpu(rawval),
			     chan->address == SX9310_REG_DIFF_MSB ? 11 : 15);

	ret = sx9310_disable_irq(data, SX9310_CONVDONE_IRQ);
	if (ret)
		goto out_put_channel;

	ret = sx9310_put_read_channel(data, chan->channel);
	if (ret)
		goto out;

	mutex_unlock(&data->mutex);

	return IIO_VAL_INT;

out_disable_irq:
	sx9310_disable_irq(data, SX9310_CONVDONE_IRQ);
out_put_channel:
	sx9310_put_read_channel(data, chan->channel);
out:
	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_read_gain(struct sx9310_data *data,
			    const struct iio_chan_spec *chan, int *val)
{
	unsigned int regval, gain;
	int ret;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL3, &regval);
	if (ret)
		return ret;

	switch (chan->channel) {
	case 0:
	case 3:
		gain = FIELD_GET(SX9310_REG_PROX_CTRL3_GAIN0_MASK, regval);
		break;
	case 1:
	case 2:
		gain = FIELD_GET(SX9310_REG_PROX_CTRL3_GAIN12_MASK, regval);
		break;
	default:
		return -EINVAL;
	}

	*val = 1 << gain;

	return IIO_VAL_INT;
}

static int sx9310_read_samp_freq(struct sx9310_data *data, int *val, int *val2)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL0, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9310_REG_PROX_CTRL0_SCANPERIOD_MASK, regval);
	*val = sx9310_samp_freq_table[regval].val;
	*val2 = sx9310_samp_freq_table[regval].val2;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int sx9310_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan, int *val,
			   int *val2, long mask)
{
	struct sx9310_data *data = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = sx9310_read_proximity(data, chan, val);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = sx9310_read_gain(data, chan, val);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return sx9310_read_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static const int sx9310_gain_vals[] = { 1, 2, 4, 8 };

static int sx9310_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(sx9310_gain_vals);
		*vals = sx9310_gain_vals;
		return IIO_AVAIL_LIST;
	}

	return -EINVAL;
}

static const unsigned int sx9310_pthresh_codes[] = {
	2, 4, 6, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 88, 96, 112,
	128, 144, 160, 192, 224, 256, 320, 384, 512, 640, 768, 1024, 1536
};

static int sx9310_get_thresh_reg(unsigned int channel)
{
	switch (channel) {
	case 0:
	case 3:
		return SX9310_REG_PROX_CTRL8;
	case 1:
	case 2:
		return SX9310_REG_PROX_CTRL9;
	}

	return -EINVAL;
}

static int sx9310_read_thresh(struct sx9310_data *data,
			      const struct iio_chan_spec *chan, int *val)
{
	unsigned int reg;
	unsigned int regval;
	int ret;

	reg = ret = sx9310_get_thresh_reg(chan->channel);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, reg, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9310_REG_PROX_CTRL8_9_PTHRESH_MASK, regval);
	if (regval >= ARRAY_SIZE(sx9310_pthresh_codes))
		return -EINVAL;

	*val = sx9310_pthresh_codes[regval];
	return IIO_VAL_INT;
}

static int sx9310_read_hysteresis(struct sx9310_data *data,
				  const struct iio_chan_spec *chan, int *val)
{
	unsigned int regval, pthresh;
	int ret;

	ret = sx9310_read_thresh(data, chan, &pthresh);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL10, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9310_REG_PROX_CTRL10_HYST_MASK, regval);
	if (!regval)
		regval = 5;

	/* regval is at most 5 */
	*val = pthresh >> (5 - regval);

	return IIO_VAL_INT;
}

static int sx9310_read_far_debounce(struct sx9310_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL10, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_MASK, regval);
	if (regval)
		*val = 1 << regval;
	else
		*val = 0;

	return IIO_VAL_INT;
}

static int sx9310_read_close_debounce(struct sx9310_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL10, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9310_REG_PROX_CTRL10_CLOSE_DEBOUNCE_MASK, regval);
	if (regval)
		*val = 1 << regval;
	else
		*val = 0;

	return IIO_VAL_INT;
}

static int sx9310_read_event_val(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int *val, int *val2)
{
	struct sx9310_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return sx9310_read_thresh(data, chan, val);
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return sx9310_read_far_debounce(data, val);
		case IIO_EV_DIR_FALLING:
			return sx9310_read_close_debounce(data, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return sx9310_read_hysteresis(data, chan, val);
	default:
		return -EINVAL;
	}
}

static int sx9310_write_thresh(struct sx9310_data *data,
			       const struct iio_chan_spec *chan, int val)
{
	unsigned int reg;
	unsigned int regval;
	int ret, i;

	reg = ret = sx9310_get_thresh_reg(chan->channel);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(sx9310_pthresh_codes); i++) {
		if (sx9310_pthresh_codes[i] == val) {
			regval = i;
			break;
		}
	}

	if (i == ARRAY_SIZE(sx9310_pthresh_codes))
		return -EINVAL;

	regval = FIELD_PREP(SX9310_REG_PROX_CTRL8_9_PTHRESH_MASK, regval);
	mutex_lock(&data->mutex);
	ret = regmap_update_bits(data->regmap, reg,
				 SX9310_REG_PROX_CTRL8_9_PTHRESH_MASK, regval);
	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_write_hysteresis(struct sx9310_data *data,
				   const struct iio_chan_spec *chan, int _val)
{
	unsigned int hyst, val = _val;
	int ret, pthresh;

	ret = sx9310_read_thresh(data, chan, &pthresh);
	if (ret < 0)
		return ret;

	if (val == 0)
		hyst = 0;
	else if (val == pthresh >> 2)
		hyst = 3;
	else if (val == pthresh >> 3)
		hyst = 2;
	else if (val == pthresh >> 4)
		hyst = 1;
	else
		return -EINVAL;

	hyst = FIELD_PREP(SX9310_REG_PROX_CTRL10_HYST_MASK, hyst);
	mutex_lock(&data->mutex);
	ret = regmap_update_bits(data->regmap, SX9310_REG_PROX_CTRL10,
				 SX9310_REG_PROX_CTRL10_HYST_MASK, hyst);
	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_write_far_debounce(struct sx9310_data *data, int val)
{
	int ret;
	unsigned int regval;

	if (val > 0)
		val = ilog2(val);
	if (!FIELD_FIT(SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_MASK, val))
		return -EINVAL;

	regval = FIELD_PREP(SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_MASK, val);

	mutex_lock(&data->mutex);
	ret = regmap_update_bits(data->regmap, SX9310_REG_PROX_CTRL10,
				 SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_MASK,
				 regval);
	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_write_close_debounce(struct sx9310_data *data, int val)
{
	int ret;
	unsigned int regval;

	if (val > 0)
		val = ilog2(val);
	if (!FIELD_FIT(SX9310_REG_PROX_CTRL10_CLOSE_DEBOUNCE_MASK, val))
		return -EINVAL;

	regval = FIELD_PREP(SX9310_REG_PROX_CTRL10_CLOSE_DEBOUNCE_MASK, val);

	mutex_lock(&data->mutex);
	ret = regmap_update_bits(data->regmap, SX9310_REG_PROX_CTRL10,
				 SX9310_REG_PROX_CTRL10_CLOSE_DEBOUNCE_MASK,
				 regval);
	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_write_event_val(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info, int val, int val2)
{
	struct sx9310_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return sx9310_write_thresh(data, chan, val);
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return sx9310_write_far_debounce(data, val);
		case IIO_EV_DIR_FALLING:
			return sx9310_write_close_debounce(data, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return sx9310_write_hysteresis(data, chan, val);
	default:
		return -EINVAL;
	}
}

static int sx9310_set_samp_freq(struct sx9310_data *data, int val, int val2)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(sx9310_samp_freq_table); i++)
		if (val == sx9310_samp_freq_table[i].val &&
		    val2 == sx9310_samp_freq_table[i].val2)
			break;

	if (i == ARRAY_SIZE(sx9310_samp_freq_table))
		return -EINVAL;

	mutex_lock(&data->mutex);

	ret = regmap_update_bits(
		data->regmap, SX9310_REG_PROX_CTRL0,
		SX9310_REG_PROX_CTRL0_SCANPERIOD_MASK,
		FIELD_PREP(SX9310_REG_PROX_CTRL0_SCANPERIOD_MASK, i));

	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_write_gain(struct sx9310_data *data,
			    const struct iio_chan_spec *chan, int val)
{
	unsigned int gain, mask;
	int ret;

	gain = ilog2(val);

	switch (chan->channel) {
	case 0:
	case 3:
		mask = SX9310_REG_PROX_CTRL3_GAIN0_MASK;
		gain = FIELD_PREP(SX9310_REG_PROX_CTRL3_GAIN0_MASK, gain);
		break;
	case 1:
	case 2:
		mask = SX9310_REG_PROX_CTRL3_GAIN12_MASK;
		gain = FIELD_PREP(SX9310_REG_PROX_CTRL3_GAIN12_MASK, gain);
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&data->mutex);
	ret = regmap_update_bits(data->regmap, SX9310_REG_PROX_CTRL3, mask,
				 gain);
	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9310_write_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan, int val, int val2,
			    long mask)
{
	struct sx9310_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return sx9310_set_samp_freq(data, val, val2);
	case IIO_CHAN_INFO_HARDWAREGAIN:
		return sx9310_write_gain(data, chan, val);
	}

	return -EINVAL;
}

static irqreturn_t sx9310_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx9310_data *data = iio_priv(indio_dev);

	if (data->trigger_enabled)
		iio_trigger_poll(data->trig);

	/*
	 * Even if no event is enabled, we need to wake the thread to clear the
	 * interrupt state by reading SX9310_REG_IRQ_SRC.
	 * It is not possible to do that here because regmap_read takes a mutex.
	 */
	return IRQ_WAKE_THREAD;
}

static void sx9310_push_events(struct iio_dev *indio_dev)
{
	int ret;
	unsigned int val, chan;
	struct sx9310_data *data = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	unsigned long prox_changed;

	/* Read proximity state on all channels */
	ret = regmap_read(data->regmap, SX9310_REG_STAT0, &val);
	if (ret) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		return;
	}

	/*
	 * Only iterate over channels with changes on proximity status that have
	 * events enabled.
	 */
	prox_changed = (data->chan_prox_stat ^ val) & data->chan_event;

	for_each_set_bit(chan, &prox_changed, SX9310_NUM_CHANNELS) {
		int dir;
		u64 ev;

		dir = (val & BIT(chan)) ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING;
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, chan,
					  IIO_EV_TYPE_THRESH, dir);

		iio_push_event(indio_dev, ev, timestamp);
	}
	data->chan_prox_stat = val;
}

static irqreturn_t sx9310_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx9310_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int val;

	mutex_lock(&data->mutex);

	ret = regmap_read(data->regmap, SX9310_REG_IRQ_SRC, &val);
	if (ret) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		goto out;
	}

	if (val & (SX9310_FAR_IRQ | SX9310_CLOSE_IRQ))
		sx9310_push_events(indio_dev);

	if (val & SX9310_CONVDONE_IRQ)
		complete(&data->completion);

out:
	mutex_unlock(&data->mutex);

	return IRQ_HANDLED;
}

static int sx9310_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct sx9310_data *data = iio_priv(indio_dev);

	return !!(data->chan_event & BIT(chan->channel));
}

static int sx9310_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, int state)
{
	struct sx9310_data *data = iio_priv(indio_dev);
	unsigned int eventirq = SX9310_FAR_IRQ | SX9310_CLOSE_IRQ;
	int ret;

	/* If the state hasn't changed, there's nothing to do. */
	if (!!(data->chan_event & BIT(chan->channel)) == state)
		return 0;

	mutex_lock(&data->mutex);
	if (state) {
		ret = sx9310_get_event_channel(data, chan->channel);
		if (ret)
			goto out_unlock;
		if (!(data->chan_event & ~BIT(chan->channel))) {
			ret = sx9310_enable_irq(data, eventirq);
			if (ret)
				sx9310_put_event_channel(data, chan->channel);
		}
	} else {
		ret = sx9310_put_event_channel(data, chan->channel);
		if (ret)
			goto out_unlock;
		if (!data->chan_event) {
			ret = sx9310_disable_irq(data, eventirq);
			if (ret)
				sx9310_get_event_channel(data, chan->channel);
		}
	}

out_unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static struct attribute *sx9310_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group sx9310_attribute_group = {
	.attrs = sx9310_attributes,
};

static const struct iio_info sx9310_info = {
	.attrs = &sx9310_attribute_group,
	.read_raw = sx9310_read_raw,
	.read_avail = sx9310_read_avail,
	.read_event_value = sx9310_read_event_val,
	.write_event_value = sx9310_write_event_val,
	.write_raw = sx9310_write_raw,
	.read_event_config = sx9310_read_event_config,
	.write_event_config = sx9310_write_event_config,
};

static int sx9310_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct sx9310_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->mutex);

	if (state)
		ret = sx9310_enable_irq(data, SX9310_CONVDONE_IRQ);
	else if (!data->chan_read)
		ret = sx9310_disable_irq(data, SX9310_CONVDONE_IRQ);
	if (ret)
		goto out;

	data->trigger_enabled = state;

out:
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_trigger_ops sx9310_trigger_ops = {
	.set_trigger_state = sx9310_set_trigger_state,
};

static irqreturn_t sx9310_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct sx9310_data *data = iio_priv(indio_dev);
	__be16 val;
	int bit, ret, i = 0;

	mutex_lock(&data->mutex);

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = sx9310_read_prox_data(data, &indio_dev->channels[bit],
					    &val);
		if (ret)
			goto out;

		data->buffer.channels[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer,
					   pf->timestamp);

out:
	mutex_unlock(&data->mutex);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int sx9310_buffer_preenable(struct iio_dev *indio_dev)
{
	struct sx9310_data *data = iio_priv(indio_dev);
	unsigned long channels = 0;
	int bit, ret;

	mutex_lock(&data->mutex);
	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength)
		__set_bit(indio_dev->channels[bit].channel, &channels);

	ret = sx9310_update_chan_en(data, channels, data->chan_event);
	mutex_unlock(&data->mutex);
	return ret;
}

static int sx9310_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct sx9310_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = sx9310_update_chan_en(data, 0, data->chan_event);
	mutex_unlock(&data->mutex);
	return ret;
}

static const struct iio_buffer_setup_ops sx9310_buffer_setup_ops = {
	.preenable = sx9310_buffer_preenable,
	.postdisable = sx9310_buffer_postdisable,
};

struct sx9310_reg_default {
	u8 reg;
	u8 def;
};

static const struct sx9310_reg_default sx9310_default_regs[] = {
	{ SX9310_REG_IRQ_MSK, 0x00 },
	{ SX9310_REG_IRQ_FUNC, 0x00 },
	/*
	 * The lower 4 bits should not be set as it enable sensors measurements.
	 * Turning the detection on before the configuration values are set to
	 * good values can cause the device to return erroneous readings.
	 */
	{ SX9310_REG_PROX_CTRL0, SX9310_REG_PROX_CTRL0_SCANPERIOD_15MS },
	{ SX9310_REG_PROX_CTRL1, 0x00 },
	{ SX9310_REG_PROX_CTRL2, SX9310_REG_PROX_CTRL2_COMBMODE_CS1_CS2 |
				 SX9310_REG_PROX_CTRL2_SHIELDEN_DYNAMIC },
	{ SX9310_REG_PROX_CTRL3, SX9310_REG_PROX_CTRL3_GAIN0_X8 |
				 SX9310_REG_PROX_CTRL3_GAIN12_X4 },
	{ SX9310_REG_PROX_CTRL4, SX9310_REG_PROX_CTRL4_RESOLUTION_FINEST },
	{ SX9310_REG_PROX_CTRL5, SX9310_REG_PROX_CTRL5_RANGE_SMALL |
				 SX9310_REG_PROX_CTRL5_STARTUPSENS_CS1 |
				 SX9310_REG_PROX_CTRL5_RAWFILT_1P25 },
	{ SX9310_REG_PROX_CTRL6, SX9310_REG_PROX_CTRL6_AVGTHRESH_DEFAULT },
	{ SX9310_REG_PROX_CTRL7, SX9310_REG_PROX_CTRL7_AVGNEGFILT_2 |
				 SX9310_REG_PROX_CTRL7_AVGPOSFILT_512 },
	{ SX9310_REG_PROX_CTRL8, SX9310_REG_PROX_CTRL8_9_PTHRESH_96 |
				 SX9310_REG_PROX_CTRL8_9_BODYTHRESH_1500 },
	{ SX9310_REG_PROX_CTRL9, SX9310_REG_PROX_CTRL8_9_PTHRESH_28 |
				 SX9310_REG_PROX_CTRL8_9_BODYTHRESH_900 },
	{ SX9310_REG_PROX_CTRL10, SX9310_REG_PROX_CTRL10_HYST_6PCT |
				  SX9310_REG_PROX_CTRL10_FAR_DEBOUNCE_2 },
	{ SX9310_REG_PROX_CTRL11, 0x00 },
	{ SX9310_REG_PROX_CTRL12, 0x00 },
	{ SX9310_REG_PROX_CTRL13, 0x00 },
	{ SX9310_REG_PROX_CTRL14, 0x00 },
	{ SX9310_REG_PROX_CTRL15, 0x00 },
	{ SX9310_REG_PROX_CTRL16, 0x00 },
	{ SX9310_REG_PROX_CTRL17, 0x00 },
	{ SX9310_REG_PROX_CTRL18, 0x00 },
	{ SX9310_REG_PROX_CTRL19, 0x00 },
	{ SX9310_REG_SAR_CTRL0, SX9310_REG_SAR_CTRL0_SARDEB_4_SAMPLES |
				SX9310_REG_SAR_CTRL0_SARHYST_8 },
	{ SX9310_REG_SAR_CTRL1, SX9310_REG_SAR_CTRL1_SLOPE(10781250) },
	{ SX9310_REG_SAR_CTRL2, SX9310_REG_SAR_CTRL2_SAROFFSET_DEFAULT },
};

/* Activate all channels and perform an initial compensation. */
static int sx9310_init_compensation(struct iio_dev *indio_dev)
{
	struct sx9310_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int val;
	unsigned int ctrl0;

	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL0, &ctrl0);
	if (ret)
		return ret;

	/* run the compensation phase on all channels */
	ret = regmap_write(data->regmap, SX9310_REG_PROX_CTRL0,
			   ctrl0 | SX9310_REG_PROX_CTRL0_SENSOREN_MASK);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(data->regmap, SX9310_REG_STAT1, val,
				       !(val & SX9310_REG_STAT1_COMPSTAT_MASK),
				       20000, 2000000);
	if (ret) {
		if (ret == -ETIMEDOUT)
			dev_err(&data->client->dev,
				"initial compensation timed out: 0x%02x\n",
				val);
		return ret;
	}

	regmap_write(data->regmap, SX9310_REG_PROX_CTRL0, ctrl0);
	return ret;
}

static const struct sx9310_reg_default *
sx9310_get_default_reg(struct device *dev, int idx,
		       struct sx9310_reg_default *reg_def)
{
	u32 combined[SX9310_NUM_CHANNELS];
	u32 start = 0, raw = 0, pos = 0;
	unsigned long comb_mask = 0;
	int ret, i, count;
	const char *res;

	memcpy(reg_def, &sx9310_default_regs[idx], sizeof(*reg_def));
	switch (reg_def->reg) {
	case SX9310_REG_PROX_CTRL2:
		if (device_property_read_bool(dev, "semtech,cs0-ground")) {
			reg_def->def &= ~SX9310_REG_PROX_CTRL2_SHIELDEN_MASK;
			reg_def->def |= SX9310_REG_PROX_CTRL2_SHIELDEN_GROUND;
		}

		count = device_property_count_u32(dev, "semtech,combined-sensors");
		if (count < 0 || count > ARRAY_SIZE(combined))
			break;
		ret = device_property_read_u32_array(dev, "semtech,combined-sensors",
				combined, count);
		if (ret)
			break;

		for (i = 0; i < count; i++)
			comb_mask |= BIT(combined[i]);

		reg_def->def &= ~SX9310_REG_PROX_CTRL2_COMBMODE_MASK;
		if (comb_mask == (BIT(3) | BIT(2) | BIT(1) | BIT(0)))
			reg_def->def |= SX9310_REG_PROX_CTRL2_COMBMODE_CS0_CS1_CS2_CS3;
		else if (comb_mask == (BIT(1) | BIT(2)))
			reg_def->def |= SX9310_REG_PROX_CTRL2_COMBMODE_CS1_CS2;
		else if (comb_mask == (BIT(0) | BIT(1)))
			reg_def->def |= SX9310_REG_PROX_CTRL2_COMBMODE_CS0_CS1;
		else if (comb_mask == BIT(3))
			reg_def->def |= SX9310_REG_PROX_CTRL2_COMBMODE_CS3;

		break;
	case SX9310_REG_PROX_CTRL4:
		ret = device_property_read_string(dev, "semtech,resolution", &res);
		if (ret)
			break;

		reg_def->def &= ~SX9310_REG_PROX_CTRL4_RESOLUTION_MASK;
		if (!strcmp(res, "coarsest"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_COARSEST;
		else if (!strcmp(res, "very-coarse"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_VERY_COARSE;
		else if (!strcmp(res, "coarse"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_COARSE;
		else if (!strcmp(res, "medium-coarse"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_MEDIUM_COARSE;
		else if (!strcmp(res, "medium"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_MEDIUM;
		else if (!strcmp(res, "fine"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_FINE;
		else if (!strcmp(res, "very-fine"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_VERY_FINE;
		else if (!strcmp(res, "finest"))
			reg_def->def |= SX9310_REG_PROX_CTRL4_RESOLUTION_FINEST;

		break;
	case SX9310_REG_PROX_CTRL5:
		ret = device_property_read_u32(dev, "semtech,startup-sensor", &start);
		if (ret) {
			start = FIELD_GET(SX9310_REG_PROX_CTRL5_STARTUPSENS_MASK,
					  reg_def->def);
		}

		reg_def->def &= ~SX9310_REG_PROX_CTRL5_STARTUPSENS_MASK;
		reg_def->def |= FIELD_PREP(SX9310_REG_PROX_CTRL5_STARTUPSENS_MASK,
					   start);

		ret = device_property_read_u32(dev, "semtech,proxraw-strength", &raw);
		if (ret) {
			raw = FIELD_GET(SX9310_REG_PROX_CTRL5_RAWFILT_MASK,
					reg_def->def);
		} else {
			raw = ilog2(raw);
		}

		reg_def->def &= ~SX9310_REG_PROX_CTRL5_RAWFILT_MASK;
		reg_def->def |= FIELD_PREP(SX9310_REG_PROX_CTRL5_RAWFILT_MASK,
					   raw);
		break;
	case SX9310_REG_PROX_CTRL7:
		ret = device_property_read_u32(dev, "semtech,avg-pos-strength", &pos);
		if (ret)
			break;

		/* Powers of 2, except for a gap between 16 and 64 */
		pos = clamp(ilog2(pos), 3, 11) - (pos >= 32 ? 4 : 3);
		reg_def->def &= ~SX9310_REG_PROX_CTRL7_AVGPOSFILT_MASK;
		reg_def->def |= FIELD_PREP(SX9310_REG_PROX_CTRL7_AVGPOSFILT_MASK,
					   pos);
		break;
	}

	return reg_def;
}

static int sx9310_init_device(struct iio_dev *indio_dev)
{
	struct sx9310_data *data = iio_priv(indio_dev);
	struct sx9310_reg_default tmp;
	const struct sx9310_reg_default *initval;
	int ret;
	unsigned int i, val;

	ret = regmap_write(data->regmap, SX9310_REG_RESET, SX9310_SOFT_RESET);
	if (ret)
		return ret;

	usleep_range(1000, 2000); /* power-up time is ~1ms. */

	/* Clear reset interrupt state by reading SX9310_REG_IRQ_SRC. */
	ret = regmap_read(data->regmap, SX9310_REG_IRQ_SRC, &val);
	if (ret)
		return ret;

	/* Program some sane defaults. */
	for (i = 0; i < ARRAY_SIZE(sx9310_default_regs); i++) {
		initval = sx9310_get_default_reg(&indio_dev->dev, i, &tmp);
		ret = regmap_write(data->regmap, initval->reg, initval->def);
		if (ret)
			return ret;
	}

	return sx9310_init_compensation(indio_dev);
}

static int sx9310_set_indio_dev_name(struct device *dev,
				     struct iio_dev *indio_dev,
				     unsigned int whoami)
{
	unsigned int long ddata;

	ddata = (uintptr_t)device_get_match_data(dev);
	if (ddata != whoami) {
		dev_err(dev, "WHOAMI does not match device data: %u\n", whoami);
		return -ENODEV;
	}

	switch (whoami) {
	case SX9310_WHOAMI_VALUE:
		indio_dev->name = "sx9310";
		break;
	case SX9311_WHOAMI_VALUE:
		indio_dev->name = "sx9311";
		break;
	default:
		dev_err(dev, "unexpected WHOAMI response: %u\n", whoami);
		return -ENODEV;
	}

	return 0;
}

static void sx9310_regulator_disable(void *_data)
{
	struct sx9310_data *data = _data;

	regulator_bulk_disable(ARRAY_SIZE(data->supplies), data->supplies);
}

static int sx9310_probe(struct i2c_client *client)
{
	int ret;
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct sx9310_data *data;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	data->supplies[0].supply = "vdd";
	data->supplies[1].supply = "svdd";
	mutex_init(&data->mutex);
	init_completion(&data->completion);

	data->regmap = devm_regmap_init_i2c(client, &sx9310_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->supplies),
				      data->supplies);
	if (ret)
		return ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(data->supplies), data->supplies);
	if (ret)
		return ret;
	/* Must wait for Tpor time after initial power up */
	usleep_range(1000, 1100);

	ret = devm_add_action_or_reset(dev, sx9310_regulator_disable, data);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, SX9310_REG_WHOAMI, &data->whoami);
	if (ret) {
		dev_err(dev, "error in reading WHOAMI register: %d", ret);
		return ret;
	}

	ret = sx9310_set_indio_dev_name(dev, indio_dev, data->whoami);
	if (ret)
		return ret;

	ACPI_COMPANION_SET(&indio_dev->dev, ACPI_COMPANION(dev));
	indio_dev->channels = sx9310_channels;
	indio_dev->num_channels = ARRAY_SIZE(sx9310_channels);
	indio_dev->info = &sx9310_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	i2c_set_clientdata(client, indio_dev);

	ret = sx9310_init_device(indio_dev);
	if (ret)
		return ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq,
						sx9310_irq_handler,
						sx9310_irq_thread_handler,
						IRQF_ONESHOT,
						"sx9310_event", indio_dev);
		if (ret)
			return ret;

		data->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
						    indio_dev->name,
						    iio_device_id(indio_dev));
		if (!data->trig)
			return -ENOMEM;

		data->trig->ops = &sx9310_trigger_ops;
		iio_trigger_set_drvdata(data->trig, indio_dev);

		ret = devm_iio_trigger_register(dev, data->trig);
		if (ret)
			return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      sx9310_trigger_handler,
					      &sx9310_buffer_setup_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static int __maybe_unused sx9310_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct sx9310_data *data = iio_priv(indio_dev);
	u8 ctrl0;
	int ret;

	disable_irq_nosync(data->client->irq);

	mutex_lock(&data->mutex);
	ret = regmap_read(data->regmap, SX9310_REG_PROX_CTRL0,
			  &data->suspend_ctrl0);
	if (ret)
		goto out;

	ctrl0 = data->suspend_ctrl0 & ~SX9310_REG_PROX_CTRL0_SENSOREN_MASK;
	ret = regmap_write(data->regmap, SX9310_REG_PROX_CTRL0, ctrl0);
	if (ret)
		goto out;

	ret = regmap_write(data->regmap, SX9310_REG_PAUSE, 0);

out:
	mutex_unlock(&data->mutex);
	return ret;
}

static int __maybe_unused sx9310_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct sx9310_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = regmap_write(data->regmap, SX9310_REG_PAUSE, 1);
	if (ret)
		goto out;

	ret = regmap_write(data->regmap, SX9310_REG_PROX_CTRL0,
			   data->suspend_ctrl0);

out:
	mutex_unlock(&data->mutex);
	if (ret)
		return ret;

	enable_irq(data->client->irq);
	return 0;
}

static const struct dev_pm_ops sx9310_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sx9310_suspend, sx9310_resume)
};

static const struct acpi_device_id sx9310_acpi_match[] = {
	{ "STH9310", SX9310_WHOAMI_VALUE },
	{ "STH9311", SX9311_WHOAMI_VALUE },
	{}
};
MODULE_DEVICE_TABLE(acpi, sx9310_acpi_match);

static const struct of_device_id sx9310_of_match[] = {
	{ .compatible = "semtech,sx9310", (void *)SX9310_WHOAMI_VALUE },
	{ .compatible = "semtech,sx9311", (void *)SX9311_WHOAMI_VALUE },
	{}
};
MODULE_DEVICE_TABLE(of, sx9310_of_match);

static const struct i2c_device_id sx9310_id[] = {
	{ "sx9310", SX9310_WHOAMI_VALUE },
	{ "sx9311", SX9311_WHOAMI_VALUE },
	{}
};
MODULE_DEVICE_TABLE(i2c, sx9310_id);

static struct i2c_driver sx9310_driver = {
	.driver = {
		.name	= "sx9310",
		.acpi_match_table = sx9310_acpi_match,
		.of_match_table = sx9310_of_match,
		.pm = &sx9310_pm_ops,

		/*
		 * Lots of i2c transfers in probe + over 200 ms waiting in
		 * sx9310_init_compensation() mean a slow probe; prefer async
		 * so we don't delay boot if we're builtin to the kernel.
		 */
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe_new	= sx9310_probe,
	.id_table	= sx9310_id,
};
module_i2c_driver(sx9310_driver);

MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_AUTHOR("Daniel Campello <campello@chromium.org>");
MODULE_DESCRIPTION("Driver for Semtech SX9310/SX9311 proximity sensor");
MODULE_LICENSE("GPL v2");
