// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/iio/light/tsl2563.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Written by Timo O. Karjalainen <timo.o.karjalainen@nokia.com>
 * Contact: Amit Kucheria <amit.kucheria@verdurent.com>
 *
 * Converted to IIO driver
 * Amit Kucheria <amit.kucheria@verdurent.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/platform_data/tsl2563.h>

/* Use this many bits for fraction part. */
#define ADC_FRAC_BITS		14

/* Given number of 1/10000's in ADC_FRAC_BITS precision. */
#define FRAC10K(f)		(((f) * (1L << (ADC_FRAC_BITS))) / (10000))

/* Bits used for fraction in calibration coefficients.*/
#define CALIB_FRAC_BITS		10
/* 0.5 in CALIB_FRAC_BITS precision */
#define CALIB_FRAC_HALF		(1 << (CALIB_FRAC_BITS - 1))
/* Make a fraction from a number n that was multiplied with b. */
#define CALIB_FRAC(n, b)	(((n) << CALIB_FRAC_BITS) / (b))
/* Decimal 10^(digits in sysfs presentation) */
#define CALIB_BASE_SYSFS	1000

#define TSL2563_CMD		0x80
#define TSL2563_CLEARINT	0x40

#define TSL2563_REG_CTRL	0x00
#define TSL2563_REG_TIMING	0x01
#define TSL2563_REG_LOWLOW	0x02 /* data0 low threshold, 2 bytes */
#define TSL2563_REG_LOWHIGH	0x03
#define TSL2563_REG_HIGHLOW	0x04 /* data0 high threshold, 2 bytes */
#define TSL2563_REG_HIGHHIGH	0x05
#define TSL2563_REG_INT		0x06
#define TSL2563_REG_ID		0x0a
#define TSL2563_REG_DATA0LOW	0x0c /* broadband sensor value, 2 bytes */
#define TSL2563_REG_DATA0HIGH	0x0d
#define TSL2563_REG_DATA1LOW	0x0e /* infrared sensor value, 2 bytes */
#define TSL2563_REG_DATA1HIGH	0x0f

#define TSL2563_CMD_POWER_ON	0x03
#define TSL2563_CMD_POWER_OFF	0x00
#define TSL2563_CTRL_POWER_MASK	0x03

#define TSL2563_TIMING_13MS	0x00
#define TSL2563_TIMING_100MS	0x01
#define TSL2563_TIMING_400MS	0x02
#define TSL2563_TIMING_MASK	0x03
#define TSL2563_TIMING_GAIN16	0x10
#define TSL2563_TIMING_GAIN1	0x00

#define TSL2563_INT_DISABLED	0x00
#define TSL2563_INT_LEVEL	0x10
#define TSL2563_INT_PERSIST(n)	((n) & 0x0F)

struct tsl2563_gainlevel_coeff {
	u8 gaintime;
	u16 min;
	u16 max;
};

static const struct tsl2563_gainlevel_coeff tsl2563_gainlevel_table[] = {
	{
		.gaintime	= TSL2563_TIMING_400MS | TSL2563_TIMING_GAIN16,
		.min		= 0,
		.max		= 65534,
	}, {
		.gaintime	= TSL2563_TIMING_400MS | TSL2563_TIMING_GAIN1,
		.min		= 2048,
		.max		= 65534,
	}, {
		.gaintime	= TSL2563_TIMING_100MS | TSL2563_TIMING_GAIN1,
		.min		= 4095,
		.max		= 37177,
	}, {
		.gaintime	= TSL2563_TIMING_13MS | TSL2563_TIMING_GAIN1,
		.min		= 3000,
		.max		= 65535,
	},
};

struct tsl2563_chip {
	struct mutex		lock;
	struct i2c_client	*client;
	struct delayed_work	poweroff_work;

	/* Remember state for suspend and resume functions */
	bool suspended;

	struct tsl2563_gainlevel_coeff const *gainlevel;

	u16			low_thres;
	u16			high_thres;
	u8			intr;
	bool			int_enabled;

	/* Calibration coefficients */
	u32			calib0;
	u32			calib1;
	int			cover_comp_gain;

	/* Cache current values, to be returned while suspended */
	u32			data0;
	u32			data1;
};

