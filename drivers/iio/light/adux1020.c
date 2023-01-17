// SPDX-License-Identifier: GPL-2.0+
/*
 * adux1020.c - Support for Analog Devices ADUX1020 photometric sensor
 *
 * Copyright (C) 2019 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 *
 * TODO: Triggered buffer support
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

#define ADUX1020_REGMAP_NAME		"adux1020_regmap"
#define ADUX1020_DRV_NAME		"adux1020"

/* System registers */
#define ADUX1020_REG_CHIP_ID		0x08
#define ADUX1020_REG_SLAVE_ADDRESS	0x09

#define ADUX1020_REG_SW_RESET		0x0f
#define ADUX1020_REG_INT_ENABLE		0x1c
#define ADUX1020_REG_INT_POLARITY	0x1d
#define ADUX1020_REG_PROX_TH_ON1	0x2a
#define ADUX1020_REG_PROX_TH_OFF1	0x2b
#define	ADUX1020_REG_PROX_TYPE		0x2f
#define	ADUX1020_REG_TEST_MODES_3	0x32
#define	ADUX1020_REG_FORCE_MODE		0x33
#define	ADUX1020_REG_FREQUENCY		0x40
#define ADUX1020_REG_LED_CURRENT	0x41
#define	ADUX1020_REG_OP_MODE		0x45
#define	ADUX1020_REG_INT_MASK		0x48
#define	ADUX1020_REG_INT_STATUS		0x49
#define	ADUX1020_REG_DATA_BUFFER	0x60

/* Chip ID bits */
#define ADUX1020_CHIP_ID_MASK		GENMASK(11, 0)
#define ADUX1020_CHIP_ID		0x03fc

#define ADUX1020_SW_RESET		BIT(1)
#define ADUX1020_FIFO_FLUSH		BIT(15)
#define ADUX1020_OP_MODE_MASK		GENMASK(3, 0)
#define ADUX1020_DATA_OUT_MODE_MASK	GENMASK(7, 4)
#define ADUX1020_DATA_OUT_PROX_I	FIELD_PREP(ADUX1020_DATA_OUT_MODE_MASK, 1)

#define ADUX1020_MODE_INT_MASK		GENMASK(7, 0)
#define ADUX1020_INT_ENABLE		0x2094
#define ADUX1020_INT_DISABLE		0x2090
#define ADUX1020_PROX_INT_ENABLE	0x00f0
#define ADUX1020_PROX_ON1_INT		BIT(0)
#define ADUX1020_PROX_OFF1_INT		BIT(1)
#define ADUX1020_FIFO_INT_ENABLE	0x7f
#define ADUX1020_MODE_INT_DISABLE	0xff
#define ADUX1020_MODE_INT_STATUS_MASK	GENMASK(7, 0)
#define ADUX1020_FIFO_STATUS_MASK	GENMASK(15, 8)
#define ADUX1020_INT_CLEAR		0xff
#define ADUX1020_PROX_TYPE		BIT(15)

#define ADUX1020_INT_PROX_ON1		BIT(0)
#define ADUX1020_INT_PROX_OFF1		BIT(1)

#define ADUX1020_FORCE_CLOCK_ON		0x0f4f
#define ADUX1020_FORCE_CLOCK_RESET	0x0040
#define ADUX1020_ACTIVE_4_STATE		0x0008

#define ADUX1020_PROX_FREQ_MASK		GENMASK(7, 4)
#define ADUX1020_PROX_FREQ(x)		FIELD_PREP(ADUX1020_PROX_FREQ_MASK, x)

#define ADUX1020_LED_CURRENT_MASK	GENMASK(3, 0)
#define ADUX1020_LED_PIREF_EN		BIT(12)

/* Operating modes */
enum adux1020_op_modes {
	ADUX1020_MODE_STANDBY,
	ADUX1020_MODE_PROX_I,
	ADUX1020_MODE_PROX_XY,
	ADUX1020_MODE_GEST,
	ADUX1020_MODE_SAMPLE,
	ADUX1020_MODE_FORCE = 0x0e,
	ADUX1020_MODE_IDLE = 0x0f,
};

struct adux1020_data {
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	struct mutex lock;
	struct regmap *regmap;
};

struct adux1020_mode_data {
	u8 bytes;
	u8 buf_len;
	u16 int_en;
};

static const struct adux1020_mode_data adux1020_modes[] = {
	[ADUX1020_MODE_PROX_I] = {
		.bytes = 2,
		.buf_len = 1,
		.int_en = ADUX1020_PROX_INT_ENABLE,
	},
};

