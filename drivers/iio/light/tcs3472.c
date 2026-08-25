// SPDX-License-Identifier: GPL-2.0-only
/*
 * tcs3472.c - Support for TAOS TCS3472 color light-to-digital converter
 *
 * Copyright (c) 2013 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * Color light sensor with 16-bit channels for red, green, blue, clear);
 * 7-bit I2C slave address 0x39 (TCS34721, TCS34723) or 0x29 (TCS34725,
 * TCS34727)
 *
 * Datasheet: http://ams.com/eng/content/download/319364/1117183/file/TCS3472_Datasheet_EN_v2.pdf
 */

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define TCS3472_DRV_NAME "tcs3472"

#define TCS3472_COMMAND BIT(7)
#define TCS3472_AUTO_INCR BIT(5)
#define TCS3472_SPECIAL_FUNC (BIT(5) | BIT(6))

#define TCS3472_INTR_CLEAR (TCS3472_COMMAND | TCS3472_SPECIAL_FUNC | 0x06)

#define TCS3472_ENABLE (TCS3472_COMMAND | 0x00)
#define TCS3472_ATIME (TCS3472_COMMAND | 0x01)
#define TCS3472_WTIME (TCS3472_COMMAND | 0x03)
#define TCS3472_AILT (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x04)
#define TCS3472_AIHT (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x06)
#define TCS3472_PERS (TCS3472_COMMAND | 0x0c)
#define TCS3472_CONFIG (TCS3472_COMMAND | 0x0d)
#define TCS3472_CONTROL (TCS3472_COMMAND | 0x0f)
#define TCS3472_ID (TCS3472_COMMAND | 0x12)
#define TCS3472_STATUS (TCS3472_COMMAND | 0x13)
#define TCS3472_CDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x14)
#define TCS3472_RDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x16)
#define TCS3472_GDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x18)
#define TCS3472_BDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x1a)

#define TCS3472_STATUS_AINT BIT(4)
#define TCS3472_STATUS_AVALID BIT(0)
#define TCS3472_ENABLE_AIEN BIT(4)
#define TCS3472_ENABLE_WEN BIT(3)
#define TCS3472_ENABLE_AEN BIT(1)
#define TCS3472_ENABLE_PON BIT(0)
#define TCS3472_ENABLE_RUN						\
	(TCS3472_ENABLE_AEN | TCS3472_ENABLE_PON | TCS3472_ENABLE_WEN)
#define TCS3472_CONTROL_AGAIN_MASK (BIT(0) | BIT(1))
#define TCS3472_CONFIG_WLONG BIT(1)

#define TCS3472_ATIME_TO_US(atime) (((256) - (atime)) * 2400)

struct tcs3472_data {
	struct i2c_client *client;
	struct mutex lock;
	int target_freq_hz;
	int target_freq_uhz;
	u16 low_thresh;
	u16 high_thresh;
	u8 enable;
	u8 enable_pre_suspend;
	u8 control;
	u8 atime;
	u8 apers;
	u8 wtime;
	bool wlong;
};

static const struct iio_event_spec tcs3472_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD),
	},
};

#define TCS3472_CHANNEL(_color, _si, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBSCALE) | \
		BIT(IIO_CHAN_INFO_INT_TIME), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.channel2 = IIO_MOD_LIGHT_##_color, \
	.address = _addr, \
	.scan_index = _si, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.event_spec = _si ? NULL : tcs3472_events, \
	.num_event_specs = _si ? 0 : ARRAY_SIZE(tcs3472_events), \
}

static const int tcs3472_agains[] = { 1, 4, 16, 60 };

