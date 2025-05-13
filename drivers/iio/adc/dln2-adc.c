// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Diolan DLN-2 USB-ADC adapter
 *
 * Copyright (c) 2017 Jack Andersen
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mfd/dln2.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#define DLN2_ADC_MOD_NAME "dln2-adc"

#define DLN2_ADC_ID             0x06

#define DLN2_ADC_GET_CHANNEL_COUNT	DLN2_CMD(0x01, DLN2_ADC_ID)
#define DLN2_ADC_ENABLE			DLN2_CMD(0x02, DLN2_ADC_ID)
#define DLN2_ADC_DISABLE		DLN2_CMD(0x03, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_ENABLE		DLN2_CMD(0x05, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_DISABLE	DLN2_CMD(0x06, DLN2_ADC_ID)
#define DLN2_ADC_SET_RESOLUTION		DLN2_CMD(0x08, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_GET_VAL	DLN2_CMD(0x0A, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_GET_ALL_VAL	DLN2_CMD(0x0B, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_SET_CFG	DLN2_CMD(0x0C, DLN2_ADC_ID)
#define DLN2_ADC_CHANNEL_GET_CFG	DLN2_CMD(0x0D, DLN2_ADC_ID)
#define DLN2_ADC_CONDITION_MET_EV	DLN2_CMD(0x10, DLN2_ADC_ID)

#define DLN2_ADC_EVENT_NONE		0
#define DLN2_ADC_EVENT_BELOW		1
#define DLN2_ADC_EVENT_LEVEL_ABOVE	2
#define DLN2_ADC_EVENT_OUTSIDE		3
#define DLN2_ADC_EVENT_INSIDE		4
#define DLN2_ADC_EVENT_ALWAYS		5

#define DLN2_ADC_MAX_CHANNELS 8
#define DLN2_ADC_DATA_BITS 10

/*
 * Plays similar role to iio_demux_table in subsystem core; except allocated
 * in a fixed 8-element array.
 */
struct dln2_adc_demux_table {
	unsigned int from;
	unsigned int to;
	unsigned int length;
};

struct dln2_adc {
	struct platform_device *pdev;
	struct iio_chan_spec iio_channels[DLN2_ADC_MAX_CHANNELS + 1];
	int port, trigger_chan;
	struct iio_trigger *trig;
	struct mutex mutex;
	/* Cached sample period in milliseconds */
	unsigned int sample_period;
	/* Demux table */
	unsigned int demux_count;
	struct dln2_adc_demux_table demux[DLN2_ADC_MAX_CHANNELS];
};

struct dln2_adc_port_chan {
	u8 port;
	u8 chan;
};

struct dln2_adc_get_all_vals {
	__le16 channel_mask;
	__le16 values[DLN2_ADC_MAX_CHANNELS];
};

static void dln2_adc_add_demux(struct dln2_adc *dln2,
	unsigned int in_loc, unsigned int out_loc,
	unsigned int length)
{
	struct dln2_adc_demux_table *p = dln2->demux_count ?
		&dln2->demux[dln2->demux_count - 1] : NULL;

	if (p && p->from + p->length == in_loc &&
		p->to + p->length == out_loc) {
		p->length += length;
	} else if (dln2->demux_count < DLN2_ADC_MAX_CHANNELS) {
		p = &dln2->demux[dln2->demux_count++];
		p->from = in_loc;
		p->to = out_loc;
		p->length = length;
	}
}

static void dln2_adc_update_demux(struct dln2_adc *dln2)
{
	int in_ind = -1, out_ind;
	unsigned int in_loc = 0, out_loc = 0;
	struct iio_dev *indio_dev = platform_get_drvdata(dln2->pdev);

	/* Clear out any old demux */
	dln2->demux_count = 0;

	/* Optimize all 8-channels case */
	if (iio_get_masklength(indio_dev) &&
	    (*indio_dev->active_scan_mask & 0xff) == 0xff) {
		dln2_adc_add_demux(dln2, 0, 0, 16);
		return;
	}

	/* Build demux table from fixed 8-channels to active_scan_mask */
	iio_for_each_active_channel(indio_dev, out_ind) {
		/* Handle timestamp separately */
		if (out_ind == DLN2_ADC_MAX_CHANNELS)
			break;
		for (++in_ind; in_ind != out_ind; ++in_ind)
			in_loc += 2;
		dln2_adc_add_demux(dln2, in_loc, out_loc, 2);
		out_loc += 2;
		in_loc += 2;
	}
}

