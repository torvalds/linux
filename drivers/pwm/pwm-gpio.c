/*
 * Copyright (c) 2015 Olliver Schinagl <oliver@schinagl.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver adds a high-resolution timer based PWM driver. Since this is a
 * bit-banged driver, accuracy will always depend on a lot of factors, such as
 * GPIO toggle speed and system load.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#define DRV_NAME "pwm-gpio"

struct gpio_pwm_data {
	struct hrtimer timer;
	struct gpio_desc *gpiod;
	bool polarity;
	bool pin_on;
	int on_time;
	int off_time;
	bool run;
};

struct gpio_pwm_chip {
	struct pwm_chip chip;
};

static void gpio_pwm_off(struct gpio_pwm_data *gpio_data)
{
	gpiod_set_value_cansleep(gpio_data->gpiod, gpio_data->polarity ? 0 : 1);
}

static void gpio_pwm_on(struct gpio_pwm_data *gpio_data)
{
	gpiod_set_value_cansleep(gpio_data->gpiod, gpio_data->polarity ? 1 : 0);
}

enum hrtimer_restart gpio_pwm_timer(struct hrtimer *timer)
{
	struct gpio_pwm_data *gpio_data = container_of(timer,
						      struct gpio_pwm_data,
						      timer);
	if (!gpio_data->run) {
		gpio_pwm_off(gpio_data);
		gpio_data->pin_on = false;
		return HRTIMER_NORESTART;
	}

	if (!gpio_data->pin_on) {
		hrtimer_forward_now(&gpio_data->timer,
				    ns_to_ktime(gpio_data->on_time));
		gpio_pwm_on(gpio_data);
		gpio_data->pin_on = true;
	} else {
		hrtimer_forward_now(&gpio_data->timer,
				    ns_to_ktime(gpio_data->off_time));
		gpio_pwm_off(gpio_data);
		gpio_data->pin_on = false;
	}

	return HRTIMER_RESTART;
}

static int gpio_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct gpio_pwm_data *gpio_data = pwm_get_chip_data(pwm);

	gpio_data->on_time = duty_ns;
	gpio_data->off_time = period_ns - duty_ns;

	return 0;
}

static int gpio_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				 enum pwm_polarity polarity)
{
	struct gpio_pwm_data *gpio_data = pwm_get_chip_data(pwm);

	gpio_data->polarity = (polarity != PWM_POLARITY_NORMAL) ? true : false;

	return 0;
}

static int gpio_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gpio_pwm_data *gpio_data = pwm_get_chip_data(pwm);

	if (gpio_data->run)
		return -EBUSY;

	gpio_data->run = true;
	if (gpio_data->off_time) {
		hrtimer_start(&gpio_data->timer, ktime_set(0, 0),
			      HRTIMER_MODE_REL);
	} else {
		if (gpio_data->on_time)
			gpio_pwm_on(gpio_data);
		else
			gpio_pwm_off(gpio_data);
	}

	return 0;
}

static void gpio_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gpio_pwm_data *gpio_data = pwm_get_chip_data(pwm);

	gpio_data->run = false;
	if (!gpio_data->off_time)
		gpio_pwm_off(gpio_data);
}

static const struct pwm_ops gpio_pwm_ops = {
	.config = gpio_pwm_config,
	.set_polarity = gpio_pwm_set_polarity,
	.enable = gpio_pwm_enable,
	.disable = gpio_pwm_disable,
	.owner = THIS_MODULE,
};

static int gpio_pwm_probe(struct platform_device *pdev)
{
	int ret;
	struct gpio_pwm_chip *gpio_chip;
	int npwm, i;
	int hrtimer = 0;

	npwm = of_gpio_named_count(pdev->dev.of_node, "pwm-gpios");
	if (npwm < 1)
		return -ENODEV;

	gpio_chip = devm_kzalloc(&pdev->dev, sizeof(*gpio_chip), GFP_KERNEL);
	if (!gpio_chip)
		return -ENOMEM;

	gpio_chip->chip.dev = &pdev->dev;
	gpio_chip->chip.ops = &gpio_pwm_ops;
	gpio_chip->chip.base = -1;
	gpio_chip->chip.npwm = npwm;
	gpio_chip->chip.of_xlate = of_pwm_xlate_with_flags;
	gpio_chip->chip.of_pwm_n_cells = 3;
	gpio_chip->chip.can_sleep = true;

	ret = pwmchip_add(&gpio_chip->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}

	for (i = 0; i < npwm; i++) {
		struct gpio_desc *gpiod;
		struct gpio_pwm_data *gpio_data;

		gpiod = devm_gpiod_get_index(&pdev->dev, "pwm", i,
					     GPIOD_OUT_LOW);
		if (IS_ERR(gpiod)) {
			int error;

			error = PTR_ERR(gpiod);
			if (error != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"failed to get gpio flags, error: %d\n",
					error);
			return error;
		}

		gpio_data = devm_kzalloc(&pdev->dev, sizeof(*gpio_data),
					 GFP_KERNEL);

		hrtimer_init(&gpio_data->timer,
			     CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		gpio_data->timer.function = &gpio_pwm_timer;
		gpio_data->gpiod = gpiod;
		gpio_data->pin_on = false;
		gpio_data->run = false;

		if (hrtimer_is_hres_active(&gpio_data->timer))
			hrtimer++;

		pwm_set_chip_data(&gpio_chip->chip.pwms[i], gpio_data);
	}
	if (!hrtimer) {
		dev_warn(&pdev->dev, "unable to use High-Resolution timer,");
		dev_warn(&pdev->dev, "%s is restricted to low resolution.",
			 DRV_NAME);
	}

	platform_set_drvdata(pdev, gpio_chip);

	dev_info(&pdev->dev, "%d gpio pwms loaded\n", npwm);

	return 0;
}

static int gpio_pwm_remove(struct platform_device *pdev)
{
	struct gpio_pwm_chip *gpio_chip;
	int i;

	gpio_chip = platform_get_drvdata(pdev);
	for (i = 0; i < gpio_chip->chip.npwm; i++) {
		struct gpio_pwm_data *gpio_data;

		gpio_data = pwm_get_chip_data(&gpio_chip->chip.pwms[i]);

		hrtimer_cancel(&gpio_data->timer);
	}

	return pwmchip_remove(&gpio_chip->chip);
}

static const struct of_device_id gpio_pwm_of_match[] = {
	{ .compatible = DRV_NAME, },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, gpio_pwm_of_match);

static struct platform_driver gpio_pwm_driver = {
	.probe = gpio_pwm_probe,
	.remove = gpio_pwm_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(gpio_pwm_of_match),
	},
};
module_platform_driver(gpio_pwm_driver);

MODULE_AUTHOR("Olliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("Generic GPIO bit-banged PWM driver");
MODULE_LICENSE("GPL");