static const struct regmap_config adux1020_regmap_config = {
	.name = ADUX1020_REGMAP_NAME,
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0x6F,
	.cache_type = REGCACHE_NONE,
};

static const struct reg_sequence adux1020_def_conf[] = {
	{ 0x000c, 0x000f },
	{ 0x0010, 0x1010 },
	{ 0x0011, 0x004c },
	{ 0x0012, 0x5f0c },
	{ 0x0013, 0xada5 },
	{ 0x0014, 0x0080 },
	{ 0x0015, 0x0000 },
	{ 0x0016, 0x0600 },
	{ 0x0017, 0x0000 },
	{ 0x0018, 0x2693 },
	{ 0x0019, 0x0004 },
	{ 0x001a, 0x4280 },
	{ 0x001b, 0x0060 },
	{ 0x001c, 0x2094 },
	{ 0x001d, 0x0020 },
	{ 0x001e, 0x0001 },
	{ 0x001f, 0x0100 },
	{ 0x0020, 0x0320 },
	{ 0x0021, 0x0A13 },
	{ 0x0022, 0x0320 },
	{ 0x0023, 0x0113 },
	{ 0x0024, 0x0000 },
	{ 0x0025, 0x2412 },
	{ 0x0026, 0x2412 },
	{ 0x0027, 0x0022 },
	{ 0x0028, 0x0000 },
	{ 0x0029, 0x0300 },
	{ 0x002a, 0x0700 },
	{ 0x002b, 0x0600 },
	{ 0x002c, 0x6000 },
	{ 0x002d, 0x4000 },
	{ 0x002e, 0x0000 },
	{ 0x002f, 0x0000 },
	{ 0x0030, 0x0000 },
	{ 0x0031, 0x0000 },
	{ 0x0032, 0x0040 },
	{ 0x0033, 0x0008 },
	{ 0x0034, 0xE400 },
	{ 0x0038, 0x8080 },
	{ 0x0039, 0x8080 },
	{ 0x003a, 0x2000 },
	{ 0x003b, 0x1f00 },
	{ 0x003c, 0x2000 },
	{ 0x003d, 0x2000 },
	{ 0x003e, 0x0000 },
	{ 0x0040, 0x8069 },
	{ 0x0041, 0x1f2f },
	{ 0x0042, 0x4000 },
	{ 0x0043, 0x0000 },
	{ 0x0044, 0x0008 },
	{ 0x0046, 0x0000 },
	{ 0x0048, 0x00ef },
	{ 0x0049, 0x0000 },
	{ 0x0045, 0x0000 },
};

static const int adux1020_rates[][2] = {
	{ 0, 100000 },
	{ 0, 200000 },
	{ 0, 500000 },
	{ 1, 0 },
	{ 2, 0 },
	{ 5, 0 },
	{ 10, 0 },
	{ 20, 0 },
	{ 50, 0 },
	{ 100, 0 },
	{ 190, 0 },
	{ 450, 0 },
	{ 820, 0 },
	{ 1400, 0 },
};

static const int adux1020_led_currents[][2] = {
	{ 0, 25000 },
	{ 0, 40000 },
	{ 0, 55000 },
	{ 0, 70000 },
	{ 0, 85000 },
	{ 0, 100000 },
	{ 0, 115000 },
	{ 0, 130000 },
	{ 0, 145000 },
	{ 0, 160000 },
	{ 0, 175000 },
	{ 0, 190000 },
	{ 0, 205000 },
	{ 0, 220000 },
	{ 0, 235000 },
	{ 0, 250000 },
};

static int adux1020_flush_fifo(struct adux1020_data *data)
{
	int ret;

	/* Force Idle mode */
	ret = regmap_write(data->regmap, ADUX1020_REG_FORCE_MODE,
			   ADUX1020_ACTIVE_4_STATE);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(data->regmap, ADUX1020_REG_OP_MODE,
				 ADUX1020_OP_MODE_MASK, ADUX1020_MODE_FORCE);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(data->regmap, ADUX1020_REG_OP_MODE,
				 ADUX1020_OP_MODE_MASK, ADUX1020_MODE_IDLE);
	if (ret < 0)
		return ret;

	/* Flush FIFO */
	ret = regmap_write(data->regmap, ADUX1020_REG_TEST_MODES_3,
			   ADUX1020_FORCE_CLOCK_ON);
	if (ret < 0)
		return ret;

	ret = regmap_write(data->regmap, ADUX1020_REG_INT_STATUS,
			   ADUX1020_FIFO_FLUSH);
	if (ret < 0)
		return ret;

	return regmap_write(data->regmap, ADUX1020_REG_TEST_MODES_3,
			    ADUX1020_FORCE_CLOCK_RESET);
}