static const struct iio_chan_spec tcs3472_channels[] = {
	TCS3472_CHANNEL(CLEAR, 0, TCS3472_CDATA),
	TCS3472_CHANNEL(RED, 1, TCS3472_RDATA),
	TCS3472_CHANNEL(GREEN, 2, TCS3472_GDATA),
	TCS3472_CHANNEL(BLUE, 3, TCS3472_BDATA),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

/*
 * The chip's cycle time is the sum of three components:
 *   - ATIME: the programmable RGBC integration time.
 *   - The fixed RGBC initialization time (2400 us).
 *   - WTIME: the wait time, used only if WEN is set. If WLONG is active,
 *     the wait step is multiplied by 12 (2400 us -> 28800 us).
 */
static unsigned int tcs3472_cycle_time_us(struct tcs3472_data *data)
{
	unsigned int atime_us = TCS3472_ATIME_TO_US(data->atime);
	unsigned int init_us = 2400;
	unsigned int wtime_us;

	if (!(data->enable & TCS3472_ENABLE_WEN))
		wtime_us = 0;
	else if (data->wlong)
		wtime_us = (256 - data->wtime) * 28800;
	else
		wtime_us = (256 - data->wtime) * 2400;

	return atime_us + init_us + wtime_us;
}

/*
 * Convert a cycle time in microseconds to a frequency in Hz and microhertz.
 *
 * Given cycle_us = T (the cycle period in microseconds), the corresponding
 * frequency is:
 *   f = 1e6 / T  [Hz]
 *
 * The result is split into the IIO_VAL_INT_PLUS_MICRO format:
 *   val  = floor(1e6 / T)                [Hz]
 *   val2 = (1e6 mod T) * 1e6 / T         [microhertz]
 */
static void tcs3472_cycle_to_freq(unsigned int cycle_us, int *val, int *val2)
{
	*val = USEC_PER_SEC / cycle_us;
	*val2 = div_u64((u64)(USEC_PER_SEC % cycle_us) * USEC_PER_SEC,
			cycle_us);
}

static int tcs3472_req_data(struct tcs3472_data *data)
{
	/*
	 * The worst-case cycle time is reached with ATIME=0x00, WTIME=0x00
	 * and WLONG=1. So:
	 *   614 ms (Max Integration Time)
	 * + 2.4 ms (RGBC Init)
	 * + 7.37 s (Max Wait Time)
	 * = ~ 8 s (Total Max cycle time).
	 * Use that as a polling upper bound; in normal operation the loop
	 * exits as soon as AVALID is set. So the total number of tries in 8
	 * seconds considering a polling period of 20 ms is 400.
	 * Considering a 20% margin due to oscillator tolerance, the total
	 * duration becomes approximately 9.8 seconds, which corresponds to
	 * about 480 steps. Therefore, setting it to 500 appears to be a
	 * reasonable and safe trade-off.
	 */
	int tries = 500;
	int ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, TCS3472_STATUS);
		if (ret < 0)
			return ret;
		if (ret & TCS3472_STATUS_AVALID)
			break;
		msleep(20);
	}

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready\n");
		return -EIO;
	}

	return 0;
}

static int tcs3472_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = tcs3472_req_data(data);
		if (ret < 0) {
			iio_device_release_direct(indio_dev);
			return ret;
		}
		ret = i2c_smbus_read_word_data(data->client, chan->address);
		iio_device_release_direct(indio_dev);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		*val = tcs3472_agains[data->control &
			TCS3472_CONTROL_AGAIN_MASK];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = TCS3472_ATIME_TO_US(data->atime);
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		unsigned int cycle_us;

		guard(mutex)(&data->lock);
		cycle_us = tcs3472_cycle_time_us(data);
		tcs3472_cycle_to_freq(cycle_us, val, val2);
		return IIO_VAL_INT_PLUS_MICRO;
	}
	default:
		return -EINVAL;
	}
}

/*
 * __tcs3472_set_sampling_freq() - implementation of sampling frequency
 * configuration. The caller must hold data->lock.
 */
