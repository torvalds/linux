// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Common part of most Semtech SAR sensor.
 */

#include <linux/bitops.h>
#include <linux/byteorder/generic.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <vdso/bits.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include "sx_common.h"

/* All Semtech SAR sensors have IRQ bit in the same order. */
#define   SX_COMMON_CONVDONE_IRQ			BIT(0)
#define   SX_COMMON_FAR_IRQ				BIT(2)
#define   SX_COMMON_CLOSE_IRQ				BIT(3)

const struct iio_event_spec sx_common_events[3] = {
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
EXPORT_SYMBOL_NS_GPL(sx_common_events, SEMTECH_PROX);

static irqreturn_t sx_common_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx_common_data *data = iio_priv(indio_dev);

	if (data->trigger_enabled)
		iio_trigger_poll(data->trig);

	/*
	 * Even if no event is enabled, we need to wake the thread to clear the
	 * interrupt state by reading SX_COMMON_REG_IRQ_SRC.
	 * It is not possible to do that here because regmap_read takes a mutex.
	 */
	return IRQ_WAKE_THREAD;
}

static void sx_common_push_events(struct iio_dev *indio_dev)
{
	int ret;
	unsigned int val, chan;
	struct sx_common_data *data = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	unsigned long prox_changed;

	/* Read proximity state on all channels */
	ret = regmap_read(data->regmap, data->chip_info->reg_stat, &val);
	if (ret) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		return;
	}

	val >>= data->chip_info->stat_offset;

	/*
	 * Only iterate over channels with changes on proximity status that have
	 * events enabled.
	 */
	prox_changed = (data->chan_prox_stat ^ val) & data->chan_event;

	for_each_set_bit(chan, &prox_changed, data->chip_info->num_channels) {
		int dir;
		u64 ev;

		dir = (val & BIT(chan)) ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING;
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, chan,
					  IIO_EV_TYPE_THRESH, dir);

		iio_push_event(indio_dev, ev, timestamp);
	}
	data->chan_prox_stat = val;
}

static int sx_common_enable_irq(struct sx_common_data *data, unsigned int irq)
{
	if (!data->client->irq)
		return 0;
	return regmap_update_bits(data->regmap, data->chip_info->reg_irq_msk,
				  irq << data->chip_info->irq_msk_offset,
				  irq << data->chip_info->irq_msk_offset);
}

static int sx_common_disable_irq(struct sx_common_data *data, unsigned int irq)
{
	if (!data->client->irq)
		return 0;
	return regmap_update_bits(data->regmap, data->chip_info->reg_irq_msk,
				  irq << data->chip_info->irq_msk_offset, 0);
}

static int sx_common_update_chan_en(struct sx_common_data *data,
				    unsigned long chan_read,
				    unsigned long chan_event)
{
	int ret;
	unsigned long channels = chan_read | chan_event;

	if ((data->chan_read | data->chan_event) != channels) {
		ret = regmap_update_bits(data->regmap,
					 data->chip_info->reg_enable_chan,
					 data->chip_info->mask_enable_chan,
					 channels);
		if (ret)
			return ret;
	}
	data->chan_read = chan_read;
	data->chan_event = chan_event;
	return 0;
}

static int sx_common_get_read_channel(struct sx_common_data *data, int channel)
{
	return sx_common_update_chan_en(data, data->chan_read | BIT(channel),
				     data->chan_event);
}

static int sx_common_put_read_channel(struct sx_common_data *data, int channel)
{
	return sx_common_update_chan_en(data, data->chan_read & ~BIT(channel),
				     data->chan_event);
}

static int sx_common_get_event_channel(struct sx_common_data *data, int channel)
{
	return sx_common_update_chan_en(data, data->chan_read,
				     data->chan_event | BIT(channel));
}

static int sx_common_put_event_channel(struct sx_common_data *data, int channel)
{
	return sx_common_update_chan_en(data, data->chan_read,
				     data->chan_event & ~BIT(channel));
}

/**
 * sx_common_read_proximity() - Read raw proximity value.
 * @data:	Internal data
 * @chan:	Channel to read
 * @val:	pointer to return read value.
 *
 * Request a conversion, wait for the sensor to be ready and
 * return the raw proximity value.
 */
