// SPDX-License-Identifier: GPL-2.0-only
/*
 * vcnl4000.c - Support for Vishay VCNL4000/4010/4020/4040/4200 combined ambient
 * light and proximity sensor
 *
 * Copyright 2012 Peter Meerwald <pmeerw@pmeerw.net>
 * Copyright 2019 Pursim SPC
 * Copyright 2020 Mathieu Othacehe <m.othacehe@gmail.com>
 *
 * IIO driver for:
 *   VCNL4000/10/20 (7-bit I2C slave address 0x13)
 *   VCNL4040 (7-bit I2C slave address 0x60)
 *   VCNL4200 (7-bit I2C slave address 0x51)
 *
 * TODO:
 *   allow to adjust IR current
 *   interrupts (VCNL4040, VCNL4200)
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define VCNL4000_DRV_NAME "vcnl4000"
#define VCNL4000_PROD_ID	0x01
#define VCNL4010_PROD_ID	0x02 /* for VCNL4020, VCNL4010 */
#define VCNL4040_PROD_ID	0x86
#define VCNL4200_PROD_ID	0x58

#define VCNL4000_COMMAND	0x80 /* Command register */
#define VCNL4000_PROD_REV	0x81 /* Product ID and Revision ID */
#define VCNL4010_PROX_RATE      0x82 /* Proximity rate */
#define VCNL4000_LED_CURRENT	0x83 /* IR LED current for proximity mode */
#define VCNL4000_AL_PARAM	0x84 /* Ambient light parameter register */
#define VCNL4010_ALS_PARAM      0x84 /* ALS rate */
#define VCNL4000_AL_RESULT_HI	0x85 /* Ambient light result register, MSB */
#define VCNL4000_AL_RESULT_LO	0x86 /* Ambient light result register, LSB */
#define VCNL4000_PS_RESULT_HI	0x87 /* Proximity result register, MSB */
#define VCNL4000_PS_RESULT_LO	0x88 /* Proximity result register, LSB */
#define VCNL4000_PS_MEAS_FREQ	0x89 /* Proximity test signal frequency */
#define VCNL4010_INT_CTRL	0x89 /* Interrupt control */
#define VCNL4000_PS_MOD_ADJ	0x8a /* Proximity modulator timing adjustment */
#define VCNL4010_LOW_THR_HI     0x8a /* Low threshold, MSB */
#define VCNL4010_LOW_THR_LO     0x8b /* Low threshold, LSB */
#define VCNL4010_HIGH_THR_HI    0x8c /* High threshold, MSB */
#define VCNL4010_HIGH_THR_LO    0x8d /* High threshold, LSB */
#define VCNL4010_ISR		0x8e /* Interrupt status */

#define VCNL4200_AL_CONF	0x00 /* Ambient light configuration */
#define VCNL4200_PS_CONF1	0x03 /* Proximity configuration */
#define VCNL4200_PS_DATA	0x08 /* Proximity data */
#define VCNL4200_AL_DATA	0x09 /* Ambient light data */
#define VCNL4200_DEV_ID		0x0e /* Device ID, slave address and version */

#define VCNL4040_DEV_ID		0x0c /* Device ID and version */

/* Bit masks for COMMAND register */
#define VCNL4000_AL_RDY		BIT(6) /* ALS data ready? */
#define VCNL4000_PS_RDY		BIT(5) /* proximity data ready? */
#define VCNL4000_AL_OD		BIT(4) /* start on-demand ALS measurement */
#define VCNL4000_PS_OD		BIT(3) /* start on-demand proximity measurement */
#define VCNL4000_ALS_EN		BIT(2) /* start ALS measurement */
#define VCNL4000_PROX_EN	BIT(1) /* start proximity measurement */
#define VCNL4000_SELF_TIMED_EN	BIT(0) /* start self-timed measurement */

/* Bit masks for interrupt registers. */
#define VCNL4010_INT_THR_SEL	BIT(0) /* Select threshold interrupt source */
#define VCNL4010_INT_THR_EN	BIT(1) /* Threshold interrupt type */
#define VCNL4010_INT_ALS_EN	BIT(2) /* Enable on ALS data ready */
#define VCNL4010_INT_PROX_EN	BIT(3) /* Enable on proximity data ready */

