// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SCD30 carbon dioxide sensor core driver
 *
 * Copyright (c) 2020 Tomasz Duszynski <tomasz.duszynski@octakon.com>
 */
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/types.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#include "scd30.h"

#define SCD30_PRESSURE_COMP_MIN_MBAR 700
#define SCD30_PRESSURE_COMP_MAX_MBAR 1400
#define SCD30_PRESSURE_COMP_DEFAULT 1013
#define SCD30_MEAS_INTERVAL_MIN_S 2
#define SCD30_MEAS_INTERVAL_MAX_S 1800
#define SCD30_MEAS_INTERVAL_DEFAULT SCD30_MEAS_INTERVAL_MIN_S
#define SCD30_FRC_MIN_PPM 400
#define SCD30_FRC_MAX_PPM 2000
#define SCD30_TEMP_OFFSET_MAX 655360
#define SCD30_EXTRA_TIMEOUT_PER_S 250

enum {
	SCD30_CONC,
	SCD30_TEMP,
	SCD30_HR,
};

static int scd30_command_write(struct scd30_state *state, enum scd30_cmd cmd, u16 arg)
{
	return state->command(state, cmd, arg, NULL, 0);
}

static int scd30_command_read(struct scd30_state *state, enum scd30_cmd cmd, u16 *val)
{
	__be16 tmp;
	int ret;

	ret = state->command(state, cmd, 0, &tmp, sizeof(tmp));
	*val = be16_to_cpup(&tmp);

	return ret;
}

static int scd30_reset(struct scd30_state *state)
{
	int ret;
	u16 val;

	ret = scd30_command_write(state, CMD_RESET, 0);
	if (ret)
		return ret;

	/* sensor boots up within 2 secs */
	msleep(2000);
	/*
	 * Power-on-reset causes sensor to produce some glitch on i2c bus and
	 * some controllers end up in error state. Try to recover by placing
	 * any data on the bus.
	 */
	scd30_command_read(state, CMD_MEAS_READY, &val);

	return 0;
}

/* simplified float to fixed point conversion with a scaling factor of 0.01 */
static int scd30_float_to_fp(int float32)
{
	int fraction, shift,
	    mantissa = float32 & GENMASK(22, 0),
	    sign = (float32 & BIT(31)) ? -1 : 1,
	    exp = (float32 & ~BIT(31)) >> 23;

	/* special case 0 */
	if (!exp && !mantissa)
		return 0;

	exp -= 127;
	if (exp < 0) {
		exp = -exp;
		/* return values ranging from 1 to 99 */
		return sign * ((((BIT(23) + mantissa) * 100) >> 23) >> exp);
	}

	/* return values starting at 100 */
	shift = 23 - exp;
	float32 = BIT(exp) + (mantissa >> shift);
	fraction = mantissa & GENMASK(shift - 1, 0);

	return sign * (float32 * 100 + ((fraction * 100) >> shift));
}

static int scd30_read_meas(struct scd30_state *state)
{
	int i, ret;

	ret = state->command(state, CMD_READ_MEAS, 0, state->meas, sizeof(state->meas));
	if (ret)
		return ret;

	be32_to_cpu_array(state->meas, (__be32 *)state->meas, ARRAY_SIZE(state->meas));

	for (i = 0; i < ARRAY_SIZE(state->meas); i++)
		state->meas[i] = scd30_float_to_fp(state->meas[i]);

	/*
	 * co2 is left unprocessed while temperature and humidity are scaled
	 * to milli deg C and milli percent respectively.
	 */
	state->meas[SCD30_TEMP] *= 10;
	state->meas[SCD30_HR] *= 10;

	return 0;
}

static int scd30_wait_meas_irq(struct scd30_state *state)
{
	int ret, timeout;

	reinit_completion(&state->meas_ready);
	enable_irq(state->irq);
	timeout = msecs_to_jiffies(state->meas_interval * (1000 + SCD30_EXTRA_TIMEOUT_PER_S));
	ret = wait_for_completion_interruptible_timeout(&state->meas_ready, timeout);
	if (ret > 0)
		ret = 0;
	else if (!ret)
		ret = -ETIMEDOUT;

	disable_irq(state->irq);

	return ret;
}

static int scd30_wait_meas_poll(struct scd30_state *state)
{
	int timeout = state->meas_interval * SCD30_EXTRA_TIMEOUT_PER_S, tries = 5;

	do {
		int ret;
		u16 val;

		ret = scd30_command_read(state, CMD_MEAS_READY, &val);
		if (ret)
			return -EIO;

		/* new measurement available */
		if (val)
			break;

		msleep_interruptible(timeout);
	} while (--tries);

	return tries ? 0 : -ETIMEDOUT;
}