static int __tcs3472_set_sampling_freq(struct tcs3472_data *data,
				       int val, int val2)
{
	unsigned int atime_us;
	unsigned int init_us = 2400;
	u64 cycle_us;
	s64 wait_us;
	int wtime;
	bool wlong = false;
	u8 config;
	int ret;

	if (val < 0 || val2 < 0 || (val == 0 && val2 == 0))
		return -EINVAL;

	atime_us = TCS3472_ATIME_TO_US(data->atime);

	/*
	 * cycle_us = 1 / freq, expressed in microseconds.
	 * Numerator: 1 [s] = PSEC_PER_SEC [ps]
	 * Denominator: freq [Hz] * MICROHZ_PER_HZ + val2 [uHz] = freq in [uHz]
	 * Result: ps / uHz = us
	 */
	cycle_us = div64_u64(PSEC_PER_SEC, (u64)val * MICROHZ_PER_HZ + val2);

	/*
	 * wait_us can be negative when the requested frequency is too high
	 * to be reached, or very large when the requested frequency is
	 * close to zero. Use s64 to cover the full range:
	 *
	 *   cycle_us = PSEC_PER_SEC / (val * MICROHZ_PER_HZ + val2)
	 *
	 * The divisor of the formula above reaches its maximum when
	 * val = val2 = INT_MAX:
	 *  INT_MAX * MICROHZ_PER_HZ + INT_MAX = ~2.15e18
	 * so cycle_us_min = floor(1e12 / 2.15e18) = 0.
	 *
	 * The divisor reaches its minimum (1) when val = 0 and val2 = 1,
	 * so cycle_us_max = 1e12 / 1 = 1e12.
	 *
	 * Therefore:
	 *   wait_us_min = 0 - 2400 - 612000 = -616800
	 *   wait_us_max = 1e12 - 2400 - 2400 = 999999995200
	 *
	 * Both fit comfortably in s64.
	 */
	wait_us = (s64)cycle_us - init_us - atime_us;
	if (wait_us < 2400) {
		if (data->enable & TCS3472_ENABLE_WEN) {
			u8 enable = data->enable & ~TCS3472_ENABLE_WEN;

			ret = i2c_smbus_write_byte_data(data->client,
							TCS3472_ENABLE, enable);
			if (ret)
				return ret;

			data->enable = enable;
		}

		data->target_freq_hz = val;
		data->target_freq_uhz = val2;
		return 0;
	}

	/*
	 * Wait state is needed: make sure WEN is active before programming
	 * WTIME (and possibly WLONG).
	 */
	if (!(data->enable & TCS3472_ENABLE_WEN)) {
		u8 enable = data->enable | TCS3472_ENABLE_WEN;

		ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
						enable);
		if (ret)
			return ret;

		data->enable = enable;
	}

	wtime = 256 - DIV_ROUND_CLOSEST_ULL(wait_us, 2400);
	if (wtime < 0) {
		/*
		 * If wait_us is too high (so the requested frequency is too
		 * low), the resulting wait exceeds what WTIME can represent
		 * (max 614 ms without WLONG). Enable WLONG, whose step is 12x
		 * longer (28.8 ms instead of 2.4 ms), and recompute.
		 */
		wlong = true;
		wtime = 256 - DIV_ROUND_CLOSEST_ULL(wait_us, 28800);
	}

	if (wlong != data->wlong) {
		ret = i2c_smbus_read_byte_data(data->client, TCS3472_CONFIG);
		if (ret < 0)
			return ret;

		config = ret;
		if (wlong)
			config |= TCS3472_CONFIG_WLONG;
		else
			config &= ~TCS3472_CONFIG_WLONG;

		ret = i2c_smbus_write_byte_data(data->client, TCS3472_CONFIG,
						config);
		if (ret)
			return ret;

		data->wlong = wlong;
	}

	/*
	 * If the requested wait is so long that even WLONG cannot
	 * cover it, wtime may still be negative. Saturate to 0,
	 * which is the largest possible wait (256 * 28.8 ms = 7.37 s).
	 */
	wtime = clamp(wtime, 0, 255);
	ret = i2c_smbus_write_byte_data(data->client, TCS3472_WTIME, wtime);
	if (ret)
		return ret;

	data->wtime = wtime;
	data->target_freq_hz = val;
	data->target_freq_uhz = val2;

	return 0;
}

static int tcs3472_set_sampling_freq(struct tcs3472_data *data,
				     int val, int val2)
{
	guard(mutex)(&data->lock);
	return __tcs3472_set_sampling_freq(data, val, val2);
}

static int tcs3472_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val2 != 0)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(tcs3472_agains); i++) {
			if (val == tcs3472_agains[i]) {
				data->control &= ~TCS3472_CONTROL_AGAIN_MASK;
				data->control |= i;
				return i2c_smbus_write_byte_data(
					data->client, TCS3472_CONTROL,
					data->control);
			}
		}
		return -EINVAL;
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		for (i = 0; i < 256; i++) {
			if (val2 != (256 - i) * 2400)
				continue;

			guard(mutex)(&data->lock);

			ret = i2c_smbus_write_byte_data(data->client,
							TCS3472_ATIME, i);
			if (ret)
				return ret;

			data->atime = i;

			/*
			 * ATIME just changed, so the cycle time changed too.
			 * Re-run the sampling frequency logic to recompute
			 * WTIME and preserve the user's last requested
			 * frequency. Lock is already held.
			 */
			return __tcs3472_set_sampling_freq(data,
							 data->target_freq_hz,
							 data->target_freq_uhz);
		}
		return -EINVAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return tcs3472_set_sampling_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

/*
 * Translation from APERS field value to the number of consecutive out-of-range
 * clear channel values before an interrupt is generated
 */