static int adux1020_read_fifo(struct adux1020_data *data, u16 *buf, u8 buf_len)
{
	unsigned int regval;
	int i, ret;

	/* Enable 32MHz clock */
	ret = regmap_write(data->regmap, ADUX1020_REG_TEST_MODES_3,
			   ADUX1020_FORCE_CLOCK_ON);
	if (ret < 0)
		return ret;

	for (i = 0; i < buf_len; i++) {
		ret = regmap_read(data->regmap, ADUX1020_REG_DATA_BUFFER,
				  &regval);
		if (ret < 0)
			return ret;

		buf[i] = regval;
	}

	/* Set 32MHz clock to be controlled by internal state machine */
	return regmap_write(data->regmap, ADUX1020_REG_TEST_MODES_3,
			    ADUX1020_FORCE_CLOCK_RESET);
}

static int adux1020_set_mode(struct adux1020_data *data,
			     enum adux1020_op_modes mode)
{
	int ret;

	/* Switch to standby mode before changing the mode */
	ret = regmap_write(data->regmap, ADUX1020_REG_OP_MODE,
			   ADUX1020_MODE_STANDBY);
	if (ret < 0)
		return ret;

	/* Set data out and switch to the desired mode */
	switch (mode) {
	case ADUX1020_MODE_PROX_I:
		ret = regmap_update_bits(data->regmap, ADUX1020_REG_OP_MODE,
					 ADUX1020_DATA_OUT_MODE_MASK,
					 ADUX1020_DATA_OUT_PROX_I);
		if (ret < 0)
			return ret;

		ret = regmap_update_bits(data->regmap, ADUX1020_REG_OP_MODE,
					 ADUX1020_OP_MODE_MASK,
					 ADUX1020_MODE_PROX_I);
		if (ret < 0)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int adux1020_measure(struct adux1020_data *data,
			    enum adux1020_op_modes mode,
			    u16 *val)
{
	unsigned int status;
	int ret, tries = 50;

	/* Disable INT pin as polling is going to be used */
	ret = regmap_write(data->regmap, ADUX1020_REG_INT_ENABLE,
			   ADUX1020_INT_DISABLE);
	if (ret < 0)
		return ret;

	/* Enable mode interrupt */
	ret = regmap_update_bits(data->regmap, ADUX1020_REG_INT_MASK,
				 ADUX1020_MODE_INT_MASK,
				 adux1020_modes[mode].int_en);
	if (ret < 0)
		return ret;

	while (tries--) {
		ret = regmap_read(data->regmap, ADUX1020_REG_INT_STATUS,
				  &status);
		if (ret < 0)
			return ret;

		status &= ADUX1020_FIFO_STATUS_MASK;
		if (status >= adux1020_modes[mode].bytes)
			break;
		msleep(20);
	}

	if (tries < 0)
		return -EIO;

	ret = adux1020_read_fifo(data, val, adux1020_modes[mode].buf_len);
	if (ret < 0)
		return ret;

	/* Clear mode interrupt */
	ret = regmap_write(data->regmap, ADUX1020_REG_INT_STATUS,
			   (~adux1020_modes[mode].int_en));
	if (ret < 0)
		return ret;

	/* Disable mode interrupts */
	return regmap_update_bits(data->regmap, ADUX1020_REG_INT_MASK,
				  ADUX1020_MODE_INT_MASK,
				  ADUX1020_MODE_INT_DISABLE);
}

static int adux1020_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct adux1020_data *data = iio_priv(indio_dev);
	u16 buf[3];
	int ret = -EINVAL;
	unsigned int regval;

	mutex_lock(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_PROXIMITY:
			ret = adux1020_set_mode(data, ADUX1020_MODE_PROX_I);
			if (ret < 0)
				goto fail;

			ret = adux1020_measure(data, ADUX1020_MODE_PROX_I, buf);
			if (ret < 0)
				goto fail;

			*val = buf[0];
			ret = IIO_VAL_INT;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_CURRENT:
			ret = regmap_read(data->regmap,
					  ADUX1020_REG_LED_CURRENT, &regval);
			if (ret < 0)
				goto fail;

			regval = regval & ADUX1020_LED_CURRENT_MASK;

			*val = adux1020_led_currents[regval][0];
			*val2 = adux1020_led_currents[regval][1];

			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_PROXIMITY:
			ret = regmap_read(data->regmap, ADUX1020_REG_FREQUENCY,
					  &regval);
			if (ret < 0)
				goto fail;

			regval = FIELD_GET(ADUX1020_PROX_FREQ_MASK, regval);

			*val = adux1020_rates[regval][0];
			*val2 = adux1020_rates[regval][1];

			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

fail:
	mutex_unlock(&data->lock);

	return ret;
};

static inline int adux1020_find_index(const int array[][2], int count, int val,
				      int val2)
{
	int i;

	for (i = 0; i < count; i++)
		if (val == array[i][0] && val2 == array[i][1])
			return i;

	return -EINVAL;
}

static int adux1020_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct adux1020_data *data = iio_priv(indio_dev);
	int i, ret = -EINVAL;

	mutex_lock(&data->lock);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type == IIO_PROXIMITY) {
			i = adux1020_find_index(adux1020_rates,
						ARRAY_SIZE(adux1020_rates),
						val, val2);
			if (i < 0) {
				ret = i;
				goto fail;
			}

			ret = regmap_update_bits(data->regmap,
						 ADUX1020_REG_FREQUENCY,
						 ADUX1020_PROX_FREQ_MASK,
						 ADUX1020_PROX_FREQ(i));
		}
		break;
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->type == IIO_CURRENT) {
			i = adux1020_find_index(adux1020_led_currents,
					ARRAY_SIZE(adux1020_led_currents),
					val, val2);
			if (i < 0) {
				ret = i;
				goto fail;
			}

			ret = regmap_update_bits(data->regmap,
						 ADUX1020_REG_LED_CURRENT,
						 ADUX1020_LED_CURRENT_MASK, i);
		}
		break;
	default:
		break;
	}