static int tsl2563_set_power(struct tsl2563_chip *chip, int on)
{
	struct i2c_client *client = chip->client;
	u8 cmd;

	cmd = on ? TSL2563_CMD_POWER_ON : TSL2563_CMD_POWER_OFF;
	return i2c_smbus_write_byte_data(client,
					 TSL2563_CMD | TSL2563_REG_CTRL, cmd);
}

/*
 * Return value is 0 for off, 1 for on, or a negative error
 * code if reading failed.
 */
static int tsl2563_get_power(struct tsl2563_chip *chip)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, TSL2563_CMD | TSL2563_REG_CTRL);
	if (ret < 0)
		return ret;

	return (ret & TSL2563_CTRL_POWER_MASK) == TSL2563_CMD_POWER_ON;
}

static int tsl2563_configure(struct tsl2563_chip *chip)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client,
			TSL2563_CMD | TSL2563_REG_TIMING,
			chip->gainlevel->gaintime);
	if (ret)
		goto error_ret;
	ret = i2c_smbus_write_byte_data(chip->client,
			TSL2563_CMD | TSL2563_REG_HIGHLOW,
			chip->high_thres & 0xFF);
	if (ret)
		goto error_ret;
	ret = i2c_smbus_write_byte_data(chip->client,
			TSL2563_CMD | TSL2563_REG_HIGHHIGH,
			(chip->high_thres >> 8) & 0xFF);
	if (ret)
		goto error_ret;
	ret = i2c_smbus_write_byte_data(chip->client,
			TSL2563_CMD | TSL2563_REG_LOWLOW,
			chip->low_thres & 0xFF);
	if (ret)
		goto error_ret;
	ret = i2c_smbus_write_byte_data(chip->client,
			TSL2563_CMD | TSL2563_REG_LOWHIGH,
			(chip->low_thres >> 8) & 0xFF);
/*
 * Interrupt register is automatically written anyway if it is relevant
 * so is not here.
 */
error_ret:
	return ret;
}

static void tsl2563_poweroff_work(struct work_struct *work)
{
	struct tsl2563_chip *chip =
		container_of(work, struct tsl2563_chip, poweroff_work.work);
	tsl2563_set_power(chip, 0);
}

static int tsl2563_detect(struct tsl2563_chip *chip)
{
	int ret;

	ret = tsl2563_set_power(chip, 1);
	if (ret)
		return ret;

	ret = tsl2563_get_power(chip);
	if (ret < 0)
		return ret;

	return ret ? 0 : -ENODEV;
}

static int tsl2563_read_id(struct tsl2563_chip *chip, u8 *id)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, TSL2563_CMD | TSL2563_REG_ID);
	if (ret < 0)
		return ret;

	*id = ret;

	return 0;
}

/*
 * "Normalized" ADC value is one obtained with 400ms of integration time and
 * 16x gain. This function returns the number of bits of shift needed to
 * convert between normalized values and HW values obtained using given
 * timing and gain settings.
 */
static int tsl2563_adc_shiftbits(u8 timing)
{
	int shift = 0;

	switch (timing & TSL2563_TIMING_MASK) {
	case TSL2563_TIMING_13MS:
		shift += 5;
		break;
	case TSL2563_TIMING_100MS:
		shift += 2;
		break;
	case TSL2563_TIMING_400MS:
		/* no-op */
		break;
	}

	if (!(timing & TSL2563_TIMING_GAIN16))
		shift += 4;

	return shift;
}

/* Convert a HW ADC value to normalized scale. */
static u32 tsl2563_normalize_adc(u16 adc, u8 timing)
{
	return adc << tsl2563_adc_shiftbits(timing);
}

static void tsl2563_wait_adc(struct tsl2563_chip *chip)
{
	unsigned int delay;

	switch (chip->gainlevel->gaintime & TSL2563_TIMING_MASK) {
	case TSL2563_TIMING_13MS:
		delay = 14;
		break;
	case TSL2563_TIMING_100MS:
		delay = 101;
		break;
	default:
		delay = 402;
	}
	/*
	 * TODO: Make sure that we wait at least required delay but why we
	 * have to extend it one tick more?
	 */
	schedule_timeout_interruptible(msecs_to_jiffies(delay) + 2);
}