static int scd30_read_poll(struct scd30_state *state)
{
	int ret;

	ret = scd30_wait_meas_poll(state);
	if (ret)
		return ret;

	return scd30_read_meas(state);
}

static int scd30_read(struct scd30_state *state)
{
	if (state->irq > 0)
		return scd30_wait_meas_irq(state);

	return scd30_read_poll(state);
}

static int scd30_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct scd30_state *state = iio_priv(indio_dev);
	int ret = -EINVAL;
	u16 tmp;

	mutex_lock(&state->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->output) {
			*val = state->pressure_comp;
			ret = IIO_VAL_INT;
			break;
		}

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			break;

		ret = scd30_read(state);
		if (ret) {
			iio_device_release_direct_mode(indio_dev);
			break;
		}

		*val = state->meas[chan->address];
		iio_device_release_direct_mode(indio_dev);
		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 1;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = scd30_command_read(state, CMD_MEAS_INTERVAL, &tmp);
		if (ret)
			break;

		*val = 0;
		*val2 = 1000000000 / tmp;
		ret = IIO_VAL_INT_PLUS_NANO;
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = scd30_command_read(state, CMD_TEMP_OFFSET, &tmp);
		if (ret)
			break;

		*val = tmp;
		ret = IIO_VAL_INT;
		break;
	}
	mutex_unlock(&state->lock);

	return ret;
}

static int scd30_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			   int val, int val2, long mask)
{
	struct scd30_state *state = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&state->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val)
			break;

		val = 1000000000 / val2;
		if (val < SCD30_MEAS_INTERVAL_MIN_S || val > SCD30_MEAS_INTERVAL_MAX_S)
			break;

		ret = scd30_command_write(state, CMD_MEAS_INTERVAL, val);
		if (ret)
			break;

		state->meas_interval = val;
		break;
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_PRESSURE:
			if (val < SCD30_PRESSURE_COMP_MIN_MBAR ||
			    val > SCD30_PRESSURE_COMP_MAX_MBAR)
				break;

			ret = scd30_command_write(state, CMD_START_MEAS, val);
			if (ret)
				break;

			state->pressure_comp = val;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val < 0 || val > SCD30_TEMP_OFFSET_MAX)
			break;
		/*
		 * Manufacturer does not explicitly specify min/max sensible
		 * values hence check is omitted for simplicity.
		 */
		ret = scd30_command_write(state, CMD_TEMP_OFFSET / 10, val);
	}
	mutex_unlock(&state->lock);

	return ret;
}

static int scd30_write_raw_get_fmt(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
				   long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_CALIBBIAS:
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const int scd30_pressure_raw_available[] = {
	SCD30_PRESSURE_COMP_MIN_MBAR, 1, SCD30_PRESSURE_COMP_MAX_MBAR,
};

static const int scd30_temp_calibbias_available[] = {
	0, 10, SCD30_TEMP_OFFSET_MAX,
};

static int scd30_read_avail(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			    const int **vals, int *type, int *length, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*vals = scd30_pressure_raw_available;
		*type = IIO_VAL_INT;

		return IIO_AVAIL_RANGE;
	case IIO_CHAN_INFO_CALIBBIAS:
		*vals = scd30_temp_calibbias_available;
		*type = IIO_VAL_INT;

		return IIO_AVAIL_RANGE;
	}

	return -EINVAL;
}

static ssize_t sampling_frequency_available_show(struct device *dev, struct device_attribute *attr,
						 char *buf)
{
	int i = SCD30_MEAS_INTERVAL_MIN_S;
	ssize_t len = 0;

	do {
		len += sysfs_emit_at(buf, len, "0.%09u ", 1000000000 / i);
		/*
		 * Not all values fit PAGE_SIZE buffer hence print every 6th
		 * (each frequency differs by 6s in time domain from the
		 * adjacent). Unlisted but valid ones are still accepted.
		 */
		i += 6;
	} while (i <= SCD30_MEAS_INTERVAL_MAX_S);

	buf[len - 1] = '\n';

	return len;
}

static ssize_t calibration_auto_enable_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd30_state *state = iio_priv(indio_dev);
	int ret;
	u16 val;

	mutex_lock(&state->lock);
	ret = scd30_command_read(state, CMD_ASC, &val);
	mutex_unlock(&state->lock);

	return ret ?: sysfs_emit(buf, "%d\n", val);
}

