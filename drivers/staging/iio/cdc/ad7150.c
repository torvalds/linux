// SPDX-License-Identifier: GPL-2.0+
/*
 * AD7150 capacitive sensor driver supporting AD7150/1/6
 *
 * Copyright 2010-2011 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
/*
 * AD7150 registers definition
 */

#define AD7150_STATUS              0
#define AD7150_STATUS_OUT1         BIT(3)
#define AD7150_STATUS_OUT2         BIT(5)
#define AD7150_CH1_DATA_HIGH       1
#define AD7150_CH2_DATA_HIGH       3
#define AD7150_CH1_AVG_HIGH        5
#define AD7150_CH2_AVG_HIGH        7
#define AD7150_CH1_SENSITIVITY     9
#define AD7150_CH1_THR_HOLD_H      9
#define AD7150_CH1_TIMEOUT         10
#define AD7150_CH1_SETUP           11
#define AD7150_CH2_SENSITIVITY     12
#define AD7150_CH2_THR_HOLD_H      12
#define AD7150_CH2_TIMEOUT         13
#define AD7150_CH2_SETUP           14
#define AD7150_CFG                 15
#define AD7150_CFG_FIX             BIT(7)
#define AD7150_PD_TIMER            16
#define AD7150_CH1_CAPDAC          17
#define AD7150_CH2_CAPDAC          18
#define AD7150_SN3                 19
#define AD7150_SN2                 20
#define AD7150_SN1                 21
#define AD7150_SN0                 22
#define AD7150_ID                  23

/* AD7150 masks */
#define AD7150_THRESHTYPE_MSK			GENMASK(6, 5)

#define AD7150_CH_TIMEOUT_RECEDING		GENMASK(3, 0)
#define AD7150_CH_TIMEOUT_APPROACHING		GENMASK(7, 4)

enum {
	AD7150,
	AD7151,
};

/**
 * struct ad7150_chip_info - instance specific chip data
 * @client: i2c client for this device
 * @current_event: device always has one type of event enabled.
 *	This element stores the event code of the current one.
 * @threshold: thresholds for simple capacitance value events
 * @thresh_sensitivity: threshold for simple capacitance offset
 *	from 'average' value.
 * @thresh_timeout: a timeout, in samples from the moment an
 *	adaptive threshold event occurs to when the average
 *	value jumps to current value.  Note made up of two fields,
 *      3:0 are for timeout receding - applies if below lower threshold
 *      7:4 are for timeout approaching - applies if above upper threshold
 * @old_state: store state from previous event, allowing confirmation
 *	of new condition.
 * @conversion_mode: the current conversion mode.
 * @state_lock: ensure consistent state of this structure wrt the
 *	hardware.
 */
struct ad7150_chip_info {
	struct i2c_client *client;
	u64 current_event;
	u16 threshold[2][2];
	u8 thresh_sensitivity[2][2];
	u8 thresh_timeout[2][2];
	int old_state;
	char *conversion_mode;
	struct mutex state_lock;
};

/*
 * sysfs nodes
 */

static const u8 ad7150_addresses[][6] = {
	{ AD7150_CH1_DATA_HIGH, AD7150_CH1_AVG_HIGH,
	  AD7150_CH1_SETUP, AD7150_CH1_THR_HOLD_H,
	  AD7150_CH1_SENSITIVITY, AD7150_CH1_TIMEOUT },
	{ AD7150_CH2_DATA_HIGH, AD7150_CH2_AVG_HIGH,
	  AD7150_CH2_SETUP, AD7150_CH2_THR_HOLD_H,
	  AD7150_CH2_SENSITIVITY, AD7150_CH2_TIMEOUT },
};

