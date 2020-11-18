// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PING: ultrasonic sensor for distance measuring by using only one GPIOs
 *
 * Copyright (c) 2019 Andreas Klinger <ak@it-klinger.de>
 *
 * For details about the devices see:
 * http://parallax.com/sites/default/files/downloads/28041-LaserPING-2m-Rangefinder-Guide.pdf
 * http://parallax.com/sites/default/files/downloads/28015-PING-Documentation-v1.6.pdf
 *
 * the measurement cycle as timing diagram looks like:
 *
 * GPIO      ___              ________________________
 * ping:  __/   \____________/                        \________________
 *          ^   ^            ^                        ^
 *          |<->|            interrupt                interrupt
 *         udelay(5)         (ts_rising)              (ts_falling)
 *                           |<---------------------->|
 *                           .  pulse time measured   .
 *                           .  --> one round trip of ultra sonic waves
 * ultra                     .                        .
 * sonic            _   _   _.                        .
 * burst: _________/ \_/ \_/ \_________________________________________
 *                                                    .
 * ultra                                              .
 * sonic                                     _   _   _.
 * echo:  __________________________________/ \_/ \_/ \________________
 */
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

struct ping_cfg {
	unsigned long	trigger_pulse_us;	/* length of trigger pulse */
	int		laserping_error;	/* support error code in */
						/*   pulse width of laser */
						/*   ping sensors */
	s64		timeout_ns;		/* timeout in ns */
};

struct ping_data {
	struct device		*dev;
	struct gpio_desc	*gpiod_ping;
	struct mutex		lock;
	int			irqnr;
	ktime_t			ts_rising;
	ktime_t			ts_falling;
	struct completion	rising;
	struct completion	falling;
	const struct ping_cfg	*cfg;
};

static const struct ping_cfg pa_ping_cfg = {
	.trigger_pulse_us	= 5,
	.laserping_error	= 0,
	.timeout_ns		= 18500000,	/* 3 meters */
};

static const struct ping_cfg pa_laser_ping_cfg = {
	.trigger_pulse_us	= 5,
	.laserping_error	= 1,
	.timeout_ns		= 15500000,	/* 2 meters plus error codes */
};

static irqreturn_t ping_handle_irq(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ping_data *data = iio_priv(indio_dev);
	ktime_t now = ktime_get();

	if (gpiod_get_value(data->gpiod_ping)) {
		data->ts_rising = now;
		complete(&data->rising);
	} else {
		data->ts_falling = now;
		complete(&data->falling);
	}

	return IRQ_HANDLED;
}