static const int tcs3472_intr_pers[] = {
	0, 1, 2, 3, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60
};

static int tcs3472_read_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int *val,
	int *val2)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	unsigned int period;

	guard(mutex)(&data->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = (dir == IIO_EV_DIR_RISING) ?
			data->high_thresh : data->low_thresh;
		return IIO_VAL_INT;
	case IIO_EV_INFO_PERIOD:
		period = tcs3472_cycle_time_us(data) *
			tcs3472_intr_pers[data->apers];
		*val = period / USEC_PER_SEC;
		*val2 = period % USEC_PER_SEC;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int tcs3472_write_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int val,
	int val2)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;
	u8 command;
	int period;
	int i;

	guard(mutex)(&data->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			command = TCS3472_AIHT;
			break;
		case IIO_EV_DIR_FALLING:
			command = TCS3472_AILT;
			break;
		default:
			return -EINVAL;
		}
		ret = i2c_smbus_write_word_data(data->client, command, val);
		if (ret)
			return ret;

		if (dir == IIO_EV_DIR_RISING)
			data->high_thresh = val;
		else
			data->low_thresh = val;

		return 0;
	case IIO_EV_INFO_PERIOD:{
		unsigned int cycle_us;

		period = val * USEC_PER_SEC + val2;
		cycle_us = tcs3472_cycle_time_us(data);
		for (i = 1; i < ARRAY_SIZE(tcs3472_intr_pers) - 1; i++) {
			if (period <= cycle_us * tcs3472_intr_pers[i])
				break;
		}
		ret = i2c_smbus_write_byte_data(data->client, TCS3472_PERS, i);
		if (ret)
			return ret;

		data->apers = i;

		return 0;
	}
	default:
		return -EINVAL;
	}
}

static int tcs3472_read_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir)
{
	struct tcs3472_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->lock);

	return (data->enable & TCS3472_ENABLE_AIEN) ? 1 : 0;
}

static int tcs3472_write_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, bool state)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret = 0;
	u8 enable_old;

	guard(mutex)(&data->lock);

	enable_old = data->enable;

	if (state)
		data->enable |= TCS3472_ENABLE_AIEN;
	else
		data->enable &= ~TCS3472_ENABLE_AIEN;

	if (enable_old != data->enable) {
		ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
						data->enable);
		if (ret) {
			data->enable = enable_old;
			return ret;
		}
	}

	return 0;
}

static irqreturn_t tcs3472_event_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_STATUS);
	if (ret >= 0 && (ret & TCS3472_STATUS_AINT)) {
		iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, 0,
						IIO_EV_TYPE_THRESH,
						IIO_EV_DIR_EITHER),
				iio_get_time_ns(indio_dev));

		i2c_smbus_read_byte_data(data->client, TCS3472_INTR_CLEAR);
	}

	return IRQ_HANDLED;
}