static int tsl2563_adjust_gainlevel(struct tsl2563_chip *chip, u16 adc)
{
	struct i2c_client *client = chip->client;

	if (adc > chip->gainlevel->max || adc < chip->gainlevel->min) {

		(adc > chip->gainlevel->max) ?
			chip->gainlevel++ : chip->gainlevel--;

		i2c_smbus_write_byte_data(client,
					  TSL2563_CMD | TSL2563_REG_TIMING,
					  chip->gainlevel->gaintime);

		tsl2563_wait_adc(chip);
		tsl2563_wait_adc(chip);

		return 1;
	} else
		return 0;
}

static int tsl2563_get_adc(struct tsl2563_chip *chip)
{
	struct i2c_client *client = chip->client;
	u16 adc0, adc1;
	int retry = 1;
	int ret = 0;

	if (chip->suspended)
		goto out;

	if (!chip->int_enabled) {
		cancel_delayed_work_sync(&chip->poweroff_work);

		if (!tsl2563_get_power(chip)) {
			ret = tsl2563_set_power(chip, 1);
			if (ret)
				goto out;
			ret = tsl2563_configure(chip);
			if (ret)
				goto out;
			tsl2563_wait_adc(chip);
		}
	}

	while (retry) {
		ret = i2c_smbus_read_word_data(client,
				TSL2563_CMD | TSL2563_REG_DATA0LOW);
		if (ret < 0)
			goto out;
		adc0 = ret;

		ret = i2c_smbus_read_word_data(client,
				TSL2563_CMD | TSL2563_REG_DATA1LOW);
		if (ret < 0)
			goto out;
		adc1 = ret;

		retry = tsl2563_adjust_gainlevel(chip, adc0);
	}

	chip->data0 = tsl2563_normalize_adc(adc0, chip->gainlevel->gaintime);
	chip->data1 = tsl2563_normalize_adc(adc1, chip->gainlevel->gaintime);

	if (!chip->int_enabled)
		schedule_delayed_work(&chip->poweroff_work, 5 * HZ);

	ret = 0;
out:
	return ret;
}

static inline int tsl2563_calib_to_sysfs(u32 calib)
{
	return (int) (((calib * CALIB_BASE_SYSFS) +
		       CALIB_FRAC_HALF) >> CALIB_FRAC_BITS);
}

static inline u32 tsl2563_calib_from_sysfs(int value)
{
	return (((u32) value) << CALIB_FRAC_BITS) / CALIB_BASE_SYSFS;
}

/*
 * Conversions between lux and ADC values.
 *
 * The basic formula is lux = c0 * adc0 - c1 * adc1, where c0 and c1 are
 * appropriate constants. Different constants are needed for different
 * kinds of light, determined by the ratio adc1/adc0 (basically the ratio
 * of the intensities in infrared and visible wavelengths). lux_table below
 * lists the upper threshold of the adc1/adc0 ratio and the corresponding
 * constants.
 */

struct tsl2563_lux_coeff {
	unsigned long ch_ratio;
	unsigned long ch0_coeff;
	unsigned long ch1_coeff;
};

static const struct tsl2563_lux_coeff lux_table[] = {
	{
		.ch_ratio	= FRAC10K(1300),
		.ch0_coeff	= FRAC10K(315),
		.ch1_coeff	= FRAC10K(262),
	}, {
		.ch_ratio	= FRAC10K(2600),
		.ch0_coeff	= FRAC10K(337),
		.ch1_coeff	= FRAC10K(430),
	}, {
		.ch_ratio	= FRAC10K(3900),
		.ch0_coeff	= FRAC10K(363),
		.ch1_coeff	= FRAC10K(529),
	}, {
		.ch_ratio	= FRAC10K(5200),
		.ch0_coeff	= FRAC10K(392),
		.ch1_coeff	= FRAC10K(605),
	}, {
		.ch_ratio	= FRAC10K(6500),
		.ch0_coeff	= FRAC10K(229),
		.ch1_coeff	= FRAC10K(291),
	}, {
		.ch_ratio	= FRAC10K(8000),
		.ch0_coeff	= FRAC10K(157),
		.ch1_coeff	= FRAC10K(180),
	}, {
		.ch_ratio	= FRAC10K(13000),
		.ch0_coeff	= FRAC10K(34),
		.ch1_coeff	= FRAC10K(26),
	}, {
		.ch_ratio	= ULONG_MAX,
		.ch0_coeff	= 0,
		.ch1_coeff	= 0,
	},
};