fail:
	mutex_unlock(&data->lock);

	return ret;
}

static int adux1020_write_event_config(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir, int state)
{
	struct adux1020_data *data = iio_priv(indio_dev);
	int ret, mask;

	mutex_lock(&data->lock);

	ret = regmap_write(data->regmap, ADUX1020_REG_INT_ENABLE,
			   ADUX1020_INT_ENABLE);
	if (ret < 0)
		goto fail;

	ret = regmap_write(data->regmap, ADUX1020_REG_INT_POLARITY, 0);
	if (ret < 0)
		goto fail;

	switch (chan->type) {
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			mask = ADUX1020_PROX_ON1_INT;
		else
			mask = ADUX1020_PROX_OFF1_INT;

		if (state)
			state = 0;
		else
			state = mask;

		ret = regmap_update_bits(data->regmap, ADUX1020_REG_INT_MASK,
					 mask, state);
		if (ret < 0)
			goto fail;

		/*
		 * Trigger proximity interrupt when the intensity is above
		 * or below threshold
		 */
		ret = regmap_update_bits(data->regmap, ADUX1020_REG_PROX_TYPE,
					 ADUX1020_PROX_TYPE,
					 ADUX1020_PROX_TYPE);
		if (ret < 0)
			goto fail;

		/* Set proximity mode */
		ret = adux1020_set_mode(data, ADUX1020_MODE_PROX_I);
		break;
	default:
		ret = -EINVAL;
		break;
	}

fail:
	mutex_unlock(&data->lock);

	return ret;
}

static int adux1020_read_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir)
{
	struct adux1020_data *data = iio_priv(indio_dev);
	int ret, mask;
	unsigned int regval;

	switch (chan->type) {
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			mask = ADUX1020_PROX_ON1_INT;
		else
			mask = ADUX1020_PROX_OFF1_INT;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(data->regmap, ADUX1020_REG_INT_MASK, &regval);
	if (ret < 0)
		return ret;

	return !(regval & mask);
}

static int adux1020_read_thresh(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info, int *val, int *val2)
{
	struct adux1020_data *data = iio_priv(indio_dev);
	u8 reg;
	int ret;
	unsigned int regval;

	switch (chan->type) {
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			reg = ADUX1020_REG_PROX_TH_ON1;
		else
			reg = ADUX1020_REG_PROX_TH_OFF1;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(data->regmap, reg, &regval);
	if (ret < 0)
		return ret;

	*val = regval;

	return IIO_VAL_INT;
}

static int adux1020_write_thresh(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int val, int val2)
{
	struct adux1020_data *data = iio_priv(indio_dev);
	u8 reg;

	switch (chan->type) {
	case IIO_PROXIMITY:
		if (dir == IIO_EV_DIR_RISING)
			reg = ADUX1020_REG_PROX_TH_ON1;
		else
			reg = ADUX1020_REG_PROX_TH_OFF1;
		break;
	default:
		return -EINVAL;
	}

	/* Full scale threshold value is 0-65535  */
	if (val < 0 || val > 65535)
		return -EINVAL;

	return regmap_write(data->regmap, reg, val);
}

static const struct iio_event_spec adux1020_proximity_event[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec adux1020_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.event_spec = adux1020_proximity_event,
		.num_event_specs = ARRAY_SIZE(adux1020_proximity_event),
	},
	{
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.extend_name = "led",
		.output = 1,
	},
};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
		      "0.1 0.2 0.5 1 2 5 10 20 50 100 190 450 820 1400");