static int ping_read(struct iio_dev *indio_dev)
{
	struct ping_data *data = iio_priv(indio_dev);
	int ret;
	ktime_t ktime_dt;
	s64 dt_ns;
	u32 time_ns, distance_mm;
	struct platform_device *pdev = to_platform_device(data->dev);

	/*
	 * just one read-echo-cycle can take place at a time
	 * ==> lock against concurrent reading calls
	 */
	mutex_lock(&data->lock);

	reinit_completion(&data->rising);
	reinit_completion(&data->falling);

	gpiod_set_value(data->gpiod_ping, 1);
	udelay(data->cfg->trigger_pulse_us);
	gpiod_set_value(data->gpiod_ping, 0);

	ret = gpiod_direction_input(data->gpiod_ping);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	}

	data->irqnr = gpiod_to_irq(data->gpiod_ping);
	if (data->irqnr < 0) {
		dev_err(data->dev, "gpiod_to_irq: %d\n", data->irqnr);
		mutex_unlock(&data->lock);
		return data->irqnr;
	}

	ret = request_irq(data->irqnr, ping_handle_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
							pdev->name, indio_dev);
	if (ret < 0) {
		dev_err(data->dev, "request_irq: %d\n", ret);
		mutex_unlock(&data->lock);
		return ret;
	}

	/* it should not take more than 20 ms until echo is rising */
	ret = wait_for_completion_killable_timeout(&data->rising, HZ/50);
	if (ret < 0)
		goto err_reset_direction;
	else if (ret == 0) {
		ret = -ETIMEDOUT;
		goto err_reset_direction;
	}

	/* it cannot take more than 50 ms until echo is falling */
	ret = wait_for_completion_killable_timeout(&data->falling, HZ/20);
	if (ret < 0)
		goto err_reset_direction;
	else if (ret == 0) {
		ret = -ETIMEDOUT;
		goto err_reset_direction;
	}

	ktime_dt = ktime_sub(data->ts_falling, data->ts_rising);

	free_irq(data->irqnr, indio_dev);

	ret = gpiod_direction_output(data->gpiod_ping, GPIOD_OUT_LOW);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return ret;
	}

	mutex_unlock(&data->lock);

	dt_ns = ktime_to_ns(ktime_dt);
	if (dt_ns > data->cfg->timeout_ns) {
		dev_dbg(data->dev, "distance out of range: dt=%lldns\n",
								dt_ns);
		return -EIO;
	}

	time_ns = dt_ns;

	/*
	 * read error code of laser ping sensor and give users chance to
	 * figure out error by using dynamic debuggging
	 */
	if (data->cfg->laserping_error) {
		if ((time_ns > 12500000) && (time_ns <= 13500000)) {
			dev_dbg(data->dev, "target too close or to far\n");
			return -EIO;
		}
		if ((time_ns > 13500000) && (time_ns <= 14500000)) {
			dev_dbg(data->dev, "internal sensor error\n");
			return -EIO;
		}
		if ((time_ns > 14500000) && (time_ns <= 15500000)) {
			dev_dbg(data->dev, "internal sensor timeout\n");
			return -EIO;
		}
	}

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
	 *             time     343,5     time * 232
	 * distance = ------ * ------- = ------------
	 *             10^6         2        1350800
	 *   with time in ns
	 *   and distance in mm (one way)
	 *
	 * because we limit to 3 meters the multiplication with 232 just
	 * fits into 32 bit
	 */
	distance_mm = time_ns * 232 / 1350800;

	return distance_mm;

err_reset_direction:
	free_irq(data->irqnr, indio_dev);
	mutex_unlock(&data->lock);

	if (gpiod_direction_output(data->gpiod_ping, GPIOD_OUT_LOW))
		dev_dbg(data->dev, "error in gpiod_direction_output\n");
	return ret;
}

static int ping_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long info)
{
	int ret;

	if (channel->type != IIO_DISTANCE)
		return -EINVAL;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = ping_read(indio_dev);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * maximum resolution in datasheet is 1 mm
		 * 1 LSB is 1 mm
		 */
		*val = 0;
		*val2 = 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ping_iio_info = {
	.read_raw		= ping_read_raw,
};

static const struct iio_chan_spec ping_chan_spec[] = {
	{
		.type = IIO_DISTANCE,
		.info_mask_separate =
				BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct of_device_id of_ping_match[] = {
	{ .compatible = "parallax,ping", .data = &pa_ping_cfg},
	{ .compatible = "parallax,laserping", .data = &pa_laser_ping_cfg},
	{},
};

MODULE_DEVICE_TABLE(of, of_ping_match);

static int ping_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ping_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct ping_data));
	if (!indio_dev) {
		dev_err(dev, "failed to allocate IIO device\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->dev = dev;
	data->cfg = of_device_get_match_data(dev);

	mutex_init(&data->lock);
	init_completion(&data->rising);
	init_completion(&data->falling);

	data->gpiod_ping = devm_gpiod_get(dev, "ping", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_ping)) {
		dev_err(dev, "failed to get ping-gpios: err=%ld\n",
						PTR_ERR(data->gpiod_ping));
		return PTR_ERR(data->gpiod_ping);
	}

	if (gpiod_cansleep(data->gpiod_ping)) {
		dev_err(data->dev, "cansleep-GPIOs not supported\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = "ping";
	indio_dev->info = &ping_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ping_chan_spec;
	indio_dev->num_channels = ARRAY_SIZE(ping_chan_spec);

	return devm_iio_device_register(dev, indio_dev);
}

static struct platform_driver ping_driver = {
	.probe		= ping_probe,
	.driver		= {
		.name		= "ping-gpio",
		.of_match_table	= of_ping_match,
	},
};

module_platform_driver(ping_driver);

MODULE_AUTHOR("Andreas Klinger <ak@it-klinger.de>");
MODULE_DESCRIPTION("PING sensors for distance measuring using one GPIOs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ping");