#define VCNL4010_INT_THR_HIGH	0 /* High threshold exceeded */
#define VCNL4010_INT_THR_LOW	1 /* Low threshold exceeded */
#define VCNL4010_INT_ALS	2 /* ALS data ready */
#define VCNL4010_INT_PROXIMITY	3 /* Proximity data ready */

#define VCNL4010_INT_THR \
	(BIT(VCNL4010_INT_THR_LOW) | BIT(VCNL4010_INT_THR_HIGH))
#define VCNL4010_INT_DRDY \
	(BIT(VCNL4010_INT_PROXIMITY) | BIT(VCNL4010_INT_ALS))

static const int vcnl4010_prox_sampling_frequency[][2] = {
	{1, 950000},
	{3, 906250},
	{7, 812500},
	{16, 625000},
	{31, 250000},
	{62, 500000},
	{125, 0},
	{250, 0},
};

#define VCNL4000_SLEEP_DELAY_MS	2000 /* before we enter pm_runtime_suspend */

enum vcnl4000_device_ids {
	VCNL4000,
	VCNL4010,
	VCNL4040,
	VCNL4200,
};

struct vcnl4200_channel {
	u8 reg;
	ktime_t last_measurement;
	ktime_t sampling_rate;
	struct mutex lock;
};

struct vcnl4000_data {
	struct i2c_client *client;
	enum vcnl4000_device_ids id;
	int rev;
	int al_scale;
	const struct vcnl4000_chip_spec *chip_spec;
	struct mutex vcnl4000_lock;
	struct vcnl4200_channel vcnl4200_al;
	struct vcnl4200_channel vcnl4200_ps;
	uint32_t near_level;
};

struct vcnl4000_chip_spec {
	const char *prod;
	struct iio_chan_spec const *channels;
	const int num_channels;
	const struct iio_info *info;
	bool irq_support;
	int (*init)(struct vcnl4000_data *data);
	int (*measure_light)(struct vcnl4000_data *data, int *val);
	int (*measure_proximity)(struct vcnl4000_data *data, int *val);
	int (*set_power_state)(struct vcnl4000_data *data, bool on);
};

static const struct i2c_device_id vcnl4000_id[] = {
	{ "vcnl4000", VCNL4000 },
	{ "vcnl4010", VCNL4010 },
	{ "vcnl4020", VCNL4010 },
	{ "vcnl4040", VCNL4040 },
	{ "vcnl4200", VCNL4200 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vcnl4000_id);

static int vcnl4000_set_power_state(struct vcnl4000_data *data, bool on)
{
	/* no suspend op */
	return 0;
}

static int vcnl4000_init(struct vcnl4000_data *data)
{
	int ret, prod_id;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4000_PROD_REV);
	if (ret < 0)
		return ret;

	prod_id = ret >> 4;
	switch (prod_id) {
	case VCNL4000_PROD_ID:
		if (data->id != VCNL4000)
			dev_warn(&data->client->dev,
					"wrong device id, use vcnl4000");
		break;
	case VCNL4010_PROD_ID:
		if (data->id != VCNL4010)
			dev_warn(&data->client->dev,
					"wrong device id, use vcnl4010/4020");
		break;
	default:
		return -ENODEV;
	}

	data->rev = ret & 0xf;
	data->al_scale = 250000;
	mutex_init(&data->vcnl4000_lock);

	return data->chip_spec->set_power_state(data, true);
};

