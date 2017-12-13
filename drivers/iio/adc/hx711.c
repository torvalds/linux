/*
 * HX711: analog to digital converter for weight sensor module
 *
 * Copyright (c) 2016 Andreas Klinger <ak@it-klinger.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

/* gain to pulse and scale conversion */
#define HX711_GAIN_MAX		3

struct hx711_gain_to_scale {
	int			gain;
	int			gain_pulse;
	int			scale;
	int			channel;
};

/*
 * .scale depends on AVDD which in turn is known as soon as the regulator
 * is available
 * therefore we set .scale in hx711_probe()
 *
 * channel A in documentation is channel 0 in source code
 * channel B in documentation is channel 1 in source code
 */
static struct hx711_gain_to_scale hx711_gain_to_scale[HX711_GAIN_MAX] = {
	{ 128, 1, 0, 0 },
	{  32, 2, 0, 1 },
	{  64, 3, 0, 0 }
};

static int hx711_get_gain_to_pulse(int gain)
{
	int i;

	for (i = 0; i < HX711_GAIN_MAX; i++)
		if (hx711_gain_to_scale[i].gain == gain)
			return hx711_gain_to_scale[i].gain_pulse;
	return 1;
}

static int hx711_get_gain_to_scale(int gain)
{
	int i;

	for (i = 0; i < HX711_GAIN_MAX; i++)
		if (hx711_gain_to_scale[i].gain == gain)
			return hx711_gain_to_scale[i].scale;
	return 0;
}

static int hx711_get_scale_to_gain(int scale)
{
	int i;

	for (i = 0; i < HX711_GAIN_MAX; i++)
		if (hx711_gain_to_scale[i].scale == scale)
			return hx711_gain_to_scale[i].gain;
	return -EINVAL;
}

struct hx711_data {
	struct device		*dev;
	struct gpio_desc	*gpiod_pd_sck;
	struct gpio_desc	*gpiod_dout;
	struct regulator	*reg_avdd;
	int			gain_set;	/* gain set on device */
	int			gain_chan_a;	/* gain for channel A */
	struct mutex		lock;
	/*
	 * triggered buffer
	 * 2x32-bit channel + 64-bit timestamp
	 */
	u32			buffer[4];
};

static int hx711_cycle(struct hx711_data *hx711_data)
{
	int val;

	/*
	 * if preempted for more then 60us while PD_SCK is high:
	 * hx711 is going in reset
	 * ==> measuring is false
	 */
	preempt_disable();
	gpiod_set_value(hx711_data->gpiod_pd_sck, 1);
	val = gpiod_get_value(hx711_data->gpiod_dout);
	/*
	 * here we are not waiting for 0.2 us as suggested by the datasheet,
	 * because the oscilloscope showed in a test scenario
	 * at least 1.15 us for PD_SCK high (T3 in datasheet)
	 * and 0.56 us for PD_SCK low on TI Sitara with 800 MHz
	 */
	gpiod_set_value(hx711_data->gpiod_pd_sck, 0);
	preempt_enable();

	return val;
}

static int hx711_read(struct hx711_data *hx711_data)
{
	int i, ret;
	int value = 0;
	int val = gpiod_get_value(hx711_data->gpiod_dout);

	/* we double check if it's really down */
	if (val)
		return -EIO;

	for (i = 0; i < 24; i++) {
		value <<= 1;
		ret = hx711_cycle(hx711_data);
		if (ret)
			value++;
	}

	value ^= 0x800000;

	for (i = 0; i < hx711_get_gain_to_pulse(hx711_data->gain_set); i++)
		hx711_cycle(hx711_data);

	return value;
}

static int hx711_wait_for_ready(struct hx711_data *hx711_data)
{
	int i, val;

	/*
	 * a maximum reset cycle time of 56 ms was measured.
	 * we round it up to 100 ms
	 */
	for (i = 0; i < 100; i++) {
		val = gpiod_get_value(hx711_data->gpiod_dout);
		if (!val)
			break;
		/* sleep at least 1 ms */
		msleep(1);
	}
	if (val)
		return -EIO;

	return 0;
}