int sx_common_read_proximity(struct sx_common_data *data,
			     const struct iio_chan_spec *chan, int *val)
{
	int ret;
	__be16 rawval;

	mutex_lock(&data->mutex);

	ret = sx_common_get_read_channel(data, chan->channel);
	if (ret)
		goto out;

	ret = sx_common_enable_irq(data, SX_COMMON_CONVDONE_IRQ);
	if (ret)
		goto out_put_channel;

	mutex_unlock(&data->mutex);

	if (data->client->irq) {
		ret = wait_for_completion_interruptible(&data->completion);
		reinit_completion(&data->completion);
	} else {
		ret = data->chip_info->ops.wait_for_sample(data);
	}

	mutex_lock(&data->mutex);

	if (ret)
		goto out_disable_irq;

	ret = data->chip_info->ops.read_prox_data(data, chan, &rawval);
	if (ret)
		goto out_disable_irq;

	*val = sign_extend32(be16_to_cpu(rawval), chan->scan_type.realbits - 1);

	ret = sx_common_disable_irq(data, SX_COMMON_CONVDONE_IRQ);
	if (ret)
		goto out_put_channel;

	ret = sx_common_put_read_channel(data, chan->channel);
	if (ret)
		goto out;

	mutex_unlock(&data->mutex);

	return IIO_VAL_INT;

out_disable_irq:
	sx_common_disable_irq(data, SX_COMMON_CONVDONE_IRQ);
out_put_channel:
	sx_common_put_read_channel(data, chan->channel);
out:
	mutex_unlock(&data->mutex);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(sx_common_read_proximity, SEMTECH_PROX);

/**
 * sx_common_read_event_config() - Configure event setting.
 * @indio_dev:	iio device object
 * @chan:	Channel to read
 * @type:	Type of event (unused)
 * @dir:	Direction of event (unused)
 *
 * return if the given channel is used for event gathering.
 */
int sx_common_read_event_config(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir)
{
	struct sx_common_data *data = iio_priv(indio_dev);

	return !!(data->chan_event & BIT(chan->channel));
}
EXPORT_SYMBOL_NS_GPL(sx_common_read_event_config, SEMTECH_PROX);

/**
 * sx_common_write_event_config() - Configure event setting.
 * @indio_dev:	iio device object
 * @chan:	Channel to enable
 * @type:	Type of event (unused)
 * @dir:	Direction of event (unused)
 * @state:	State of the event.
 *
 * Enable/Disable event on a given channel.
 */
int sx_common_write_event_config(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir, int state)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	unsigned int eventirq = SX_COMMON_FAR_IRQ | SX_COMMON_CLOSE_IRQ;
	int ret;

	/* If the state hasn't changed, there's nothing to do. */
	if (!!(data->chan_event & BIT(chan->channel)) == state)
		return 0;

	mutex_lock(&data->mutex);
	if (state) {
		ret = sx_common_get_event_channel(data, chan->channel);
		if (ret)
			goto out_unlock;
		if (!(data->chan_event & ~BIT(chan->channel))) {
			ret = sx_common_enable_irq(data, eventirq);
			if (ret)
				sx_common_put_event_channel(data, chan->channel);
		}
	} else {
		ret = sx_common_put_event_channel(data, chan->channel);
		if (ret)
			goto out_unlock;
		if (!data->chan_event) {
			ret = sx_common_disable_irq(data, eventirq);
			if (ret)
				sx_common_get_event_channel(data, chan->channel);
		}
	}

out_unlock:
	mutex_unlock(&data->mutex);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(sx_common_write_event_config, SEMTECH_PROX);

static int sx_common_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct sx_common_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->mutex);

	if (state)
		ret = sx_common_enable_irq(data, SX_COMMON_CONVDONE_IRQ);
	else if (!data->chan_read)
		ret = sx_common_disable_irq(data, SX_COMMON_CONVDONE_IRQ);
	if (ret)
		goto out;

	data->trigger_enabled = state;

out:
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_trigger_ops sx_common_trigger_ops = {
	.set_trigger_state = sx_common_set_trigger_state,
};

static irqreturn_t sx_common_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx_common_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int val;

	mutex_lock(&data->mutex);

	ret = regmap_read(data->regmap, SX_COMMON_REG_IRQ_SRC, &val);
	if (ret) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		goto out;
	}

	if (val & ((SX_COMMON_FAR_IRQ | SX_COMMON_CLOSE_IRQ) << data->chip_info->irq_msk_offset))
		sx_common_push_events(indio_dev);

	if (val & (SX_COMMON_CONVDONE_IRQ << data->chip_info->irq_msk_offset))
		complete(&data->completion);

out:
	mutex_unlock(&data->mutex);

	return IRQ_HANDLED;
}