static int vcnl4200_set_power_state(struct vcnl4000_data *data, bool on)
{
	u16 val = on ? 0 /* power on */ : 1 /* shut down */;
	int ret;

	ret = i2c_smbus_write_word_data(data->client, VCNL4200_AL_CONF, val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_word_data(data->client, VCNL4200_PS_CONF1, val);
	if (ret < 0)
		return ret;

	if (on) {
		/* Wait at least one integration cycle before fetching data */
		data->vcnl4200_al.last_measurement = ktime_get();
		data->vcnl4200_ps.last_measurement = ktime_get();
	}

	return 0;
}

static int vcnl4200_init(struct vcnl4000_data *data)
{
	int ret, id;

	ret = i2c_smbus_read_word_data(data->client, VCNL4200_DEV_ID);
	if (ret < 0)
		return ret;

	id = ret & 0xff;

	if (id != VCNL4200_PROD_ID) {
		ret = i2c_smbus_read_word_data(data->client, VCNL4040_DEV_ID);
		if (ret < 0)
			return ret;

		id = ret & 0xff;

		if (id != VCNL4040_PROD_ID)
			return -ENODEV;
	}

	dev_dbg(&data->client->dev, "device id 0x%x", id);

	data->rev = (ret >> 8) & 0xf;

	data->vcnl4200_al.reg = VCNL4200_AL_DATA;
	data->vcnl4200_ps.reg = VCNL4200_PS_DATA;
	switch (id) {
	case VCNL4200_PROD_ID:
		/* Default wait time is 50ms, add 20% tolerance. */
		data->vcnl4200_al.sampling_rate = ktime_set(0, 60000 * 1000);
		/* Default wait time is 4.8ms, add 20% tolerance. */
		data->vcnl4200_ps.sampling_rate = ktime_set(0, 5760 * 1000);
		data->al_scale = 24000;
		break;
	case VCNL4040_PROD_ID:
		/* Default wait time is 80ms, add 20% tolerance. */
		data->vcnl4200_al.sampling_rate = ktime_set(0, 96000 * 1000);
		/* Default wait time is 5ms, add 20% tolerance. */
		data->vcnl4200_ps.sampling_rate = ktime_set(0, 6000 * 1000);
		data->al_scale = 120000;
		break;
	}
	mutex_init(&data->vcnl4200_al.lock);
	mutex_init(&data->vcnl4200_ps.lock);

	ret = data->chip_spec->set_power_state(data, true);
	if (ret < 0)
		return ret;

	return 0;
};

static int vcnl4000_read_data(struct vcnl4000_data *data, u8 data_reg, int *val)
{
	s32 ret;

	ret = i2c_smbus_read_word_swapped(data->client, data_reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int vcnl4000_write_data(struct vcnl4000_data *data, u8 data_reg, int val)
{
	if (val > U16_MAX)
		return -ERANGE;

	return i2c_smbus_write_word_swapped(data->client, data_reg, val);
}


static int vcnl4000_measure(struct vcnl4000_data *data, u8 req_mask,
				u8 rdy_mask, u8 data_reg, int *val)
{
	int tries = 20;
	int ret;

	mutex_lock(&data->vcnl4000_lock);

	ret = i2c_smbus_write_byte_data(data->client, VCNL4000_COMMAND,
					req_mask);
	if (ret < 0)
		goto fail;

	/* wait for data to become ready */
	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, VCNL4000_COMMAND);
		if (ret < 0)
			goto fail;
		if (ret & rdy_mask)
			break;
		msleep(20); /* measurement takes up to 100 ms */
	}

	if (tries < 0) {
		dev_err(&data->client->dev,
			"vcnl4000_measure() failed, data not ready\n");
		ret = -EIO;
		goto fail;
	}

	ret = vcnl4000_read_data(data, data_reg, val);
	if (ret < 0)
		goto fail;

	mutex_unlock(&data->vcnl4000_lock);

	return 0;

fail:
	mutex_unlock(&data->vcnl4000_lock);
	return ret;
}

static int vcnl4200_measure(struct vcnl4000_data *data,
		struct vcnl4200_channel *chan, int *val)
{
	int ret;
	s64 delta;
	ktime_t next_measurement;

	mutex_lock(&chan->lock);

	next_measurement = ktime_add(chan->last_measurement,
			chan->sampling_rate);
	delta = ktime_us_delta(next_measurement, ktime_get());
	if (delta > 0)
		usleep_range(delta, delta + 500);
	chan->last_measurement = ktime_get();

	mutex_unlock(&chan->lock);

	ret = i2c_smbus_read_word_data(data->client, chan->reg);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int vcnl4000_measure_light(struct vcnl4000_data *data, int *val)
{
	return vcnl4000_measure(data,
			VCNL4000_AL_OD, VCNL4000_AL_RDY,
			VCNL4000_AL_RESULT_HI, val);
}

static int vcnl4200_measure_light(struct vcnl4000_data *data, int *val)
{
	return vcnl4200_measure(data, &data->vcnl4200_al, val);
}

static int vcnl4000_measure_proximity(struct vcnl4000_data *data, int *val)
{
	return vcnl4000_measure(data,
			VCNL4000_PS_OD, VCNL4000_PS_RDY,
			VCNL4000_PS_RESULT_HI, val);
}

static int vcnl4200_measure_proximity(struct vcnl4000_data *data, int *val)
{
	return vcnl4200_measure(data, &data->vcnl4200_ps, val);
}

static int vcnl4010_read_proxy_samp_freq(struct vcnl4000_data *data, int *val,
					 int *val2)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4010_PROX_RATE);
	if (ret < 0)
		return ret;

	if (ret >= ARRAY_SIZE(vcnl4010_prox_sampling_frequency))
		return -EINVAL;

	*val = vcnl4010_prox_sampling_frequency[ret][0];
	*val2 = vcnl4010_prox_sampling_frequency[ret][1];

	return 0;
}