/* Convert normalized, scaled ADC values to lux. */
static unsigned int tsl2563_adc_to_lux(u32 adc0, u32 adc1)
{
	const struct tsl2563_lux_coeff *lp = lux_table;
	unsigned long ratio, lux, ch0 = adc0, ch1 = adc1;

	ratio = ch0 ? ((ch1 << ADC_FRAC_BITS) / ch0) : ULONG_MAX;

	while (lp->ch_ratio < ratio)
		lp++;

	lux = ch0 * lp->ch0_coeff - ch1 * lp->ch1_coeff;

	return (unsigned int) (lux >> ADC_FRAC_BITS);
}

/* Apply calibration coefficient to ADC count. */
static u32 tsl2563_calib_adc(u32 adc, u32 calib)
{
	unsigned long scaled = adc;

	scaled *= calib;
	scaled >>= CALIB_FRAC_BITS;

	return (u32) scaled;
}

static int tsl2563_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct tsl2563_chip *chip = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_CALIBSCALE)
		return -EINVAL;
	if (chan->channel2 == IIO_MOD_LIGHT_BOTH)
		chip->calib0 = tsl2563_calib_from_sysfs(val);
	else if (chan->channel2 == IIO_MOD_LIGHT_IR)
		chip->calib1 = tsl2563_calib_from_sysfs(val);
	else
		return -EINVAL;

	return 0;
}

static int tsl2563_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val,
			    int *val2,
			    long mask)
{
	int ret = -EINVAL;
	u32 calib0, calib1;
	struct tsl2563_chip *chip = iio_priv(indio_dev);

	mutex_lock(&chip->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = tsl2563_get_adc(chip);
			if (ret)
				goto error_ret;
			calib0 = tsl2563_calib_adc(chip->data0, chip->calib0) *
				chip->cover_comp_gain;
			calib1 = tsl2563_calib_adc(chip->data1, chip->calib1) *
				chip->cover_comp_gain;
			*val = tsl2563_adc_to_lux(calib0, calib1);
			ret = IIO_VAL_INT;
			break;
		case IIO_INTENSITY:
			ret = tsl2563_get_adc(chip);
			if (ret)
				goto error_ret;
			if (chan->channel2 == IIO_MOD_LIGHT_BOTH)
				*val = chip->data0;
			else
				*val = chip->data1;
			ret = IIO_VAL_INT;
			break;
		default:
			break;
		}
		break;

	case IIO_CHAN_INFO_CALIBSCALE:
		if (chan->channel2 == IIO_MOD_LIGHT_BOTH)
			*val = tsl2563_calib_to_sysfs(chip->calib0);
		else
			*val = tsl2563_calib_to_sysfs(chip->calib1);
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		goto error_ret;
	}

error_ret:
	mutex_unlock(&chip->lock);
	return ret;
}

static const struct iio_event_spec tsl2563_events[] = {
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
	},
};

static const struct iio_chan_spec tsl2563_channels[] = {
	{
		.type = IIO_LIGHT,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.channel = 0,
	}, {
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_BOTH,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE),
		.event_spec = tsl2563_events,
		.num_event_specs = ARRAY_SIZE(tsl2563_events),
	}, {
		.type = IIO_INTENSITY,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_CALIBSCALE),
	}
};