static int dln2_adc_get_chan_count(struct dln2_adc *dln2)
{
	int ret;
	u8 port = dln2->port;
	u8 count;
	int olen = sizeof(count);

	ret = dln2_transfer(dln2->pdev, DLN2_ADC_GET_CHANNEL_COUNT,
			    &port, sizeof(port), &count, &olen);
	if (ret < 0) {
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);
		return ret;
	}
	if (olen < sizeof(count))
		return -EPROTO;

	return count;
}

static int dln2_adc_set_port_resolution(struct dln2_adc *dln2)
{
	int ret;
	struct dln2_adc_port_chan port_chan = {
		.port = dln2->port,
		.chan = DLN2_ADC_DATA_BITS,
	};

	ret = dln2_transfer_tx(dln2->pdev, DLN2_ADC_SET_RESOLUTION,
			       &port_chan, sizeof(port_chan));
	if (ret < 0)
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);

	return ret;
}

static int dln2_adc_set_chan_enabled(struct dln2_adc *dln2,
				     int channel, bool enable)
{
	int ret;
	struct dln2_adc_port_chan port_chan = {
		.port = dln2->port,
		.chan = channel,
	};
	u16 cmd = enable ? DLN2_ADC_CHANNEL_ENABLE : DLN2_ADC_CHANNEL_DISABLE;

	ret = dln2_transfer_tx(dln2->pdev, cmd, &port_chan, sizeof(port_chan));
	if (ret < 0)
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);

	return ret;
}

static int dln2_adc_set_port_enabled(struct dln2_adc *dln2, bool enable,
				     u16 *conflict_out)
{
	int ret;
	u8 port = dln2->port;
	__le16 conflict;
	int olen = sizeof(conflict);
	u16 cmd = enable ? DLN2_ADC_ENABLE : DLN2_ADC_DISABLE;

	if (conflict_out)
		*conflict_out = 0;

	ret = dln2_transfer(dln2->pdev, cmd, &port, sizeof(port),
			    &conflict, &olen);
	if (ret < 0) {
		dev_dbg(&dln2->pdev->dev, "Problem in %s(%d)\n",
			__func__, (int)enable);
		if (conflict_out && enable && olen >= sizeof(conflict))
			*conflict_out = le16_to_cpu(conflict);
		return ret;
	}
	if (enable && olen < sizeof(conflict))
		return -EPROTO;

	return ret;
}

static int dln2_adc_set_chan_period(struct dln2_adc *dln2,
	unsigned int channel, unsigned int period)
{
	int ret;
	struct {
		struct dln2_adc_port_chan port_chan;
		__u8 type;
		__le16 period;
		__le16 low;
		__le16 high;
	} __packed set_cfg = {
		.port_chan.port = dln2->port,
		.port_chan.chan = channel,
		.type = period ? DLN2_ADC_EVENT_ALWAYS : DLN2_ADC_EVENT_NONE,
		.period = cpu_to_le16(period)
	};

	ret = dln2_transfer_tx(dln2->pdev, DLN2_ADC_CHANNEL_SET_CFG,
			       &set_cfg, sizeof(set_cfg));
	if (ret < 0)
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);

	return ret;
}

