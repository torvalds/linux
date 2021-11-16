// SPDX-License-Identifier: GPL-2.0-only
/*
 * CM3605 Ambient Light and Proximity Sensor
 *
 * Copyright (C) 2016 Linaro Ltd.
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This hardware was found in the very first Nexus One handset from Google/HTC
 * and an early endavour into mobile light and proximity sensors.
 */

#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/consumer.h> /* To get our ADC channel */
#include <linux/iio/types.h> /* To deal with our ADC channel */
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/pm.h>

#define CM3605_PROX_CHANNEL 0
#define CM3605_ALS_CHANNEL 1
#define CM3605_AOUT_TYP_MAX_MV 1550
/* It should not go above 1.650V according to the data sheet */
#define CM3605_AOUT_MAX_MV 1650

/**
 * struct cm3605 - CM3605 state
 * @dev: pointer to parent device
 * @vdd: regulator controlling VDD
 * @aset: sleep enable GPIO, high = sleep
 * @aout: IIO ADC channel to convert the AOUT signal
 * @als_max: maximum LUX detection (depends on RSET)
 * @dir: proximity direction: start as FALLING
 * @led: trigger for the infrared LED used by the proximity sensor
 */
struct cm3605 {
	struct device *dev;
	struct regulator *vdd;
	struct gpio_desc *aset;
	struct iio_channel *aout;
	s32 als_max;
	enum iio_event_direction dir;
	struct led_trigger *led;
};

static irqreturn_t cm3605_prox_irq(int irq, void *d)
{
	struct iio_dev *indio_dev = d;
	struct cm3605 *cm3605 = iio_priv(indio_dev);
	u64 ev;

	ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, CM3605_PROX_CHANNEL,
				  IIO_EV_TYPE_THRESH, cm3605->dir);
	iio_push_event(indio_dev, ev, iio_get_time_ns(indio_dev));

	/* Invert the edge for each event */
	if (cm3605->dir == IIO_EV_DIR_RISING)
		cm3605->dir = IIO_EV_DIR_FALLING;
	else
		cm3605->dir = IIO_EV_DIR_RISING;

	return IRQ_HANDLED;
}

static int cm3605_get_lux(struct cm3605 *cm3605)
{
	int ret, res;
	s64 lux;

	ret = iio_read_channel_processed(cm3605->aout, &res);
	if (ret < 0)
		return ret;

	dev_dbg(cm3605->dev, "read %d mV from ADC\n", res);

	/*
	 * AOUT has an offset of ~30mV then linear at dark
	 * then goes from 2.54 up to 650 LUX yielding 1.55V
	 * (1550 mV) so scale the returned value to this interval
	 * using simple linear interpolation.
	 */
	if (res < 30)
		return 0;
	if (res > CM3605_AOUT_MAX_MV)
		dev_err(cm3605->dev, "device out of range\n");

	/* Remove bias */
	lux = res - 30;

	/* Linear interpolation between 0 and ALS typ max */
	lux *= cm3605->als_max;
	lux = div64_s64(lux, CM3605_AOUT_TYP_MAX_MV);

	return lux;
}

static int cm3605_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct cm3605 *cm3605 = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = cm3605_get_lux(cm3605);
			if (ret < 0)
				return ret;
			*val = ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info cm3605_info = {
	.read_raw = cm3605_read_raw,
};

static const struct iio_event_spec cm3605_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec cm3605_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.event_spec = cm3605_events,
		.num_event_specs = ARRAY_SIZE(cm3605_events),
	},
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.channel = CM3605_ALS_CHANNEL,
	},
};

