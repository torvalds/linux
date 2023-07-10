// SPDX-License-Identifier: GPL-2.0+
/*
 * AD7150 capacitive sensor driver supporting AD7150/1/6
 *
 * Copyright 2010-2011 Analog Devices Inc.
 * Copyright 2021 Jonathan Cameron <Jonathan.Cameron@huawei.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

#define AD7150_STATUS_REG		0
#define   AD7150_STATUS_OUT1		BIT(3)
#define   AD7150_STATUS_OUT2		BIT(5)
#define AD7150_CH1_DATA_HIGH_REG	1
#define AD7150_CH2_DATA_HIGH_REG	3
#define AD7150_CH1_AVG_HIGH_REG		5
#define AD7150_CH2_AVG_HIGH_REG		7
#define AD7150_CH1_SENSITIVITY_REG	9
#define AD7150_CH1_THR_HOLD_H_REG	9
#define AD7150_CH1_TIMEOUT_REG		10
#define   AD7150_CH_TIMEOUT_RECEDING	GENMASK(3, 0)
#define   AD7150_CH_TIMEOUT_APPROACHING	GENMASK(7, 4)
#define AD7150_CH1_SETUP_REG		11
#define AD7150_CH2_SENSITIVITY_REG	12
#define AD7150_CH2_THR_HOLD_H_REG	12
#define AD7150_CH2_TIMEOUT_REG		13
#define AD7150_CH2_SETUP_REG		14
#define AD7150_CFG_REG			15
#define   AD7150_CFG_FIX		BIT(7)
#define   AD7150_CFG_THRESHTYPE_MSK	GENMASK(6, 5)
#define   AD7150_CFG_TT_NEG		0x0
#define   AD7150_CFG_TT_POS		0x1
#define   AD7150_CFG_TT_IN_WINDOW	0x2
#define   AD7150_CFG_TT_OUT_WINDOW	0x3
#define AD7150_PD_TIMER_REG		16
#define AD7150_CH1_CAPDAC_REG		17
#define AD7150_CH2_CAPDAC_REG		18
#define AD7150_SN3_REG			19
#define AD7150_SN2_REG			20
#define AD7150_SN1_REG			21
#define AD7150_SN0_REG			22
#define AD7150_ID_REG			23

enum {
	AD7150,
	AD7151,
};

/**
 * struct ad7150_chip_info - instance specific chip data
 * @client: i2c client for this device
 * @threshold: thresholds for simple capacitance value events
 * @thresh_sensitivity: threshold for simple capacitance offset
 *	from 'average' value.
 * @thresh_timeout: a timeout, in samples from the moment an
 *	adaptive threshold event occurs to when the average
 *	value jumps to current value.  Note made up of two fields,
 *      3:0 are for timeout receding - applies if below lower threshold
 *      7:4 are for timeout approaching - applies if above upper threshold
 * @state_lock: ensure consistent state of this structure wrt the
 *	hardware.
 * @interrupts: one or two interrupt numbers depending on device type.
 * @int_enabled: is a given interrupt currently enabled.
 * @type: threshold type
 * @dir: threshold direction
 */
struct ad7150_chip_info {
	struct i2c_client *client;
	u16 threshold[2][2];
	u8 thresh_sensitivity[2][2];
	u8 thresh_timeout[2][2];
	struct mutex state_lock;
	int interrupts[2];
	bool int_enabled[2];
	enum iio_event_type type;
	enum iio_event_direction dir;
};

static const u8 ad7150_addresses[][6] = {
	{ AD7150_CH1_DATA_HIGH_REG, AD7150_CH1_AVG_HIGH_REG,
	  AD7150_CH1_SETUP_REG, AD7150_CH1_THR_HOLD_H_REG,
	  AD7150_CH1_SENSITIVITY_REG, AD7150_CH1_TIMEOUT_REG },
	{ AD7150_CH2_DATA_HIGH_REG, AD7150_CH2_AVG_HIGH_REG,
	  AD7150_CH2_SETUP_REG, AD7150_CH2_THR_HOLD_H_REG,
	  AD7150_CH2_SENSITIVITY_REG, AD7150_CH2_TIMEOUT_REG },
};