static int dln2_adc_read(struct dln2_adc *dln2, unsigned int channel)
{
	int ret, i;
	u16 conflict;
	__le16 value;
	int olen = sizeof(value);
	struct dln2_adc_port_chan port_chan = {
		.port = dln2->port,
		.chan = channel,
	};

	ret = dln2_adc_set_chan_enabled(dln2, channel, true);
	if (ret < 0)
		return ret;

	ret = dln2_adc_set_port_enabled(dln2, true, &conflict);
	if (ret < 0) {
		if (conflict) {
			dev_err(&dln2->pdev->dev,
				"ADC pins conflict with mask %04X\n",
				(int)conflict);
			ret = -EBUSY;
		}
		goto disable_chan;
	}

	/*
	 * Call GET_VAL twice due to initial zero-return immediately after
	 * enabling channel.
	 */
	for (i = 0; i < 2; ++i) {
		ret = dln2_transfer(dln2->pdev, DLN2_ADC_CHANNEL_GET_VAL,
				    &port_chan, sizeof(port_chan),
				    &value, &olen);
		if (ret < 0) {
			dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);
			goto disable_port;
		}
		if (olen < sizeof(value)) {
			ret = -EPROTO;
			goto disable_port;
		}
	}

	ret = le16_to_cpu(value);

disable_port:
	dln2_adc_set_port_enabled(dln2, false, NULL);
disable_chan:
	dln2_adc_set_chan_enabled(dln2, channel, false);

	return ret;
}

static int dln2_adc_read_all(struct dln2_adc *dln2,
			     struct dln2_adc_get_all_vals *get_all_vals)
{
	int ret;
	__u8 port = dln2->port;
	int olen = sizeof(*get_all_vals);

	ret = dln2_transfer(dln2->pdev, DLN2_ADC_CHANNEL_GET_ALL_VAL,
			    &port, sizeof(port), get_all_vals, &olen);
	if (ret < 0) {
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);
		return ret;
	}
	if (olen < sizeof(*get_all_vals))
		return -EPROTO;

	return ret;
}

static int dln2_adc_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	int ret;
	unsigned int microhertz;
	struct dln2_adc *dln2 = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		mutex_lock(&dln2->mutex);
		ret = dln2_adc_read(dln2, chan->channel);
		mutex_unlock(&dln2->mutex);

		iio_device_release_direct(indio_dev);

		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		/*
		 * Voltage reference is fixed at 3.3v
		 *  3.3 / (1 << 10) * 1000000000
		 */
		*val = 0;
		*val2 = 3222656;
		return IIO_VAL_INT_PLUS_NANO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (dln2->sample_period) {
			microhertz = 1000000000 / dln2->sample_period;
			*val = microhertz / 1000000;
			*val2 = microhertz % 1000000;
		} else {
			*val = 0;
			*val2 = 0;
		}

		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int dln2_adc_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val,
			      int val2,
			      long mask)
{
	int ret;
	unsigned int microhertz;
	struct dln2_adc *dln2 = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		microhertz = 1000000 * val + val2;

		mutex_lock(&dln2->mutex);

		dln2->sample_period =
			microhertz ? 1000000000 / microhertz : UINT_MAX;
		if (dln2->sample_period > 65535) {
			dln2->sample_period = 65535;
			dev_warn(&dln2->pdev->dev,
				 "clamping period to 65535ms\n");
		}

		/*
		 * The first requested channel is arbitrated as a shared
		 * trigger source, so only one event is registered with the
		 * DLN. The event handler will then read all enabled channel
		 * values using DLN2_ADC_CHANNEL_GET_ALL_VAL to maintain
		 * synchronization between ADC readings.
		 */
		if (dln2->trigger_chan != -1)
			ret = dln2_adc_set_chan_period(dln2,
				dln2->trigger_chan, dln2->sample_period);
		else
			ret = 0;

		mutex_unlock(&dln2->mutex);

		return ret;

	default:
		return -EINVAL;
	}
}

static int dln2_update_scan_mode(struct iio_dev *indio_dev,
				 const unsigned long *scan_mask)
{
	struct dln2_adc *dln2 = iio_priv(indio_dev);
	int chan_count = indio_dev->num_channels - 1;
	int ret, i, j;

	mutex_lock(&dln2->mutex);

	for (i = 0; i < chan_count; ++i) {
		ret = dln2_adc_set_chan_enabled(dln2, i,
						test_bit(i, scan_mask));
		if (ret < 0) {
			for (j = 0; j < i; ++j)
				dln2_adc_set_chan_enabled(dln2, j, false);
			mutex_unlock(&dln2->mutex);
			dev_err(&dln2->pdev->dev,
				"Unable to enable ADC channel %d\n", i);
			return -EBUSY;
		}
	}

	dln2_adc_update_demux(dln2);

	mutex_unlock(&dln2->mutex);

	return 0;
}