static irqreturn_t tcs3472_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct tcs3472_data *data = iio_priv(indio_dev);
	int i, j = 0;
	/* Ensure timestamp is naturally aligned */
	struct {
		u16 chans[4];
		aligned_s64 timestamp;
	} scan = { };

	int ret = tcs3472_req_data(data);
	if (ret < 0)
		goto done;

	iio_for_each_active_channel(indio_dev, i) {
		ret = i2c_smbus_read_word_data(data->client,
			TCS3472_CDATA + 2*i);
		if (ret < 0)
			goto done;

		scan.chans[j++] = ret;
	}

	iio_push_to_buffers_with_ts(indio_dev, &scan, sizeof(scan),
		iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static ssize_t tcs3472_show_int_time_available(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	size_t len = 0;
	int i;

	for (i = 1; i <= 256; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06d ",
			2400 * i);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_CONST_ATTR(calibscale_available, "1 4 16 60");
static IIO_DEV_ATTR_INT_TIME_AVAIL(tcs3472_show_int_time_available);

static struct attribute *tcs3472_attributes[] = {
	&iio_const_attr_calibscale_available.dev_attr.attr,
	&iio_dev_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group tcs3472_attribute_group = {
	.attrs = tcs3472_attributes,
};

static const struct iio_info tcs3472_info = {
	.read_raw = tcs3472_read_raw,
	.write_raw = tcs3472_write_raw,
	.read_event_value = tcs3472_read_event,
	.write_event_value = tcs3472_write_event,
	.read_event_config = tcs3472_read_event_config,
	.write_event_config = tcs3472_write_event_config,
	.attrs = &tcs3472_attribute_group,
};

static int tcs3472_powerdown(struct tcs3472_data *data)
{
	int ret;

	guard(mutex)(&data->lock);

	data->enable_pre_suspend = data->enable;

	ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
					data->enable & ~TCS3472_ENABLE_RUN);
	if (ret)
		return ret;

	return 0;
}

static void tcs3472_powerdown_action(void *data)
{
	tcs3472_powerdown(data);
}

static int tcs3472_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tcs3472_data *data;
	struct iio_dev *indio_dev;
	unsigned int cycle_us;
	int ret;
	u8 enable;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	indio_dev->info = &tcs3472_info;
	indio_dev->name = TCS3472_DRV_NAME;
	indio_dev->channels = tcs3472_channels;
	indio_dev->num_channels = ARRAY_SIZE(tcs3472_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_ID);
	if (ret < 0)
		return ret;

	if (ret == 0x44)
		dev_info(dev, "TCS34721/34725 found\n");
	else if (ret == 0x4d)
		dev_info(dev, "TCS34723/34727 found\n");
	else
		return -ENODEV;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_CONTROL);
	if (ret < 0)
		return ret;
	data->control = ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_ATIME);
	if (ret < 0)
		return ret;
	data->atime = ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_WTIME);
	if (ret < 0)
		return ret;
	data->wtime = ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_CONFIG);
	if (ret < 0)
		return ret;
	data->wlong = (ret & TCS3472_CONFIG_WLONG) ? 1 : 0;

	ret = i2c_smbus_read_word_data(data->client, TCS3472_AILT);
	if (ret < 0)
		return ret;
	data->low_thresh = ret;

	ret = i2c_smbus_read_word_data(data->client, TCS3472_AIHT);
	if (ret < 0)
		return ret;
	data->high_thresh = ret;

	data->apers = 1;
	ret = i2c_smbus_write_byte_data(data->client, TCS3472_PERS,
					data->apers);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_ENABLE);
	if (ret < 0)
		return ret;

	/*
	 * Enable the chip in its full running state, including WEN. The
	 * actual wait time is controlled by the WTIME and WLONG registers,
	 * which retain their power-on defaults until userspace writes to
	 * sampling_frequency.
	 */
	enable = (ret | TCS3472_ENABLE_RUN) & ~TCS3472_ENABLE_AIEN;

	ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE, enable);
	if (ret < 0)
		return ret;

	data->enable = enable;

	/*
	 * Initialize target frequency from the chip's current state so that
	 * subsequent integration_time changes via IIO_CHAN_INFO_INT_TIME can
	 * preserve a meaningful sampling rate, even before userspace writes
	 * sampling_frequency for the first time.
	 */
	cycle_us = tcs3472_cycle_time_us(data);
	tcs3472_cycle_to_freq(cycle_us, &data->target_freq_hz,
			      &data->target_freq_uhz);

	ret = devm_add_action_or_reset(dev, tcs3472_powerdown_action, data);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      tcs3472_trigger_handler, NULL);
	if (ret < 0)
		return ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						tcs3472_event_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_SHARED |
						IRQF_ONESHOT,
						client->name, indio_dev);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static int tcs3472_suspend(struct device *dev)
{
	struct tcs3472_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return tcs3472_powerdown(data);
}

static int tcs3472_resume(struct device *dev)
{
	struct tcs3472_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	int ret;

	guard(mutex)(&data->lock);

	/*
	 * Restore the full ENABLE register from the snapshot taken in
	 * tcs3472_powerdown(). This preserves the user's last
	 * sampling_frequency configuration (in particular the WEN bit)
	 * across suspend/resume.
	 */
	ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
					data->enable_pre_suspend);
	if (ret)
		return ret;

	data->enable = data->enable_pre_suspend;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(tcs3472_pm_ops, tcs3472_suspend,
				tcs3472_resume);

static const struct i2c_device_id tcs3472_id[] = {
	{ .name = "tcs3472" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tcs3472_id);

static struct i2c_driver tcs3472_driver = {
	.driver = {
		.name	= TCS3472_DRV_NAME,
		.pm	= pm_sleep_ptr(&tcs3472_pm_ops),
	},
	.probe		= tcs3472_probe,
	.id_table	= tcs3472_id,
};
module_i2c_driver(tcs3472_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("TCS3472 color light sensors driver");
MODULE_LICENSE("GPL");