static irqreturn_t sx_common_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct sx_common_data *data = iio_priv(indio_dev);
	__be16 val;
	int bit, ret, i = 0;

	mutex_lock(&data->mutex);

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = data->chip_info->ops.read_prox_data(data,
						     &indio_dev->channels[bit],
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

static int sx_common_buffer_preenable(struct iio_dev *indio_dev)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	unsigned long channels = 0;
	int bit, ret;

	mutex_lock(&data->mutex);
	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength)
		__set_bit(indio_dev->channels[bit].channel, &channels);

	ret = sx_common_update_chan_en(data, channels, data->chan_event);
	mutex_unlock(&data->mutex);
	return ret;
}

static int sx_common_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = sx_common_update_chan_en(data, 0, data->chan_event);
	mutex_unlock(&data->mutex);
	return ret;
}

static const struct iio_buffer_setup_ops sx_common_buffer_setup_ops = {
	.preenable = sx_common_buffer_preenable,
	.postdisable = sx_common_buffer_postdisable,
};

static void sx_common_regulator_disable(void *_data)
{
	struct sx_common_data *data = _data;

	regulator_bulk_disable(ARRAY_SIZE(data->supplies), data->supplies);
}

#define SX_COMMON_SOFT_RESET				0xde

static int sx_common_init_device(struct device *dev, struct iio_dev *indio_dev)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	struct sx_common_reg_default tmp;
	const struct sx_common_reg_default *initval;
	int ret;
	unsigned int i, val;

	ret = regmap_write(data->regmap, data->chip_info->reg_reset,
			   SX_COMMON_SOFT_RESET);
	if (ret)
		return ret;

	usleep_range(1000, 2000); /* power-up time is ~1ms. */

	/* Clear reset interrupt state by reading SX_COMMON_REG_IRQ_SRC. */
	ret = regmap_read(data->regmap, SX_COMMON_REG_IRQ_SRC, &val);
	if (ret)
		return ret;

	/* Program defaults from constant or BIOS. */
	for (i = 0; i < data->chip_info->num_default_regs; i++) {
		initval = data->chip_info->ops.get_default_reg(dev, i, &tmp);
		ret = regmap_write(data->regmap, initval->reg, initval->def);
		if (ret)
			return ret;
	}

	return data->chip_info->ops.init_compensation(indio_dev);
}

/**
 * sx_common_probe() - Common setup for Semtech SAR sensor
 * @client:		I2C client object
 * @chip_info:		Semtech sensor chip information.
 * @regmap_config:	Sensor registers map configuration.
 */
int sx_common_probe(struct i2c_client *client,
		    const struct sx_common_chip_info *chip_info,
		    const struct regmap_config *regmap_config)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct sx_common_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	data->chip_info = chip_info;
	data->client = client;
	data->supplies[0].supply = "vdd";
	data->supplies[1].supply = "svdd";
	mutex_init(&data->mutex);
	init_completion(&data->completion);

	data->regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Could init register map\n");

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->supplies),
				      data->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(data->supplies), data->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to enable regulators\n");

	/* Must wait for Tpor time after initial power up */
	usleep_range(1000, 1100);

	ret = devm_add_action_or_reset(dev, sx_common_regulator_disable, data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Unable to register regulators deleter\n");

	ret = data->chip_info->ops.check_whoami(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "error reading WHOAMI\n");

	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels =  data->chip_info->iio_channels;
	indio_dev->num_channels = data->chip_info->num_iio_channels;
	indio_dev->info = &data->chip_info->iio_info;

	i2c_set_clientdata(client, indio_dev);

	ret = sx_common_init_device(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to initialize sensor\n");

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq,
						sx_common_irq_handler,
						sx_common_irq_thread_handler,
						IRQF_ONESHOT,
						"sx_event", indio_dev);
		if (ret)
			return dev_err_probe(dev, ret, "No IRQ\n");

		data->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
						    indio_dev->name,
						    iio_device_id(indio_dev));
		if (!data->trig)
			return -ENOMEM;

		data->trig->ops = &sx_common_trigger_ops;
		iio_trigger_set_drvdata(data->trig, indio_dev);

		ret = devm_iio_trigger_register(dev, data->trig);
		if (ret)
			return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      sx_common_trigger_handler,
					      &sx_common_buffer_setup_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(sx_common_probe, SEMTECH_PROX);

MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_DESCRIPTION("Common functions and structures for Semtech sensor");
MODULE_LICENSE("GPL v2");