static int ad7150_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long mask)
{
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int channel = chan->channel;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_word_swapped(chip->client,
						  ad7150_addresses[channel][0]);
		if (ret < 0)
			return ret;
		*val = ret >> 4;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_AVERAGE_RAW:
		ret = i2c_smbus_read_word_swapped(chip->client,
						  ad7150_addresses[channel][1]);
		if (ret < 0)
			return ret;
		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * Base units for capacitance are nano farads and the value
		 * calculated from the datasheet formula is in picofarad
		 * so multiply by 1000
		 */
		*val = 1000;
		*val2 = 40944 >> 4; /* To match shift in _RAW */
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_OFFSET:
		*val = -(12288 >> 4); /* To match shift in _RAW */
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
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	u8 threshtype;
	bool thrfixed;
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, AD7150_CFG_REG);
	if (ret < 0)
		return ret;

	threshtype = FIELD_GET(AD7150_CFG_THRESHTYPE_MSK, ret);

	/*check if threshold mode is fixed or adaptive*/
	thrfixed = FIELD_GET(AD7150_CFG_FIX, ret);

	switch (type) {
	case IIO_EV_TYPE_THRESH_ADAPTIVE:
		if (dir == IIO_EV_DIR_RISING)
			return !thrfixed && (threshtype == AD7150_CFG_TT_POS);
		return !thrfixed && (threshtype == AD7150_CFG_TT_NEG);
	case IIO_EV_TYPE_THRESH:
		if (dir == IIO_EV_DIR_RISING)
			return thrfixed && (threshtype == AD7150_CFG_TT_POS);
		return thrfixed && (threshtype == AD7150_CFG_TT_NEG);
	default:
		break;
	}
	return -EINVAL;
}

/* state_lock should be held to ensure consistent state */
static int ad7150_write_event_params(struct iio_dev *indio_dev,
				     unsigned int chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int rising = (dir == IIO_EV_DIR_RISING);

	/* Only update value live, if parameter is in use */
	if ((type != chip->type) || (dir != chip->dir))
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
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	int ret = 0;

	/*
	 * There is only a single shared control and no on chip
	 * interrupt disables for the two interrupt lines.
	 * So, enabling will switch the events configured to enable
	 * whatever was most recently requested and if necessary enable_irq()
	 * the interrupt and any disable will disable_irq() for that
	 * channels interrupt.
	 */
	if (!state) {
		if ((chip->int_enabled[chan->channel]) &&
		    (type == chip->type) && (dir == chip->dir)) {
			disable_irq(chip->interrupts[chan->channel]);
			chip->int_enabled[chan->channel] = false;
		}
		return 0;
	}

	mutex_lock(&chip->state_lock);
	if ((type != chip->type) || (dir != chip->dir)) {
		int rising = (dir == IIO_EV_DIR_RISING);
		u8 thresh_type, cfg, fixed;

		/*
		 * Need to temporarily disable both interrupts if
		 * enabled - this is to avoid races around changing
		 * config and thresholds.
		 * Note enable/disable_irq() are reference counted so
		 * no need to check if already enabled.
		 */
		disable_irq(chip->interrupts[0]);
		disable_irq(chip->interrupts[1]);

		ret = i2c_smbus_read_byte_data(chip->client, AD7150_CFG_REG);
		if (ret < 0)
			goto error_ret;

		cfg = ret & ~(AD7150_CFG_THRESHTYPE_MSK | AD7150_CFG_FIX);

		if (type == IIO_EV_TYPE_THRESH_ADAPTIVE)
			fixed = 0;
		else
			fixed = 1;

		if (rising)
			thresh_type = AD7150_CFG_TT_POS;
		else
			thresh_type = AD7150_CFG_TT_NEG;

		cfg |= FIELD_PREP(AD7150_CFG_FIX, fixed) |
			FIELD_PREP(AD7150_CFG_THRESHTYPE_MSK, thresh_type);

		ret = i2c_smbus_write_byte_data(chip->client, AD7150_CFG_REG,
						cfg);
		if (ret < 0)
			goto error_ret;

		/*
		 * There is a potential race condition here, but not easy
		 * to close given we can't disable the interrupt at the
		 * chip side of things. Rely on the status bit.
		 */
		chip->type = type;
		chip->dir = dir;

		/* update control attributes */
		ret = ad7150_write_event_params(indio_dev, chan->channel, type,
						dir);
		if (ret)
			goto error_ret;
		/* reenable any irq's we disabled whilst changing mode */
		enable_irq(chip->interrupts[0]);
		enable_irq(chip->interrupts[1]);
	}
	if (!chip->int_enabled[chan->channel]) {
		enable_irq(chip->interrupts[chan->channel]);
		chip->int_enabled[chan->channel] = true;
	}

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
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
			BIT(IIO_CHAN_INFO_OFFSET),		\
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
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
			BIT(IIO_CHAN_INFO_OFFSET),		\
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

static irqreturn_t __ad7150_event_handler(void *private, u8 status_mask,
					  int channel)
{
	struct iio_dev *indio_dev = private;
	struct ad7150_chip_info *chip = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	int int_status;

	int_status = i2c_smbus_read_byte_data(chip->client, AD7150_STATUS_REG);
	if (int_status < 0)
		return IRQ_HANDLED;

	if (!(int_status & status_mask))
		return IRQ_HANDLED;

	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_CAPACITANCE, channel,
					    chip->type, chip->dir),
		       timestamp);

	return IRQ_HANDLED;
}

