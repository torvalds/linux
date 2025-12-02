// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SRF04: ultrasonic sensor for distance measuring by using GPIOs
 *
 * Copyright (c) 2017 Andreas Klinger <ak@it-klinger.de>
 *
 * For details about the device see:
 * https://www.robot-electronics.co.uk/htm/srf04tech.htm
 *
 * the measurement cycle as timing diagram looks like:
 *
 *          +---+
 * GPIO     |   |
 * trig:  --+   +------------------------------------------------------
 *          ^   ^
 *          |<->|
 *         udelay(trigger_pulse_us)
 *
 * ultra           +-+ +-+ +-+
 * sonic           | | | | | |
 * burst: ---------+ +-+ +-+ +-----------------------------------------
 *                           .
 * ultra                     .              +-+ +-+ +-+
 * sonic                     .              | | | | | |
 * echo:  ----------------------------------+ +-+ +-+ +----------------
 *                           .                        .
 *                           +------------------------+
 * GPIO                      |                        |
 * echo:  -------------------+                        +---------------
 *                           ^                        ^
 *                           interrupt                interrupt
 *                           (ts_rising)              (ts_falling)
 *                           |<---------------------->|
 *                              pulse time measured
 *                              --> one round trip of ultra sonic waves
 */
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

struct srf04_cfg {
	unsigned long trigger_pulse_us;
};

struct srf04_data {
	struct device		*dev;
	struct gpio_desc	*gpiod_trig;
	struct gpio_desc	*gpiod_echo;
	struct gpio_desc	*gpiod_power;
	struct mutex		lock;
	int			irqnr;
	ktime_t			ts_rising;
	ktime_t			ts_falling;
	struct completion	rising;
	struct completion	falling;
	const struct srf04_cfg	*cfg;
	int			startup_time_ms;
};

static const struct srf04_cfg srf04_cfg = {
	.trigger_pulse_us = 10,
};

static const struct srf04_cfg mb_lv_cfg = {
	.trigger_pulse_us = 20,
};

static irqreturn_t srf04_handle_irq(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct srf04_data *data = iio_priv(indio_dev);
	ktime_t now = ktime_get();

	if (gpiod_get_value(data->gpiod_echo)) {
		data->ts_rising = now;
		complete(&data->rising);
	} else {
		data->ts_falling = now;
		complete(&data->falling);
	}

	return IRQ_HANDLED;
}

static int srf04_read(struct srf04_data *data)
{
	int ret;
	ktime_t ktime_dt;
	u64 dt_ns;
	u32 time_ns, distance_mm;

	if (data->gpiod_power) {
		ret = pm_runtime_resume_and_get(data->dev);
		if (ret < 0)
			return ret;
	}
	/*
	 * just one read-echo-cycle can take place at a time
	 * ==> lock against concurrent reading calls
	 */
	mutex_lock(&data->lock);

	reinit_completion(&data->rising);
	reinit_completion(&data->falling);

	gpiod_set_value(data->gpiod_trig, 1);
	udelay(data->cfg->trigger_pulse_us);
	gpiod_set_value(data->gpiod_trig, 0);

	if (data->gpiod_power)
		pm_runtime_put_autosuspend(data->dev);

	/* it should not take more than 20 ms until echo is rising */
	ret = wait_for_completion_killable_timeout(&data->rising, HZ/50);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	} else if (ret == 0) {
		mutex_unlock(&data->lock);
		return -ETIMEDOUT;
	}

	/* it cannot take more than 50 ms until echo is falling */
	ret = wait_for_completion_killable_timeout(&data->falling, HZ/20);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	} else if (ret == 0) {
		mutex_unlock(&data->lock);
		return -ETIMEDOUT;
	}

	ktime_dt = ktime_sub(data->ts_falling, data->ts_rising);

	mutex_unlock(&data->lock);

	dt_ns = ktime_to_ns(ktime_dt);
	/*
	 * measuring more than 6,45 meters is beyond the capabilities of
	 * the supported sensors
	 * ==> filter out invalid results for not measuring echos of
	 *     another us sensor
	 *
	 * formula:
	 *         distance     6,45 * 2 m
	 * time = ---------- = ------------ = 40438871 ns
	 *          speed         319 m/s
	 *
	 * using a minimum speed at -20 °C of 319 m/s
	 */
	if (dt_ns > 40438871)
		return -EIO;

	time_ns = dt_ns;

	/*
	 * the speed as function of the temperature is approximately:
	 *
	 * speed = 331,5 + 0,6 * Temp
	 *   with Temp in °C
	 *   and speed in m/s
	 *
	 * use 343,5 m/s as ultrasonic speed at 20 °C here in absence of the
	 * temperature
	 *
	 * therefore:
	 *             time     343,5     time * 106
	 * distance = ------ * ------- = ------------
	 *             10^6         2         617176
	 *   with time in ns
	 *   and distance in mm (one way)
	 *
	 * because we limit to 6,45 meters the multiplication with 106 just
	 * fits into 32 bit
	 */
	distance_mm = time_ns * 106 / 617176;

	return distance_mm;
}