static int tsl2563_read_thresh(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int *val,
	int *val2)
{
	struct tsl2563_chip *chip = iio_priv(indio_dev);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		*val = chip->high_thres;
		break;
	case IIO_EV_DIR_FALLING:
		*val = chip->low_thres;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int tsl2563_write_thresh(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int val,
	int val2)
{
	struct tsl2563_chip *chip = iio_priv(indio_dev);
	int ret;
	u8 address;

	if (dir == IIO_EV_DIR_RISING)
		address = TSL2563_REG_HIGHLOW;
	else
		address = TSL2563_REG_LOWLOW;
	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(chip->client, TSL2563_CMD | address,
					val & 0xFF);
	if (ret)
		goto error_ret;
	ret = i2c_smbus_write_byte_data(chip->client,
					TSL2563_CMD | (address + 1),
					(val >> 8) & 0xFF);
	if (dir == IIO_EV_DIR_RISING)
		chip->high_thres = val;
	else
		chip->low_thres = val;

error_ret:
	mutex_unlock(&chip->lock);

	return ret;
}

static irqreturn_t tsl2563_event_handler(int irq, void *private)
{
	struct iio_dev *dev_info = private;
	struct tsl2563_chip *chip = iio_priv(dev_info);

	iio_push_event(dev_info,
		       IIO_UNMOD_EVENT_CODE(IIO_INTENSITY,
					    0,
					    IIO_EV_TYPE_THRESH,
					    IIO_EV_DIR_EITHER),
		       iio_get_time_ns(dev_info));

	/* clear the interrupt and push the event */
	i2c_smbus_write_byte(chip->client, TSL2563_CMD | TSL2563_CLEARINT);
	return IRQ_HANDLED;
}

static int tsl2563_write_interrupt_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, int state)
{
	struct tsl2563_chip *chip = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&chip->lock);
	if (state && !(chip->intr & 0x30)) {
		chip->intr &= ~0x30;
		chip->intr |= 0x10;
		/* ensure the chip is actually on */
		cancel_delayed_work_sync(&chip->poweroff_work);
		if (!tsl2563_get_power(chip)) {
			ret = tsl2563_set_power(chip, 1);
			if (ret)
				goto out;
			ret = tsl2563_configure(chip);
			if (ret)
				goto out;
		}
		ret = i2c_smbus_write_byte_data(chip->client,
						TSL2563_CMD | TSL2563_REG_INT,
						chip->intr);
		chip->int_enabled = true;
	}

	if (!state && (chip->intr & 0x30)) {
		chip->intr &= ~0x30;
		ret = i2c_smbus_write_byte_data(chip->client,
						TSL2563_CMD | TSL2563_REG_INT,
						chip->intr);
		chip->int_enabled = false;
		/* now the interrupt is not enabled, we can go to sleep */
		schedule_delayed_work(&chip->poweroff_work, 5 * HZ);
	}
out:
	mutex_unlock(&chip->lock);

	return ret;
}

static int tsl2563_read_interrupt_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir)
{
	struct tsl2563_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->lock);
	ret = i2c_smbus_read_byte_data(chip->client,
				       TSL2563_CMD | TSL2563_REG_INT);
	mutex_unlock(&chip->lock);
	if (ret < 0)
		return ret;

	return !!(ret & 0x30);
}

static const struct iio_info tsl2563_info_no_irq = {
	.read_raw = &tsl2563_read_raw,
	.write_raw = &tsl2563_write_raw,
};

static const struct iio_info tsl2563_info = {
	.read_raw = &tsl2563_read_raw,
	.write_raw = &tsl2563_write_raw,
	.read_event_value = &tsl2563_read_thresh,
	.write_event_value = &tsl2563_write_thresh,
	.read_event_config = &tsl2563_read_interrupt_config,
	.write_event_config = &tsl2563_write_interrupt_config,
};