static bool vcnl4010_is_in_periodic_mode(struct vcnl4000_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4000_COMMAND);
	if (ret < 0)
		return false;

	return !!(ret & VCNL4000_SELF_TIMED_EN);
}

static int vcnl4000_set_pm_runtime_state(struct vcnl4000_data *data, bool on)
{
	struct device *dev = &data->client->dev;
	int ret;

	if (on) {
		ret = pm_runtime_resume_and_get(dev);
	} else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	return ret;
}

static int vcnl4000_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	int ret;
	struct vcnl4000_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = vcnl4000_set_pm_runtime_state(data, true);
		if  (ret < 0)
			return ret;

		switch (chan->type) {
		case IIO_LIGHT:
			ret = data->chip_spec->measure_light(data, val);
			if (!ret)
				ret = IIO_VAL_INT;
			break;
		case IIO_PROXIMITY:
			ret = data->chip_spec->measure_proximity(data, val);
			if (!ret)
				ret = IIO_VAL_INT;
			break;
		default:
			ret = -EINVAL;
		}
		vcnl4000_set_pm_runtime_state(data, false);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_LIGHT)
			return -EINVAL;

		*val = 0;
		*val2 = data->al_scale;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int vcnl4010_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	int ret;
	struct vcnl4000_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_SCALE:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		/* Protect against event capture. */
		if (vcnl4010_is_in_periodic_mode(data)) {
			ret = -EBUSY;
		} else {
			ret = vcnl4000_read_raw(indio_dev, chan, val, val2,
						mask);
		}

		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_PROXIMITY:
			ret = vcnl4010_read_proxy_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int vcnl4010_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type, int *length,
			       long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)vcnl4010_prox_sampling_frequency;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = 2 * ARRAY_SIZE(vcnl4010_prox_sampling_frequency);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int vcnl4010_write_proxy_samp_freq(struct vcnl4000_data *data, int val,
					  int val2)
{
	unsigned int i;
	int index = -1;

	for (i = 0; i < ARRAY_SIZE(vcnl4010_prox_sampling_frequency); i++) {
		if (val == vcnl4010_prox_sampling_frequency[i][0] &&
		    val2 == vcnl4010_prox_sampling_frequency[i][1]) {
			index = i;
			break;
		}
	}

	if (index < 0)
		return -EINVAL;

	return i2c_smbus_write_byte_data(data->client, VCNL4010_PROX_RATE,
					 index);
}

static int vcnl4010_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	int ret;
	struct vcnl4000_data *data = iio_priv(indio_dev);

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	/* Protect against event capture. */
	if (vcnl4010_is_in_periodic_mode(data)) {
		ret = -EBUSY;
		goto end;
	}

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_PROXIMITY:
			ret = vcnl4010_write_proxy_samp_freq(data, val, val2);
			goto end;
		default:
			ret = -EINVAL;
			goto end;
		}
	default:
		ret = -EINVAL;
		goto end;
	}

end:
	iio_device_release_direct_mode(indio_dev);
	return ret;
}