static ssize_t calibration_auto_enable_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd30_state *state = iio_priv(indio_dev);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	mutex_lock(&state->lock);
	ret = scd30_command_write(state, CMD_ASC, val);
	mutex_unlock(&state->lock);

	return ret ?: len;
}

static ssize_t calibration_forced_value_show(struct device *dev, struct device_attribute *attr,
					     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd30_state *state = iio_priv(indio_dev);
	int ret;
	u16 val;

	mutex_lock(&state->lock);
	ret = scd30_command_read(state, CMD_FRC, &val);
	mutex_unlock(&state->lock);

	return ret ?: sysfs_emit(buf, "%d\n", val);
}

static ssize_t calibration_forced_value_store(struct device *dev, struct device_attribute *attr,
					      const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct scd30_state *state = iio_priv(indio_dev);
	int ret;
	u16 val;

	ret = kstrtou16(buf, 0, &val);
	if (ret)
		return ret;

	if (val < SCD30_FRC_MIN_PPM || val > SCD30_FRC_MAX_PPM)
		return -EINVAL;

	mutex_lock(&state->lock);
	ret = scd30_command_write(state, CMD_FRC, val);
	mutex_unlock(&state->lock);

	return ret ?: len;
}

static IIO_DEVICE_ATTR_RO(sampling_frequency_available, 0);
static IIO_DEVICE_ATTR_RW(calibration_auto_enable, 0);
static IIO_DEVICE_ATTR_RW(calibration_forced_value, 0);

static struct attribute *scd30_attrs[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_calibration_auto_enable.dev_attr.attr,
	&iio_dev_attr_calibration_forced_value.dev_attr.attr,
	NULL
};

static const struct attribute_group scd30_attr_group = {
	.attrs = scd30_attrs,
};

static const struct iio_info scd30_info = {
	.attrs = &scd30_attr_group,
	.read_raw = scd30_read_raw,
	.write_raw = scd30_write_raw,
	.write_raw_get_fmt = scd30_write_raw_get_fmt,
	.read_avail = scd30_read_avail,
};

#define SCD30_CHAN_SCAN_TYPE(_sign, _realbits) .scan_type = { \
	.sign = _sign, \
	.realbits = _realbits, \
	.storagebits = 32, \
	.endianness = IIO_CPU, \
}

static const struct iio_chan_spec scd30_channels[] = {
	{
		/*
		 * this channel is special in a sense we are pretending that
		 * sensor is able to change measurement chamber pressure but in
		 * fact we're just setting pressure compensation value
		 */
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW),
		.output = 1,
		.scan_index = -1,
	},
	{
		.type = IIO_CONCENTRATION,
		.channel2 = IIO_MOD_CO2,
		.address = SCD30_CONC,
		.scan_index = SCD30_CONC,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.modified = 1,

		SCD30_CHAN_SCAN_TYPE('u', 20),
	},
	{
		.type = IIO_TEMP,
		.address = SCD30_TEMP,
		.scan_index = SCD30_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_CALIBBIAS),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_CALIBBIAS),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),

		SCD30_CHAN_SCAN_TYPE('s', 18),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.address = SCD30_HR,
		.scan_index = SCD30_HR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),

		SCD30_CHAN_SCAN_TYPE('u', 17),
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int scd30_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct scd30_state *state  = iio_priv(indio_dev);
	int ret;

	ret = scd30_command_write(state, CMD_STOP_MEAS, 0);
	if (ret)
		return ret;

	return regulator_disable(state->vdd);
}

static int scd30_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct scd30_state *state = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(state->vdd);
	if (ret)
		return ret;

	return scd30_command_write(state, CMD_START_MEAS, state->pressure_comp);
}

EXPORT_NS_SIMPLE_DEV_PM_OPS(scd30_pm_ops, scd30_suspend, scd30_resume, IIO_SCD30);

static void scd30_stop_meas(void *data)
{
	struct scd30_state *state = data;

	scd30_command_write(state, CMD_STOP_MEAS, 0);
}

static void scd30_disable_regulator(void *data)
{
	struct scd30_state *state = data;

	regulator_disable(state->vdd);
}

static irqreturn_t scd30_irq_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;

	if (iio_buffer_enabled(indio_dev)) {
		iio_trigger_poll(indio_dev->trig);

		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t scd30_irq_thread_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct scd30_state *state = iio_priv(indio_dev);
	int ret;

	ret = scd30_read_meas(state);
	if (ret)
		goto out;

	complete_all(&state->meas_ready);
out:
	return IRQ_HANDLED;
}