static int srf04_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long info)
{
	struct srf04_data *data = iio_priv(indio_dev);
	int ret;

	if (channel->type != IIO_DISTANCE)
		return -EINVAL;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = srf04_read(data);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * theoretical maximum resolution is 3 mm
		 * 1 LSB is 1 mm
		 */
		*val = 0;
		*val2 = 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info srf04_iio_info = {
	.read_raw		= srf04_read_raw,
};

static const struct iio_chan_spec srf04_chan_spec[] = {
	{
		.type = IIO_DISTANCE,
		.info_mask_separate =
				BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct of_device_id of_srf04_match[] = {
	{ .compatible = "devantech,srf04", .data = &srf04_cfg },
	{ .compatible = "maxbotix,mb1000", .data = &mb_lv_cfg },
	{ .compatible = "maxbotix,mb1010", .data = &mb_lv_cfg },
	{ .compatible = "maxbotix,mb1020", .data = &mb_lv_cfg },
	{ .compatible = "maxbotix,mb1030", .data = &mb_lv_cfg },
	{ .compatible = "maxbotix,mb1040", .data = &mb_lv_cfg },
	{ }
};

MODULE_DEVICE_TABLE(of, of_srf04_match);

static int srf04_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct srf04_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct srf04_data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = dev;
	data->cfg = device_get_match_data(dev);

	mutex_init(&data->lock);
	init_completion(&data->rising);
	init_completion(&data->falling);

	data->gpiod_trig = devm_gpiod_get(dev, "trig", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_trig)) {
		dev_err(dev, "failed to get trig-gpios: err=%ld\n",
					PTR_ERR(data->gpiod_trig));
		return PTR_ERR(data->gpiod_trig);
	}

	data->gpiod_echo = devm_gpiod_get(dev, "echo", GPIOD_IN);
	if (IS_ERR(data->gpiod_echo)) {
		dev_err(dev, "failed to get echo-gpios: err=%ld\n",
					PTR_ERR(data->gpiod_echo));
		return PTR_ERR(data->gpiod_echo);
	}

	data->gpiod_power = devm_gpiod_get_optional(dev, "power",
								GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_power)) {
		dev_err(dev, "failed to get power-gpios: err=%ld\n",
						PTR_ERR(data->gpiod_power));
		return PTR_ERR(data->gpiod_power);
	}
	if (data->gpiod_power) {
		data->startup_time_ms = 100;
		device_property_read_u32(dev, "startup-time-ms", &data->startup_time_ms);
		dev_dbg(dev, "using power gpio: startup-time-ms=%d\n",
							data->startup_time_ms);
	}

	if (gpiod_cansleep(data->gpiod_echo)) {
		dev_err(data->dev, "cansleep-GPIOs not supported\n");
		return -ENODEV;
	}

	data->irqnr = gpiod_to_irq(data->gpiod_echo);
	if (data->irqnr < 0) {
		dev_err(data->dev, "gpiod_to_irq: %d\n", data->irqnr);
		return data->irqnr;
	}

	ret = devm_request_irq(dev, data->irqnr, srf04_handle_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			pdev->name, indio_dev);
	if (ret < 0) {
		dev_err(data->dev, "request_irq: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = "srf04";
	indio_dev->info = &srf04_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = srf04_chan_spec;
	indio_dev->num_channels = ARRAY_SIZE(srf04_chan_spec);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(data->dev, "iio_device_register: %d\n", ret);
		return ret;
	}

	if (data->gpiod_power) {
		pm_runtime_set_autosuspend_delay(data->dev, 1000);
		pm_runtime_use_autosuspend(data->dev);

		ret = pm_runtime_set_active(data->dev);
		if (ret) {
			dev_err(data->dev, "pm_runtime_set_active: %d\n", ret);
			iio_device_unregister(indio_dev);
		}

		pm_runtime_enable(data->dev);
		pm_runtime_idle(data->dev);
	}

	return ret;
}

static void srf04_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct srf04_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	if (data->gpiod_power) {
		pm_runtime_disable(data->dev);
		pm_runtime_set_suspended(data->dev);
	}
}

static int  srf04_pm_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
						struct platform_device, dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct srf04_data *data = iio_priv(indio_dev);

	gpiod_set_value(data->gpiod_power, 0);

	return 0;
}

static int srf04_pm_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
						struct platform_device, dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct srf04_data *data = iio_priv(indio_dev);

	gpiod_set_value(data->gpiod_power, 1);
	msleep(data->startup_time_ms);

	return 0;
}

static const struct dev_pm_ops srf04_pm_ops = {
	RUNTIME_PM_OPS(srf04_pm_runtime_suspend,
		       srf04_pm_runtime_resume, NULL)
};

static struct platform_driver srf04_driver = {
	.probe		= srf04_probe,
	.remove		= srf04_remove,
	.driver		= {
		.name		= "srf04-gpio",
		.of_match_table	= of_srf04_match,
		.pm		= pm_ptr(&srf04_pm_ops),
	},
};

module_platform_driver(srf04_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("SRF04 ultrasonic sensor for distance measuring using GPIOs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:srf04");