static int vcnl4010_read_event(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info,
			       int *val, int *val2)
{
	int ret;
	struct vcnl4000_data *data = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = vcnl4000_read_data(data, VCNL4010_HIGH_THR_HI,
						 val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = vcnl4000_read_data(data, VCNL4010_LOW_THR_HI,
						 val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int vcnl4010_write_event(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info,
				int val, int val2)
{
	int ret;
	struct vcnl4000_data *data = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = vcnl4000_write_data(data, VCNL4010_HIGH_THR_HI,
						  val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = vcnl4000_write_data(data, VCNL4010_LOW_THR_HI,
						  val);
			if (ret < 0)
				return ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static bool vcnl4010_is_thr_enabled(struct vcnl4000_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4010_INT_CTRL);
	if (ret < 0)
		return false;

	return !!(ret & VCNL4010_INT_THR_EN);
}

static int vcnl4010_read_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir)
{
	struct vcnl4000_data *data = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_PROXIMITY:
		return vcnl4010_is_thr_enabled(data);
	default:
		return -EINVAL;
	}
}

static int vcnl4010_config_threshold(struct iio_dev *indio_dev, bool state)
{
	struct vcnl4000_data *data = iio_priv(indio_dev);
	int ret;
	int icr;
	int command;

	if (state) {
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		/* Enable periodic measurement of proximity data. */
		command = VCNL4000_SELF_TIMED_EN | VCNL4000_PROX_EN;

		/*
		 * Enable interrupts on threshold, for proximity data by
		 * default.
		 */
		icr = VCNL4010_INT_THR_EN;
	} else {
		if (!vcnl4010_is_thr_enabled(data))
			return 0;

		command = 0;
		icr = 0;
	}

	ret = i2c_smbus_write_byte_data(data->client, VCNL4000_COMMAND,
					command);
	if (ret < 0)
		goto end;

	ret = i2c_smbus_write_byte_data(data->client, VCNL4010_INT_CTRL, icr);

end:
	if (state)
		iio_device_release_direct_mode(indio_dev);

	return ret;
}

static int vcnl4010_write_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       int state)
{
	switch (chan->type) {
	case IIO_PROXIMITY:
		return vcnl4010_config_threshold(indio_dev, state);
	default:
		return -EINVAL;
	}
}

static ssize_t vcnl4000_read_near_level(struct iio_dev *indio_dev,
					uintptr_t priv,
					const struct iio_chan_spec *chan,
					char *buf)
{
	struct vcnl4000_data *data = iio_priv(indio_dev);

	return sprintf(buf, "%u\n", data->near_level);
}

static const struct iio_chan_spec_ext_info vcnl4000_ext_info[] = {
	{
		.name = "nearlevel",
		.shared = IIO_SEPARATE,
		.read = vcnl4000_read_near_level,
	},
	{ /* sentinel */ }
};

static const struct iio_event_spec vcnl4000_event_spec[] = {
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
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	}
};

static const struct iio_chan_spec vcnl4000_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	}, {
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.ext_info = vcnl4000_ext_info,
	}
};