#define DLN2_ADC_CHAN(lval, idx) {					\
	lval.type = IIO_VOLTAGE;					\
	lval.channel = idx;						\
	lval.indexed = 1;						\
	lval.info_mask_separate = BIT(IIO_CHAN_INFO_RAW);		\
	lval.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE) |	\
				       BIT(IIO_CHAN_INFO_SAMP_FREQ);	\
	lval.scan_index = idx;						\
	lval.scan_type.sign = 'u';					\
	lval.scan_type.realbits = DLN2_ADC_DATA_BITS;			\
	lval.scan_type.storagebits = 16;				\
	lval.scan_type.endianness = IIO_LE;				\
}

/* Assignment version of IIO_CHAN_SOFT_TIMESTAMP */
#define IIO_CHAN_SOFT_TIMESTAMP_ASSIGN(lval, _si) {	\
	lval.type = IIO_TIMESTAMP;			\
	lval.channel = -1;				\
	lval.scan_index = _si;				\
	lval.scan_type.sign = 's';			\
	lval.scan_type.realbits = 64;			\
	lval.scan_type.storagebits = 64;		\
}

static const struct iio_info dln2_adc_info = {
	.read_raw = dln2_adc_read_raw,
	.write_raw = dln2_adc_write_raw,
	.update_scan_mode = dln2_update_scan_mode,
};

static irqreturn_t dln2_adc_trigger_h(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct {
		__le16 values[DLN2_ADC_MAX_CHANNELS];
		aligned_s64 timestamp_space;
	} data;
	struct dln2_adc_get_all_vals dev_data;
	struct dln2_adc *dln2 = iio_priv(indio_dev);
	const struct dln2_adc_demux_table *t;
	int ret, i;

	mutex_lock(&dln2->mutex);
	ret = dln2_adc_read_all(dln2, &dev_data);
	mutex_unlock(&dln2->mutex);
	if (ret < 0)
		goto done;

	memset(&data, 0, sizeof(data));

	/* Demux operation */
	for (i = 0; i < dln2->demux_count; ++i) {
		t = &dln2->demux[i];
		memcpy((void *)data.values + t->to,
		       (void *)dev_data.values + t->from, t->length);
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data,
					   iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int dln2_adc_triggered_buffer_postenable(struct iio_dev *indio_dev)
{
	int ret;
	struct dln2_adc *dln2 = iio_priv(indio_dev);
	u16 conflict;
	unsigned int trigger_chan;

	mutex_lock(&dln2->mutex);

	/* Enable ADC */
	ret = dln2_adc_set_port_enabled(dln2, true, &conflict);
	if (ret < 0) {
		mutex_unlock(&dln2->mutex);
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);
		if (conflict) {
			dev_err(&dln2->pdev->dev,
				"ADC pins conflict with mask %04X\n",
				(int)conflict);
			ret = -EBUSY;
		}
		return ret;
	}

	/* Assign trigger channel based on first enabled channel */
	trigger_chan = find_first_bit(indio_dev->active_scan_mask,
				      iio_get_masklength(indio_dev));
	if (trigger_chan < DLN2_ADC_MAX_CHANNELS) {
		dln2->trigger_chan = trigger_chan;
		ret = dln2_adc_set_chan_period(dln2, dln2->trigger_chan,
					       dln2->sample_period);
		mutex_unlock(&dln2->mutex);
		if (ret < 0) {
			dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);
			return ret;
		}
	} else {
		dln2->trigger_chan = -1;
		mutex_unlock(&dln2->mutex);
	}

	return 0;
}

static int dln2_adc_triggered_buffer_predisable(struct iio_dev *indio_dev)
{
	int ret;
	struct dln2_adc *dln2 = iio_priv(indio_dev);

	mutex_lock(&dln2->mutex);

	/* Disable trigger channel */
	if (dln2->trigger_chan != -1) {
		dln2_adc_set_chan_period(dln2, dln2->trigger_chan, 0);
		dln2->trigger_chan = -1;
	}

	/* Disable ADC */
	ret = dln2_adc_set_port_enabled(dln2, false, NULL);

	mutex_unlock(&dln2->mutex);
	if (ret < 0)
		dev_dbg(&dln2->pdev->dev, "Problem in %s\n", __func__);

	return ret;
}