static int ad7150_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long mask)
{
	int ret;
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int channel = chan->channel;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_word_swapped(chip->client,
						  ad7150_addresses[channel][0]);
		if (ret < 0)
			return ret;
		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_AVERAGE_RAW:
		ret = i2c_smbus_read_word_swapped(chip->client,
						  ad7150_addresses[channel][1]);
		if (ret < 0)
			return ret;
		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		/* Strangely same for both 1 and 2 chan parts */
		*val = 100;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7150_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	int ret;
	u8 threshtype;
	bool thrfixed;
	struct ad7150_chip_info *chip = iio_priv(indio_dev);

	ret = i2c_smbus_read_byte_data(chip->client, AD7150_CFG);
	if (ret < 0)
		return ret;

	threshtype = FIELD_GET(AD7150_THRESHTYPE_MSK, ret);

	/*check if threshold mode is fixed or adaptive*/
	thrfixed = FIELD_GET(AD7150_CFG_FIX, ret);

	switch (type) {
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		if (dir == IIO_EV_DIR_RISING)
			return !thrfixed && (threshtype == 0x3);
		return !thrfixed && (threshtype == 0x2);
	case IIO_EV_TYPE_THRESH:
		if (dir == IIO_EV_DIR_RISING)
			return thrfixed && (threshtype == 0x1);
		return thrfixed && (threshtype == 0x0);
	default:
		break;
	}
	return -EINVAL;
}

/* state_lock should be held to ensure consistent state*/

static int ad7150_write_event_params(struct iio_dev *indio_dev,
				     unsigned int chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int rising = (dir == IIO_EV_DIR_RISING);
	u64 event_code;

	event_code = IIO_UNMOD_EVENT_CODE(IIO_CAPACITANCE, chan, type, dir);

	if (event_code != chip->current_event)
		return 0;

	switch (type) {
		/* Note completely different from the adaptive versions */
	case IIO_EV_TYPE_THRESH: {
		u16 value = chip->threshold[rising][chan];
		return i2c_smbus_write_word_swapped(chip->client,
						    ad7150_addresses[chan][3],
						    value);
	}
	case IIO_EV_TYPE_THRESH_ADAPTIVE: {
		int ret;
		u8 sens, timeout;

		sens = chip->thresh_sensitivity[rising][chan];
		ret = i2c_smbus_write_byte_data(chip->client,
						ad7150_addresses[chan][4],
						sens);
		if (ret)
			return ret;

		/*
		 * Single timeout register contains timeouts for both
		 * directions.
		 */
		timeout = FIELD_PREP(AD7150_CH_TIMEOUT_APPROACHING,
				     chip->thresh_timeout[1][chan]);
		timeout |= FIELD_PREP(AD7150_CH_TIMEOUT_RECEDING,
				      chip->thresh_timeout[0][chan]);
		return i2c_smbus_write_byte_data(chip->client,
						 ad7150_addresses[chan][5],
						 timeout);
	}
	default:
		return -EINVAL;
	}
}

static int ad7150_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, int state)
{
	u8 thresh_type, cfg, adaptive;
	int ret;
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int rising = (dir == IIO_EV_DIR_RISING);
	u64 event_code;

	/* Something must always be turned on */
	if (!state)
		return -EINVAL;

	event_code = IIO_UNMOD_EVENT_CODE(chan->type, chan->channel, type, dir);
	if (event_code == chip->current_event)
		return 0;
	mutex_lock(&chip->state_lock);
	ret = i2c_smbus_read_byte_data(chip->client, AD7150_CFG);
	if (ret < 0)
		goto error_ret;

	cfg = ret & ~((0x03 << 5) | BIT(7));

	switch (type) {
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		adaptive = 1;
		if (rising)
			thresh_type = 0x3;
		else
			thresh_type = 0x2;
		break;
	case IIO_EV_TYPE_THRESH:
		adaptive = 0;
		if (rising)
			thresh_type = 0x1;
		else
			thresh_type = 0x0;
		break;
	default:
		ret = -EINVAL;
		goto error_ret;
	}

	cfg |= (!adaptive << 7) | (thresh_type << 5);

	ret = i2c_smbus_write_byte_data(chip->client, AD7150_CFG, cfg);
	if (ret < 0)
		goto error_ret;

	chip->current_event = event_code;

	/* update control attributes */
	ret = ad7150_write_event_params(indio_dev, chan->channel, type, dir);
error_ret:
	mutex_unlock(&chip->state_lock);

	return ret;
}