static const struct iio_chan_spec vcnl4010_channels[] = {
	{
		.type = IIO_LIGHT,
		.scan_index = -1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
	}, {
		.type = IIO_PROXIMITY,
		.scan_index = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.event_spec = vcnl4000_event_spec,
		.num_event_specs = ARRAY_SIZE(vcnl4000_event_spec),
		.ext_info = vcnl4000_ext_info,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_info vcnl4000_info = {
	.read_raw = vcnl4000_read_raw,
};

static const struct iio_info vcnl4010_info = {
	.read_raw = vcnl4010_read_raw,
	.read_avail = vcnl4010_read_avail,
	.write_raw = vcnl4010_write_raw,
	.read_event_value = vcnl4010_read_event,
	.write_event_value = vcnl4010_write_event,
	.read_event_config = vcnl4010_read_event_config,
	.write_event_config = vcnl4010_write_event_config,
};

static const struct vcnl4000_chip_spec vcnl4000_chip_spec_cfg[] = {
	[VCNL4000] = {
		.prod = "VCNL4000",
		.init = vcnl4000_init,
		.measure_light = vcnl4000_measure_light,
		.measure_proximity = vcnl4000_measure_proximity,
		.set_power_state = vcnl4000_set_power_state,
		.channels = vcnl4000_channels,
		.num_channels = ARRAY_SIZE(vcnl4000_channels),
		.info = &vcnl4000_info,
		.irq_support = false,
	},
	[VCNL4010] = {
		.prod = "VCNL4010/4020",
		.init = vcnl4000_init,
		.measure_light = vcnl4000_measure_light,
		.measure_proximity = vcnl4000_measure_proximity,
		.set_power_state = vcnl4000_set_power_state,
		.channels = vcnl4010_channels,
		.num_channels = ARRAY_SIZE(vcnl4010_channels),
		.info = &vcnl4010_info,
		.irq_support = true,
	},
	[VCNL4040] = {
		.prod = "VCNL4040",
		.init = vcnl4200_init,
		.measure_light = vcnl4200_measure_light,
		.measure_proximity = vcnl4200_measure_proximity,
		.set_power_state = vcnl4200_set_power_state,
		.channels = vcnl4000_channels,
		.num_channels = ARRAY_SIZE(vcnl4000_channels),
		.info = &vcnl4000_info,
		.irq_support = false,
	},
	[VCNL4200] = {
		.prod = "VCNL4200",
		.init = vcnl4200_init,
		.measure_light = vcnl4200_measure_light,
		.measure_proximity = vcnl4200_measure_proximity,
		.set_power_state = vcnl4200_set_power_state,
		.channels = vcnl4000_channels,
		.num_channels = ARRAY_SIZE(vcnl4000_channels),
		.info = &vcnl4000_info,
		.irq_support = false,
	},
};

static irqreturn_t vcnl4010_irq_thread(int irq, void *p)
{
	struct iio_dev *indio_dev = p;
	struct vcnl4000_data *data = iio_priv(indio_dev);
	unsigned long isr;
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4010_ISR);
	if (ret < 0)
		goto end;

	isr = ret;

	if (isr & VCNL4010_INT_THR) {
		if (test_bit(VCNL4010_INT_THR_LOW, &isr)) {
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(
					       IIO_PROXIMITY,
					       1,
					       IIO_EV_TYPE_THRESH,
					       IIO_EV_DIR_FALLING),
				       iio_get_time_ns(indio_dev));
		}

		if (test_bit(VCNL4010_INT_THR_HIGH, &isr)) {
			iio_push_event(indio_dev,
				       IIO_UNMOD_EVENT_CODE(
					       IIO_PROXIMITY,
					       1,
					       IIO_EV_TYPE_THRESH,
					       IIO_EV_DIR_RISING),
				       iio_get_time_ns(indio_dev));
		}

		i2c_smbus_write_byte_data(data->client, VCNL4010_ISR,
					  isr & VCNL4010_INT_THR);
	}

	if (isr & VCNL4010_INT_DRDY && iio_buffer_enabled(indio_dev))
		iio_trigger_poll_chained(indio_dev->trig);

end:
	return IRQ_HANDLED;
}

static irqreturn_t vcnl4010_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct vcnl4000_data *data = iio_priv(indio_dev);
	const unsigned long *active_scan_mask = indio_dev->active_scan_mask;
	u16 buffer[8] __aligned(8) = {0}; /* 1x16-bit + naturally aligned ts */
	bool data_read = false;
	unsigned long isr;
	int val = 0;
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4010_ISR);
	if (ret < 0)
		goto end;

	isr = ret;

	if (test_bit(0, active_scan_mask)) {
		if (test_bit(VCNL4010_INT_PROXIMITY, &isr)) {
			ret = vcnl4000_read_data(data,
						 VCNL4000_PS_RESULT_HI,
						 &val);
			if (ret < 0)
				goto end;

			buffer[0] = val;
			data_read = true;
		}
	}

	ret = i2c_smbus_write_byte_data(data->client, VCNL4010_ISR,
					isr & VCNL4010_INT_DRDY);
	if (ret < 0)
		goto end;

	if (!data_read)
		goto end;

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
					   iio_get_time_ns(indio_dev));

end:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int vcnl4010_buffer_postenable(struct iio_dev *indio_dev)
{
	struct vcnl4000_data *data = iio_priv(indio_dev);
	int ret;
	int cmd;

	/* Do not enable the buffer if we are already capturing events. */
	if (vcnl4010_is_in_periodic_mode(data))
		return -EBUSY;

	ret = i2c_smbus_write_byte_data(data->client, VCNL4010_INT_CTRL,
					VCNL4010_INT_PROX_EN);
	if (ret < 0)
		return ret;

	cmd = VCNL4000_SELF_TIMED_EN | VCNL4000_PROX_EN;
	return i2c_smbus_write_byte_data(data->client, VCNL4000_COMMAND, cmd);
}

static int vcnl4010_buffer_predisable(struct iio_dev *indio_dev)
{
	struct vcnl4000_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, VCNL4010_INT_CTRL, 0);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(data->client, VCNL4000_COMMAND, 0);
}

static const struct iio_buffer_setup_ops vcnl4010_buffer_ops = {
	.postenable = &vcnl4010_buffer_postenable,
	.predisable = &vcnl4010_buffer_predisable,
};