static int hx711_reset(struct hx711_data *hx711_data)
{
	int ret;
	int val = gpiod_get_value(hx711_data->gpiod_dout);

	if (val) {
		/*
		 * an examination with the oszilloscope indicated
		 * that the first value read after the reset is not stable
		 * if we reset too short;
		 * the shorter the reset cycle
		 * the less reliable the first value after reset is;
		 * there were no problems encountered with a value
		 * of 10 ms or higher
		 */
		gpiod_set_value(hx711_data->gpiod_pd_sck, 1);
		msleep(10);
		gpiod_set_value(hx711_data->gpiod_pd_sck, 0);

		ret = hx711_wait_for_ready(hx711_data);
		if (ret)
			return ret;
		/*
		 * after a reset the gain is 128 so we do a dummy read
		 * to set the gain for the next read
		 */
		ret = hx711_read(hx711_data);
		if (ret < 0)
			return ret;

		/*
		 * after a dummy read we need to wait vor readiness
		 * for not mixing gain pulses with the clock
		 */
		ret = hx711_wait_for_ready(hx711_data);
		if (ret)
			return ret;
	}

	return val;
}

static int hx711_set_gain_for_channel(struct hx711_data *hx711_data, int chan)
{
	int ret;

	if (chan == 0) {
		if (hx711_data->gain_set == 32) {
			hx711_data->gain_set = hx711_data->gain_chan_a;

			ret = hx711_read(hx711_data);
			if (ret < 0)
				return ret;

			ret = hx711_wait_for_ready(hx711_data);
			if (ret)
				return ret;
		}
	} else {
		if (hx711_data->gain_set != 32) {
			hx711_data->gain_set = 32;

			ret = hx711_read(hx711_data);
			if (ret < 0)
				return ret;

			ret = hx711_wait_for_ready(hx711_data);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int hx711_reset_read(struct hx711_data *hx711_data, int chan)
{
	int ret;
	int val;

	/*
	 * hx711_reset() must be called from here
	 * because it could be calling hx711_read() by itself
	 */
	if (hx711_reset(hx711_data)) {
		dev_err(hx711_data->dev, "reset failed!");
		return -EIO;
	}

	ret = hx711_set_gain_for_channel(hx711_data, chan);
	if (ret < 0)
		return ret;

	val = hx711_read(hx711_data);

	return val;
}

static int hx711_read_raw(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				int *val, int *val2, long mask)
{
	struct hx711_data *hx711_data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&hx711_data->lock);

		*val = hx711_reset_read(hx711_data, chan->channel);

		mutex_unlock(&hx711_data->lock);

		if (*val < 0)
			return *val;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		mutex_lock(&hx711_data->lock);

		*val2 = hx711_get_gain_to_scale(hx711_data->gain_set);

		mutex_unlock(&hx711_data->lock);

		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int hx711_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val,
				int val2,
				long mask)
{
	struct hx711_data *hx711_data = iio_priv(indio_dev);
	int ret;
	int gain;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		/*
		 * a scale greater than 1 mV per LSB is not possible
		 * with the HX711, therefore val must be 0
		 */
		if (val != 0)
			return -EINVAL;

		mutex_lock(&hx711_data->lock);

		gain = hx711_get_scale_to_gain(val2);
		if (gain < 0) {
			mutex_unlock(&hx711_data->lock);
			return gain;
		}

		if (gain != hx711_data->gain_set) {
			hx711_data->gain_set = gain;
			if (gain != 32)
				hx711_data->gain_chan_a = gain;

			ret = hx711_read(hx711_data);
			if (ret < 0) {
				mutex_unlock(&hx711_data->lock);
				return ret;
			}
		}

		mutex_unlock(&hx711_data->lock);
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hx711_write_raw_get_fmt(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		long mask)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static irqreturn_t hx711_trigger(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct hx711_data *hx711_data = iio_priv(indio_dev);
	int i, j = 0;

	mutex_lock(&hx711_data->lock);

	memset(hx711_data->buffer, 0, sizeof(hx711_data->buffer));

	for (i = 0; i < indio_dev->masklength; i++) {
		if (!test_bit(i, indio_dev->active_scan_mask))
			continue;

		hx711_data->buffer[j] = hx711_reset_read(hx711_data,
					indio_dev->channels[i].channel);
		j++;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, hx711_data->buffer,
							pf->timestamp);

	mutex_unlock(&hx711_data->lock);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static ssize_t hx711_scale_available_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(attr);
	int channel = iio_attr->address;
	int i, len = 0;

	for (i = 0; i < HX711_GAIN_MAX; i++)
		if (hx711_gain_to_scale[i].channel == channel)
			len += sprintf(buf + len, "0.%09d ",
					hx711_gain_to_scale[i].scale);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEVICE_ATTR(in_voltage0_scale_available, S_IRUGO,
	hx711_scale_available_show, NULL, 0);

static IIO_DEVICE_ATTR(in_voltage1_scale_available, S_IRUGO,
	hx711_scale_available_show, NULL, 1);

static struct attribute *hx711_attributes[] = {
	&iio_dev_attr_in_voltage0_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage1_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group hx711_attribute_group = {
	.attrs = hx711_attributes,
};

static const struct iio_info hx711_iio_info = {
	.read_raw		= hx711_read_raw,
	.write_raw		= hx711_write_raw,
	.write_raw_get_fmt	= hx711_write_raw_get_fmt,
	.attrs			= &hx711_attribute_group,
};

static const struct iio_chan_spec hx711_chan_spec[] = {
	{
		.type = IIO_VOLTAGE,
		.channel = 0,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_VOLTAGE,
		.channel = 1,
		.indexed = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 24,
			.storagebits = 32,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static int hx711_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hx711_data *hx711_data;
	struct iio_dev *indio_dev;
	int ret;
	int i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct hx711_data));
	if (!indio_dev) {
		dev_err(dev, "failed to allocate IIO device\n");
		return -ENOMEM;
	}

	hx711_data = iio_priv(indio_dev);
	hx711_data->dev = dev;

	mutex_init(&hx711_data->lock);

	/*
	 * PD_SCK stands for power down and serial clock input of HX711
	 * in the driver it is an output
	 */
	hx711_data->gpiod_pd_sck = devm_gpiod_get(dev, "sck", GPIOD_OUT_LOW);
	if (IS_ERR(hx711_data->gpiod_pd_sck)) {
		dev_err(dev, "failed to get sck-gpiod: err=%ld\n",
					PTR_ERR(hx711_data->gpiod_pd_sck));
		return PTR_ERR(hx711_data->gpiod_pd_sck);
	}

	/*
	 * DOUT stands for serial data output of HX711
	 * for the driver it is an input
	 */
	hx711_data->gpiod_dout = devm_gpiod_get(dev, "dout", GPIOD_IN);
	if (IS_ERR(hx711_data->gpiod_dout)) {
		dev_err(dev, "failed to get dout-gpiod: err=%ld\n",
					PTR_ERR(hx711_data->gpiod_dout));
		return PTR_ERR(hx711_data->gpiod_dout);
	}

	hx711_data->reg_avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(hx711_data->reg_avdd))
		return PTR_ERR(hx711_data->reg_avdd);

	ret = regulator_enable(hx711_data->reg_avdd);
	if (ret < 0)
		return ret;

	/*
	 * with
	 * full scale differential input range: AVDD / GAIN
	 * full scale output data: 2^24
	 * we can say:
	 *     AVDD / GAIN = 2^24
	 * therefore:
	 *     1 LSB = AVDD / GAIN / 2^24
	 * AVDD is in uV, but we need 10^-9 mV
	 * approximately to fit into a 32 bit number:
	 * 1 LSB = (AVDD * 100) / GAIN / 1678 [10^-9 mV]
	 */
	ret = regulator_get_voltage(hx711_data->reg_avdd);
	if (ret < 0)
		goto error_regulator;

	/* we need 10^-9 mV */
	ret *= 100;

	for (i = 0; i < HX711_GAIN_MAX; i++)
		hx711_gain_to_scale[i].scale =
			ret / hx711_gain_to_scale[i].gain / 1678;

	hx711_data->gain_set = 128;
	hx711_data->gain_chan_a = 128;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = "hx711";
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &hx711_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = hx711_chan_spec;
	indio_dev->num_channels = ARRAY_SIZE(hx711_chan_spec);

	ret = iio_triggered_buffer_setup(indio_dev, iio_pollfunc_store_time,
							hx711_trigger, NULL);
	if (ret < 0) {
		dev_err(dev, "setup of iio triggered buffer failed\n");
		goto error_regulator;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(dev, "Couldn't register the device\n");
		goto error_buffer;
	}

	return 0;

error_buffer:
	iio_triggered_buffer_cleanup(indio_dev);

error_regulator:
	regulator_disable(hx711_data->reg_avdd);

	return ret;
}

static int hx711_remove(struct platform_device *pdev)
{
	struct hx711_data *hx711_data;
	struct iio_dev *indio_dev;

	indio_dev = platform_get_drvdata(pdev);
	hx711_data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	iio_triggered_buffer_cleanup(indio_dev);

	regulator_disable(hx711_data->reg_avdd);

	return 0;
}

static const struct of_device_id of_hx711_match[] = {
	{ .compatible = "avia,hx711", },
	{},
};

MODULE_DEVICE_TABLE(of, of_hx711_match);

static struct platform_driver hx711_driver = {
	.probe		= hx711_probe,
	.remove		= hx711_remove,
	.driver		= {
		.name		= "hx711-gpio",
		.of_match_table	= of_hx711_match,
	},
};

module_platform_driver(hx711_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("HX711 bitbanging driver - ADC for weight cells");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hx711-gpio");