static int ad7150_read_event_value(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int rising = (dir == IIO_EV_DIR_RISING);

	/* Complex register sharing going on here */
	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (type) {
		case IIO_EV_TYPE_THRESH_ADAPTIVE:
			*val = chip->thresh_sensitivity[rising][chan->channel];
			return IIO_VAL_INT;
		case IIO_EV_TYPE_THRESH:
			*val = chip->threshold[rising][chan->channel];
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_TIMEOUT:
		*val = 0;
		*val2 = chip->thresh_timeout[rising][chan->channel] * 10000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ad7150_write_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	int ret;
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int rising = (dir == IIO_EV_DIR_RISING);

	mutex_lock(&chip->state_lock);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (type) {
		case IIO_EV_TYPE_THRESH_ADAPTIVE:
			chip->thresh_sensitivity[rising][chan->channel] = val;
			break;
		case IIO_EV_TYPE_THRESH:
			chip->threshold[rising][chan->channel] = val;
			break;
		default:
			ret = -EINVAL;
			goto error_ret;
		}
		break;
	case IIO_EV_INFO_TIMEOUT: {
		/*
		 * Raw timeout is in cycles of 10 msecs as long as both
		 * channels are enabled.
		 * In terms of INT_PLUS_MICRO, that is in units of 10,000
		 */
		int timeout = val2 / 10000;

		if (val != 0 || timeout < 0 || timeout > 15 || val2 % 10000) {
			ret = -EINVAL;
			goto error_ret;
		}

		chip->thresh_timeout[rising][chan->channel] = timeout;
		break;
	}
	default:
		ret = -EINVAL;
		goto error_ret;
	}

	/* write back if active */
	ret = ad7150_write_event_params(indio_dev, chan->channel, type, dir);

error_ret:
	mutex_unlock(&chip->state_lock);
	return ret;
}

static const struct iio_event_spec ad7150_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH_ADAPTIVE,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE) |
			BIT(IIO_EV_INFO_TIMEOUT),
	}, {
		.type = IIO_EV_TYPE_THRESH_ADAPTIVE,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE) |
			BIT(IIO_EV_INFO_TIMEOUT),
	},
};

#define AD7150_CAPACITANCE_CHAN(_chan)	{			\
		.type = IIO_CAPACITANCE,			\
		.indexed = 1,					\
		.channel = _chan,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
		BIT(IIO_CHAN_INFO_AVERAGE_RAW),			\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.event_spec = ad7150_events,			\
		.num_event_specs = ARRAY_SIZE(ad7150_events),	\
	}

#define AD7150_CAPACITANCE_CHAN_NO_IRQ(_chan)	{		\
		.type = IIO_CAPACITANCE,			\
		.indexed = 1,					\
		.channel = _chan,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
		BIT(IIO_CHAN_INFO_AVERAGE_RAW),			\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
	}

static const struct iio_chan_spec ad7150_channels[] = {
	AD7150_CAPACITANCE_CHAN(0),
	AD7150_CAPACITANCE_CHAN(1),
};

static const struct iio_chan_spec ad7150_channels_no_irq[] = {
	AD7150_CAPACITANCE_CHAN_NO_IRQ(0),
	AD7150_CAPACITANCE_CHAN_NO_IRQ(1),
};

static const struct iio_chan_spec ad7151_channels[] = {
	AD7150_CAPACITANCE_CHAN(0),
};

static const struct iio_chan_spec ad7151_channels_no_irq[] = {
	AD7150_CAPACITANCE_CHAN_NO_IRQ(0),
};