static const struct iio_trigger_ops vcnl4010_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static int vcnl4010_probe_trigger(struct iio_dev *indio_dev)
{
	struct vcnl4000_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	struct iio_trigger *trigger;

	trigger = devm_iio_trigger_alloc(&client->dev, "%s-dev%d",
					 indio_dev->name,
					 iio_device_id(indio_dev));
	if (!trigger)
		return -ENOMEM;

	trigger->ops = &vcnl4010_trigger_ops;
	iio_trigger_set_drvdata(trigger, indio_dev);

	return devm_iio_trigger_register(&client->dev, trigger);
}

static int vcnl4000_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct vcnl4000_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->id = id->driver_data;
	data->chip_spec = &vcnl4000_chip_spec_cfg[data->id];

	ret = data->chip_spec->init(data);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "%s Ambient light/proximity sensor, Rev: %02x\n",
		data->chip_spec->prod, data->rev);

	if (device_property_read_u32(&client->dev, "proximity-near-level",
				     &data->near_level))
		data->near_level = 0;

	indio_dev->info = data->chip_spec->info;
	indio_dev->channels = data->chip_spec->channels;
	indio_dev->num_channels = data->chip_spec->num_channels;
	indio_dev->name = VCNL4000_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (client->irq && data->chip_spec->irq_support) {
		ret = devm_iio_triggered_buffer_setup(&client->dev, indio_dev,
						      NULL,
						      vcnl4010_trigger_handler,
						      &vcnl4010_buffer_ops);
		if (ret < 0) {
			dev_err(&client->dev,
				"unable to setup iio triggered buffer\n");
			return ret;
		}

		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, vcnl4010_irq_thread,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"vcnl4010_irq",
						indio_dev);
		if (ret < 0) {
			dev_err(&client->dev, "irq request failed\n");
			return ret;
		}

		ret = vcnl4010_probe_trigger(indio_dev);
		if (ret < 0)
			return ret;
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret < 0)
		goto fail_poweroff;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto fail_poweroff;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, VCNL4000_SLEEP_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	return 0;
fail_poweroff:
	data->chip_spec->set_power_state(data, false);
	return ret;
}

static const struct of_device_id vcnl_4000_of_match[] = {
	{
		.compatible = "vishay,vcnl4000",
		.data = (void *)VCNL4000,
	},
	{
		.compatible = "vishay,vcnl4010",
		.data = (void *)VCNL4010,
	},
	{
		.compatible = "vishay,vcnl4020",
		.data = (void *)VCNL4010,
	},
	{
		.compatible = "vishay,vcnl4040",
		.data = (void *)VCNL4040,
	},
	{
		.compatible = "vishay,vcnl4200",
		.data = (void *)VCNL4200,
	},
	{},
};
MODULE_DEVICE_TABLE(of, vcnl_4000_of_match);

static int vcnl4000_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct vcnl4000_data *data = iio_priv(indio_dev);

	pm_runtime_dont_use_autosuspend(&client->dev);
	pm_runtime_disable(&client->dev);
	iio_device_unregister(indio_dev);
	pm_runtime_set_suspended(&client->dev);

	return data->chip_spec->set_power_state(data, false);
}

static int __maybe_unused vcnl4000_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct vcnl4000_data *data = iio_priv(indio_dev);

	return data->chip_spec->set_power_state(data, false);
}

static int __maybe_unused vcnl4000_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct vcnl4000_data *data = iio_priv(indio_dev);

	return data->chip_spec->set_power_state(data, true);
}

static const struct dev_pm_ops vcnl4000_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(vcnl4000_runtime_suspend,
			   vcnl4000_runtime_resume, NULL)
};

static struct i2c_driver vcnl4000_driver = {
	.driver = {
		.name   = VCNL4000_DRV_NAME,
		.pm	= &vcnl4000_pm_ops,
		.of_match_table = vcnl_4000_of_match,
	},
	.probe  = vcnl4000_probe,
	.id_table = vcnl4000_id,
	.remove	= vcnl4000_remove,
};

module_i2c_driver(vcnl4000_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_AUTHOR("Mathieu Othacehe <m.othacehe@gmail.com>");
MODULE_DESCRIPTION("Vishay VCNL4000 proximity/ambient light sensor driver");
MODULE_LICENSE("GPL");