static const struct iio_buffer_setup_ops dln2_adc_buffer_setup_ops = {
	.postenable = dln2_adc_triggered_buffer_postenable,
	.predisable = dln2_adc_triggered_buffer_predisable,
};

static void dln2_adc_event(struct platform_device *pdev, u16 echo,
			   const void *data, int len)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct dln2_adc *dln2 = iio_priv(indio_dev);

	/* Called via URB completion handler */
	iio_trigger_poll(dln2->trig);
}

static int dln2_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dln2_adc *dln2;
	struct dln2_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev;
	int i, ret, chans;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*dln2));
	if (!indio_dev) {
		dev_err(dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	dln2 = iio_priv(indio_dev);
	dln2->pdev = pdev;
	dln2->port = pdata->port;
	dln2->trigger_chan = -1;
	mutex_init(&dln2->mutex);

	platform_set_drvdata(pdev, indio_dev);

	ret = dln2_adc_set_port_resolution(dln2);
	if (ret < 0) {
		dev_err(dev, "failed to set ADC resolution to 10 bits\n");
		return ret;
	}

	chans = dln2_adc_get_chan_count(dln2);
	if (chans < 0) {
		dev_err(dev, "failed to get channel count: %d\n", chans);
		return chans;
	}
	if (chans > DLN2_ADC_MAX_CHANNELS) {
		chans = DLN2_ADC_MAX_CHANNELS;
		dev_warn(dev, "clamping channels to %d\n",
			 DLN2_ADC_MAX_CHANNELS);
	}

	for (i = 0; i < chans; ++i)
		DLN2_ADC_CHAN(dln2->iio_channels[i], i)
	IIO_CHAN_SOFT_TIMESTAMP_ASSIGN(dln2->iio_channels[i], i);

	indio_dev->name = DLN2_ADC_MOD_NAME;
	indio_dev->info = &dln2_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dln2->iio_channels;
	indio_dev->num_channels = chans + 1;
	indio_dev->setup_ops = &dln2_adc_buffer_setup_ops;

	dln2->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
					    indio_dev->name,
					    iio_device_id(indio_dev));
	if (!dln2->trig) {
		dev_err(dev, "failed to allocate trigger\n");
		return -ENOMEM;
	}
	iio_trigger_set_drvdata(dln2->trig, dln2);
	ret = devm_iio_trigger_register(dev, dln2->trig);
	if (ret) {
		dev_err(dev, "failed to register trigger: %d\n", ret);
		return ret;
	}
	iio_trigger_set_immutable(indio_dev, dln2->trig);

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      dln2_adc_trigger_h,
					      &dln2_adc_buffer_setup_ops);
	if (ret) {
		dev_err(dev, "failed to allocate triggered buffer: %d\n", ret);
		return ret;
	}

	ret = dln2_register_event_cb(pdev, DLN2_ADC_CONDITION_MET_EV,
				     dln2_adc_event);
	if (ret) {
		dev_err(dev, "failed to setup DLN2 periodic event: %d\n", ret);
		return ret;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "failed to register iio device: %d\n", ret);
		goto unregister_event;
	}

	return ret;

unregister_event:
	dln2_unregister_event_cb(pdev, DLN2_ADC_CONDITION_MET_EV);

	return ret;
}

static void dln2_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);

	iio_device_unregister(indio_dev);
	dln2_unregister_event_cb(pdev, DLN2_ADC_CONDITION_MET_EV);
}

static struct platform_driver dln2_adc_driver = {
	.driver.name	= DLN2_ADC_MOD_NAME,
	.probe		= dln2_adc_probe,
	.remove		= dln2_adc_remove,
};

module_platform_driver(dln2_adc_driver);

MODULE_AUTHOR("Jack Andersen <jackoalan@gmail.com");
MODULE_DESCRIPTION("Driver for the Diolan DLN2 ADC interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dln2-adc");