static irqreturn_t ad7150_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	u8 int_status;
	s64 timestamp = iio_get_time_ns(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, AD7150_STATUS);
	if (ret < 0)
		return IRQ_HANDLED;

	int_status = ret;

	if ((int_status & AD7150_STATUS_OUT1) &&
	    !(chip->old_state & AD7150_STATUS_OUT1))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_CAPACITANCE,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
				timestamp);
	else if ((!(int_status & AD7150_STATUS_OUT1)) &&
		 (chip->old_state & AD7150_STATUS_OUT1))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_CAPACITANCE,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);

	if ((int_status & AD7150_STATUS_OUT2) &&
	    !(chip->old_state & AD7150_STATUS_OUT2))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_CAPACITANCE,
						    1,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);
	else if ((!(int_status & AD7150_STATUS_OUT2)) &&
		 (chip->old_state & AD7150_STATUS_OUT2))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_CAPACITANCE,
						    1,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);
	/* store the status to avoid repushing same events */
	chip->old_state = int_status;

	return IRQ_HANDLED;
}

static IIO_CONST_ATTR(in_capacitance_thresh_adaptive_timeout_available,
		      "[0 0.01 0.15]");

static struct attribute *ad7150_event_attributes[] = {
	&iio_const_attr_in_capacitance_thresh_adaptive_timeout_available
	.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7150_event_attribute_group = {
	.attrs = ad7150_event_attributes,
	.name = "events",
};

static const struct iio_info ad7150_info = {
	.event_attrs = &ad7150_event_attribute_group,
	.read_raw = &ad7150_read_raw,
	.read_event_config = &ad7150_read_event_config,
	.write_event_config = &ad7150_write_event_config,
	.read_event_value = &ad7150_read_event_value,
	.write_event_value = &ad7150_write_event_value,
};

static const struct iio_info ad7150_info_no_irq = {
	.read_raw = &ad7150_read_raw,
};

static int ad7150_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct ad7150_chip_info *chip;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	mutex_init(&chip->state_lock);
	chip->client = client;

	indio_dev->name = id->name;

	indio_dev->modes = INDIO_DIRECT_MODE;

	if (client->irq) {
		indio_dev->info = &ad7150_info;
		switch (id->driver_data) {
		case AD7150:
			indio_dev->channels = ad7150_channels;
			indio_dev->num_channels = ARRAY_SIZE(ad7150_channels);
			break;
		case AD7151:
			indio_dev->channels = ad7151_channels;
			indio_dev->num_channels = ARRAY_SIZE(ad7151_channels);
			break;
		default:
			return -EINVAL;
		}

		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL,
						&ad7150_event_handler,
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT,
						"ad7150_irq1",
						indio_dev);
		if (ret)
			return ret;
	} else {
		indio_dev->info = &ad7150_info_no_irq;
		switch (id->driver_data) {
		case AD7150:
			indio_dev->channels = ad7150_channels_no_irq;
			indio_dev->num_channels = ARRAY_SIZE(ad7150_channels_no_irq);
			break;
		case AD7151:
			indio_dev->channels = ad7151_channels_no_irq;
			indio_dev->num_channels = ARRAY_SIZE(ad7151_channels_no_irq);
			break;
		default:
			return -EINVAL;
		}
	}

	return devm_iio_device_register(indio_dev->dev.parent, indio_dev);
}

static const struct i2c_device_id ad7150_id[] = {
	{ "ad7150", AD7150 },
	{ "ad7151", AD7151 },
	{ "ad7156", AD7150 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad7150_id);

static struct i2c_driver ad7150_driver = {
	.driver = {
		.name = "ad7150",
	},
	.probe = ad7150_probe,
	.id_table = ad7150_id,
};
module_i2c_driver(ad7150_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD7150/1/6 capacitive sensor driver");
MODULE_LICENSE("GPL v2");