static int cm3605_probe(struct platform_device *pdev)
{
	struct cm3605 *cm3605;
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	enum iio_chan_type ch_type;
	u32 rset;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*cm3605));
	if (!indio_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, indio_dev);

	cm3605 = iio_priv(indio_dev);
	cm3605->dev = dev;
	cm3605->dir = IIO_EV_DIR_FALLING;

	ret = of_property_read_u32(np, "capella,aset-resistance-ohms", &rset);
	if (ret) {
		dev_info(dev, "no RSET specified, assuming 100K\n");
		rset = 100000;
	}
	switch (rset) {
	case 50000:
		cm3605->als_max = 650;
		break;
	case 100000:
		cm3605->als_max = 300;
		break;
	case 300000:
		cm3605->als_max = 100;
		break;
	case 600000:
		cm3605->als_max = 50;
		break;
	default:
		dev_info(dev, "non-standard resistance\n");
		return -EINVAL;
	}

	cm3605->aout = devm_iio_channel_get(dev, "aout");
	if (IS_ERR(cm3605->aout)) {
		ret = PTR_ERR(cm3605->aout);
		ret = (ret == -ENODEV) ? -EPROBE_DEFER : ret;
		return dev_err_probe(dev, ret, "failed to get AOUT ADC channel\n");
	}
	ret = iio_get_channel_type(cm3605->aout, &ch_type);
	if (ret < 0)
		return ret;
	if (ch_type != IIO_VOLTAGE) {
		dev_err(dev, "wrong type of IIO channel specified for AOUT\n");
		return -EINVAL;
	}

	cm3605->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(cm3605->vdd))
		return dev_err_probe(dev, PTR_ERR(cm3605->vdd),
				     "failed to get VDD regulator\n");

	ret = regulator_enable(cm3605->vdd);
	if (ret) {
		dev_err(dev, "failed to enable VDD regulator\n");
		return ret;
	}

	cm3605->aset = devm_gpiod_get(dev, "aset", GPIOD_OUT_HIGH);
	if (IS_ERR(cm3605->aset)) {
		ret = dev_err_probe(dev, PTR_ERR(cm3605->aset), "no ASET GPIO\n");
		goto out_disable_vdd;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "failed to get irq\n");

	ret = devm_request_threaded_irq(dev, irq, cm3605_prox_irq,
					NULL, 0, "cm3605", indio_dev);
	if (ret) {
		dev_err(dev, "unable to request IRQ\n");
		goto out_disable_aset;
	}

	/* Just name the trigger the same as the driver */
	led_trigger_register_simple("cm3605", &cm3605->led);
	led_trigger_event(cm3605->led, LED_FULL);

	indio_dev->info = &cm3605_info;
	indio_dev->name = "cm3605";
	indio_dev->channels = cm3605_channels;
	indio_dev->num_channels = ARRAY_SIZE(cm3605_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto out_remove_trigger;
	dev_info(dev, "Capella Microsystems CM3605 enabled range 0..%d LUX\n",
		 cm3605->als_max);

	return 0;

out_remove_trigger:
	led_trigger_event(cm3605->led, LED_OFF);
	led_trigger_unregister_simple(cm3605->led);
out_disable_aset:
	gpiod_set_value_cansleep(cm3605->aset, 0);
out_disable_vdd:
	regulator_disable(cm3605->vdd);
	return ret;
}

static int cm3605_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct cm3605 *cm3605 = iio_priv(indio_dev);

	led_trigger_event(cm3605->led, LED_OFF);
	led_trigger_unregister_simple(cm3605->led);
	gpiod_set_value_cansleep(cm3605->aset, 0);
	iio_device_unregister(indio_dev);
	regulator_disable(cm3605->vdd);

	return 0;
}

static int __maybe_unused cm3605_pm_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct cm3605 *cm3605 = iio_priv(indio_dev);

	led_trigger_event(cm3605->led, LED_OFF);
	regulator_disable(cm3605->vdd);

	return 0;
}

static int __maybe_unused cm3605_pm_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct cm3605 *cm3605 = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(cm3605->vdd);
	if (ret)
		dev_err(dev, "failed to enable regulator in resume path\n");
	led_trigger_event(cm3605->led, LED_FULL);

	return 0;
}

static const struct dev_pm_ops cm3605_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cm3605_pm_suspend,
				cm3605_pm_resume)
};

static const struct of_device_id cm3605_of_match[] = {
	{.compatible = "capella,cm3605"},
	{ },
};
MODULE_DEVICE_TABLE(of, cm3605_of_match);

static struct platform_driver cm3605_driver = {
	.driver = {
		.name = "cm3605",
		.of_match_table = cm3605_of_match,
		.pm = &cm3605_dev_pm_ops,
	},
	.probe = cm3605_probe,
	.remove = cm3605_remove,
};
module_platform_driver(cm3605_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("CM3605 ambient light and proximity sensor driver");
MODULE_LICENSE("GPL");