static irqreturn_t scd30_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct scd30_state *state = iio_priv(indio_dev);
	struct {
		int data[SCD30_MEAS_COUNT];
		s64 ts __aligned(8);
	} scan;
	int ret;

	mutex_lock(&state->lock);
	if (!iio_trigger_using_own(indio_dev))
		ret = scd30_read_poll(state);
	else
		ret = scd30_read_meas(state);
	memset(&scan, 0, sizeof(scan));
	memcpy(scan.data, state->meas, sizeof(state->meas));
	mutex_unlock(&state->lock);
	if (ret)
		goto out;

	iio_push_to_buffers_with_timestamp(indio_dev, &scan, iio_get_time_ns(indio_dev));
out:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int scd30_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct scd30_state *st = iio_priv(indio_dev);

	if (state)
		enable_irq(st->irq);
	else
		disable_irq(st->irq);

	return 0;
}

static const struct iio_trigger_ops scd30_trigger_ops = {
	.set_trigger_state = scd30_set_trigger_state,
	.validate_device = iio_trigger_validate_own_device,
};

static int scd30_setup_trigger(struct iio_dev *indio_dev)
{
	struct scd30_state *state = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
				      iio_device_id(indio_dev));
	if (!trig)
		return dev_err_probe(dev, -ENOMEM, "failed to allocate trigger\n");

	trig->ops = &scd30_trigger_ops;
	iio_trigger_set_drvdata(trig, indio_dev);

	ret = devm_iio_trigger_register(dev, trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(trig);

	/*
	 * Interrupt is enabled just before taking a fresh measurement
	 * and disabled afterwards. This means we need to ensure it is not
	 * enabled here to keep calls to enable/disable balanced.
	 */
	ret = devm_request_threaded_irq(dev, state->irq, scd30_irq_handler,
					scd30_irq_thread_handler,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT |
					IRQF_NO_AUTOEN,
					indio_dev->name, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq\n");

	return 0;
}

int scd30_probe(struct device *dev, int irq, const char *name, void *priv,
		scd30_command_t command)
{
	static const unsigned long scd30_scan_masks[] = { 0x07, 0x00 };
	struct scd30_state *state;
	struct iio_dev *indio_dev;
	int ret;
	u16 val;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	state->dev = dev;
	state->priv = priv;
	state->irq = irq;
	state->pressure_comp = SCD30_PRESSURE_COMP_DEFAULT;
	state->meas_interval = SCD30_MEAS_INTERVAL_DEFAULT;
	state->command = command;
	mutex_init(&state->lock);
	init_completion(&state->meas_ready);

	dev_set_drvdata(dev, indio_dev);

	indio_dev->info = &scd30_info;
	indio_dev->name = name;
	indio_dev->channels = scd30_channels;
	indio_dev->num_channels = ARRAY_SIZE(scd30_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = scd30_scan_masks;

	state->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(state->vdd))
		return dev_err_probe(dev, PTR_ERR(state->vdd), "failed to get regulator\n");

	ret = regulator_enable(state->vdd);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, scd30_disable_regulator, state);
	if (ret)
		return ret;

	ret = scd30_reset(state);
	if (ret)
		return dev_err_probe(dev, ret, "failed to reset device\n");

	if (state->irq > 0) {
		ret = scd30_setup_trigger(indio_dev);
		if (ret)
			return dev_err_probe(dev, ret, "failed to setup trigger\n");
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL, scd30_trigger_handler, NULL);
	if (ret)
		return ret;

	ret = scd30_command_read(state, CMD_FW_VERSION, &val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read firmware version\n");
	dev_info(dev, "firmware version: %d.%d\n", val >> 8, (char)val);

	ret = scd30_command_write(state, CMD_MEAS_INTERVAL, state->meas_interval);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set measurement interval\n");

	ret = scd30_command_write(state, CMD_START_MEAS, state->pressure_comp);
	if (ret)
		return dev_err_probe(dev, ret, "failed to start measurement\n");

	ret = devm_add_action_or_reset(dev, scd30_stop_meas, state);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS(scd30_probe, IIO_SCD30);

MODULE_AUTHOR("Tomasz Duszynski <tomasz.duszynski@octakon.com>");
MODULE_DESCRIPTION("Sensirion SCD30 carbon dioxide sensor core driver");
MODULE_LICENSE("GPL v2");