static int tsl2563_probe(struct i2c_client *client,
				const struct i2c_device_id *device_id)
{
	struct iio_dev *indio_dev;
	struct tsl2563_chip *chip;
	struct tsl2563_platform_data *pdata = client->dev.platform_data;
	int err = 0;
	u8 id = 0;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;

	chip = iio_priv(indio_dev);

	i2c_set_clientdata(client, indio_dev);
	chip->client = client;

	err = tsl2563_detect(chip);
	if (err) {
		dev_err(&client->dev, "detect error %d\n", -err);
		return err;
	}

	err = tsl2563_read_id(chip, &id);
	if (err) {
		dev_err(&client->dev, "read id error %d\n", -err);
		return err;
	}

	mutex_init(&chip->lock);

	/* Default values used until userspace says otherwise */
	chip->low_thres = 0x0;
	chip->high_thres = 0xffff;
	chip->gainlevel = tsl2563_gainlevel_table;
	chip->intr = TSL2563_INT_PERSIST(4);
	chip->calib0 = tsl2563_calib_from_sysfs(CALIB_BASE_SYSFS);
	chip->calib1 = tsl2563_calib_from_sysfs(CALIB_BASE_SYSFS);

	if (pdata) {
		chip->cover_comp_gain = pdata->cover_comp_gain;
	} else {
		err = device_property_read_u32(&client->dev, "amstaos,cover-comp-gain",
					       &chip->cover_comp_gain);
		if (err)
			chip->cover_comp_gain = 1;
	}

	dev_info(&client->dev, "model %d, rev. %d\n", id >> 4, id & 0x0f);
	indio_dev->name = client->name;
	indio_dev->channels = tsl2563_channels;
	indio_dev->num_channels = ARRAY_SIZE(tsl2563_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (client->irq)
		indio_dev->info = &tsl2563_info;
	else
		indio_dev->info = &tsl2563_info_no_irq;

	if (client->irq) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
					   NULL,
					   &tsl2563_event_handler,
					   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					   "tsl2563_event",
					   indio_dev);
		if (err) {
			dev_err(&client->dev, "irq request error %d\n", -err);
			return err;
		}
	}

	err = tsl2563_configure(chip);
	if (err) {
		dev_err(&client->dev, "configure error %d\n", -err);
		return err;
	}

	INIT_DELAYED_WORK(&chip->poweroff_work, tsl2563_poweroff_work);

	/* The interrupt cannot yet be enabled so this is fine without lock */
	schedule_delayed_work(&chip->poweroff_work, 5 * HZ);

	err = iio_device_register(indio_dev);
	if (err) {
		dev_err(&client->dev, "iio registration error %d\n", -err);
		goto fail;
	}

	return 0;

fail:
	cancel_delayed_work_sync(&chip->poweroff_work);
	return err;
}

static void tsl2563_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct tsl2563_chip *chip = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	if (!chip->int_enabled)
		cancel_delayed_work_sync(&chip->poweroff_work);
	/* Ensure that interrupts are disabled - then flush any bottom halves */
	chip->intr &= ~0x30;
	i2c_smbus_write_byte_data(chip->client, TSL2563_CMD | TSL2563_REG_INT,
				  chip->intr);
	tsl2563_set_power(chip, 0);
}

static int tsl2563_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct tsl2563_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->lock);

	ret = tsl2563_set_power(chip, 0);
	if (ret)
		goto out;

	chip->suspended = true;

out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int tsl2563_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct tsl2563_chip *chip = iio_priv(indio_dev);
	int ret;

	mutex_lock(&chip->lock);

	ret = tsl2563_set_power(chip, 1);
	if (ret)
		goto out;

	ret = tsl2563_configure(chip);
	if (ret)
		goto out;

	chip->suspended = false;

out:
	mutex_unlock(&chip->lock);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(tsl2563_pm_ops, tsl2563_suspend,
				tsl2563_resume);

static const struct i2c_device_id tsl2563_id[] = {
	{ "tsl2560", 0 },
	{ "tsl2561", 1 },
	{ "tsl2562", 2 },
	{ "tsl2563", 3 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tsl2563_id);

static const struct of_device_id tsl2563_of_match[] = {
	{ .compatible = "amstaos,tsl2560" },
	{ .compatible = "amstaos,tsl2561" },
	{ .compatible = "amstaos,tsl2562" },
	{ .compatible = "amstaos,tsl2563" },
	{}
};
MODULE_DEVICE_TABLE(of, tsl2563_of_match);

static struct i2c_driver tsl2563_i2c_driver = {
	.driver = {
		.name	 = "tsl2563",
		.of_match_table = tsl2563_of_match,
		.pm	= pm_sleep_ptr(&tsl2563_pm_ops),
	},
	.probe		= tsl2563_probe,
	.remove		= tsl2563_remove,
	.id_table	= tsl2563_id,
};
module_i2c_driver(tsl2563_i2c_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("tsl2563 light sensor driver");
MODULE_LICENSE("GPL");