static struct attribute *adux1020_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group adux1020_attribute_group = {
	.attrs = adux1020_attributes,
};

static const struct iio_info adux1020_info = {
	.attrs = &adux1020_attribute_group,
	.read_raw = adux1020_read_raw,
	.write_raw = adux1020_write_raw,
	.read_event_config = adux1020_read_event_config,
	.write_event_config = adux1020_write_event_config,
	.read_event_value = adux1020_read_thresh,
	.write_event_value = adux1020_write_thresh,
};

static irqreturn_t adux1020_interrupt_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adux1020_data *data = iio_priv(indio_dev);
	int ret, status;

	ret = regmap_read(data->regmap, ADUX1020_REG_INT_STATUS, &status);
	if (ret < 0)
		return IRQ_HANDLED;

	status &= ADUX1020_MODE_INT_STATUS_MASK;

	if (status & ADUX1020_INT_PROX_ON1) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       iio_get_time_ns(indio_dev));
	}

	if (status & ADUX1020_INT_PROX_OFF1) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       iio_get_time_ns(indio_dev));
	}

	regmap_update_bits(data->regmap, ADUX1020_REG_INT_STATUS,
			   ADUX1020_MODE_INT_MASK, ADUX1020_INT_CLEAR);

	return IRQ_HANDLED;
}

static int adux1020_chip_init(struct adux1020_data *data)
{
	struct i2c_client *client = data->client;
	int ret;
	unsigned int val;

	ret = regmap_read(data->regmap, ADUX1020_REG_CHIP_ID, &val);
	if (ret < 0)
		return ret;

	if ((val & ADUX1020_CHIP_ID_MASK) != ADUX1020_CHIP_ID) {
		dev_err(&client->dev, "invalid chip id 0x%04x\n", val);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "Detected ADUX1020 with chip id: 0x%04x\n", val);

	ret = regmap_update_bits(data->regmap, ADUX1020_REG_SW_RESET,
				 ADUX1020_SW_RESET, ADUX1020_SW_RESET);
	if (ret < 0)
		return ret;

	/* Load default configuration */
	ret = regmap_multi_reg_write(data->regmap, adux1020_def_conf,
				     ARRAY_SIZE(adux1020_def_conf));
	if (ret < 0)
		return ret;

	ret = adux1020_flush_fifo(data);
	if (ret < 0)
		return ret;

	/* Use LED_IREF for proximity mode */
	ret = regmap_update_bits(data->regmap, ADUX1020_REG_LED_CURRENT,
				 ADUX1020_LED_PIREF_EN, 0);
	if (ret < 0)
		return ret;

	/* Mask all interrupts */
	return regmap_update_bits(data->regmap, ADUX1020_REG_INT_MASK,
			   ADUX1020_MODE_INT_MASK, ADUX1020_MODE_INT_DISABLE);
}

static int adux1020_probe(struct i2c_client *client)
{
	struct adux1020_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &adux1020_info;
	indio_dev->name = ADUX1020_DRV_NAME;
	indio_dev->channels = adux1020_channels;
	indio_dev->num_channels = ARRAY_SIZE(adux1020_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	data = iio_priv(indio_dev);

	data->regmap = devm_regmap_init_i2c(client, &adux1020_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(&client->dev, "regmap initialization failed.\n");
		return PTR_ERR(data->regmap);
	}

	data->client = client;
	data->indio_dev = indio_dev;
	mutex_init(&data->lock);

	ret = adux1020_chip_init(data);
	if (ret)
		return ret;

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, adux1020_interrupt_handler,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					ADUX1020_DRV_NAME, indio_dev);
		if (ret) {
			dev_err(&client->dev, "irq request error %d\n", -ret);
			return ret;
		}
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id adux1020_id[] = {
	{ "adux1020", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, adux1020_id);

static const struct of_device_id adux1020_of_match[] = {
	{ .compatible = "adi,adux1020" },
	{ }
};
MODULE_DEVICE_TABLE(of, adux1020_of_match);

static struct i2c_driver adux1020_driver = {
	.driver = {
		.name	= ADUX1020_DRV_NAME,
		.of_match_table = adux1020_of_match,
	},
	.probe_new	= adux1020_probe,
	.id_table	= adux1020_id,
};
module_i2c_driver(adux1020_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("ADUX1020 photometric sensor");
MODULE_LICENSE("GPL");