static irqreturn_t ad7150_event_handler_ch1(int irq, void *private)
{
	return __ad7150_event_handler(private, AD7150_STATUS_OUT1, 0);
}

static irqreturn_t ad7150_event_handler_ch2(int irq, void *private)
{
	return __ad7150_event_handler(private, AD7150_STATUS_OUT2, 1);
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

static int ad7150_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct ad7150_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);
	mutex_init(&chip->state_lock);
	chip->client = client;

	indio_dev->name = id->name;

	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_regulator_get_enable(&client->dev, "vdd");
	if (ret)
		return ret;

	chip->interrupts[0] = fwnode_irq_get(dev_fwnode(&client->dev), 0);
	if (chip->interrupts[0] < 0)
		return chip->interrupts[0];
	if (id->driver_data == AD7150) {
		chip->interrupts[1] = fwnode_irq_get(dev_fwnode(&client->dev), 1);
		if (chip->interrupts[1] < 0)
			return chip->interrupts[1];
	}
	if (chip->interrupts[0] &&
	    (id->driver_data == AD7151 || chip->interrupts[1])) {
		irq_set_status_flags(chip->interrupts[0], IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(&client->dev,
						chip->interrupts[0],
						NULL,
						&ad7150_event_handler_ch1,
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT,
						"ad7150_irq1",
						indio_dev);
		if (ret)
			return ret;

		indio_dev->info = &ad7150_info;
		switch (id->driver_data) {
		case AD7150:
			indio_dev->channels = ad7150_channels;
			indio_dev->num_channels = ARRAY_SIZE(ad7150_channels);
			irq_set_status_flags(chip->interrupts[1], IRQ_NOAUTOEN);
			ret = devm_request_threaded_irq(&client->dev,
							chip->interrupts[1],
							NULL,
							&ad7150_event_handler_ch2,
							IRQF_TRIGGER_RISING |
							IRQF_ONESHOT,
							"ad7150_irq2",
							indio_dev);
			if (ret)
				return ret;
			break;
		case AD7151:
			indio_dev->channels = ad7151_channels;
			indio_dev->num_channels = ARRAY_SIZE(ad7151_channels);
			break;
		default:
			return -EINVAL;
		}

	} else {
		indio_dev->info = &ad7150_info_no_irq;
		switch (id->driver_data) {
		case AD7150:
			indio_dev->channels = ad7150_channels_no_irq;
			indio_dev->num_channels =
				ARRAY_SIZE(ad7150_channels_no_irq);
			break;
		case AD7151:
			indio_dev->channels = ad7151_channels_no_irq;
			indio_dev->num_channels =
				ARRAY_SIZE(ad7151_channels_no_irq);
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

static const struct of_device_id ad7150_of_match[] = {
	{ "adi,ad7150" },
	{ "adi,ad7151" },
	{ "adi,ad7156" },
	{}
};
static struct i2c_driver ad7150_driver = {
	.driver = {
		.name = "ad7150",
		.of_match_table = ad7150_of_match,
	},
	.probe = ad7150_probe,
	.id_table = ad7150_id,
};
module_i2c_driver(ad7150_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD7150/1/6 capacitive sensor driver");
MODULE_LICENSE("GPL v2");
