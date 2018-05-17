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

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>

struct gpio_pwm_chip {
	struct pwm_chip chip;
	struct hrtimer timer;
	struct gpio_desc *gpiod;
	unsigned int on_time;
	unsigned int off_time;
	bool pin_on;
};

static inline struct gpio_pwm_chip *to_gpio_pwm_chip(struct pwm_chip *c)
{
	return container_of(c, struct gpio_pwm_chip, chip);
}

static void gpio_pwm_off(struct gpio_pwm_chip *pc)
{
	enum pwm_polarity polarity = pwm_get_polarity(pc->chip.pwms);

	gpiod_set_value(pc->gpiod, polarity ? 1 : 0);
}

static void gpio_pwm_on(struct gpio_pwm_chip *pc)
{
	enum pwm_polarity polarity = pwm_get_polarity(pc->chip.pwms);

	gpiod_set_value(pc->gpiod, polarity ? 0 : 1);
}

static enum hrtimer_restart gpio_pwm_timer(struct hrtimer *timer)
{
	struct gpio_pwm_chip *pc = container_of(timer,
						struct gpio_pwm_chip,
						timer);
	if (!pwm_is_enabled(pc->chip.pwms)) {
		gpio_pwm_off(pc);
		pc->pin_on = false;
		return HRTIMER_NORESTART;
	}

	if (!pc->pin_on) {
		hrtimer_forward_now(&pc->timer, ns_to_ktime(pc->on_time));

		if (pc->on_time) {
			gpio_pwm_on(pc);
			pc->pin_on = true;
		}

	} else {
		hrtimer_forward_now(&pc->timer, ns_to_ktime(pc->off_time));

		if (pc->off_time) {
			gpio_pwm_off(pc);
			pc->pin_on = false;
		}
	}

	return HRTIMER_RESTART;
}

static int gpio_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct gpio_pwm_chip *pc = to_gpio_pwm_chip(chip);

	pc->on_time = duty_ns;
	pc->off_time = period_ns - duty_ns;

	return 0;
}

static int gpio_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				 enum pwm_polarity polarity)
{
	return 0;
}

static int gpio_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gpio_pwm_chip *pc = to_gpio_pwm_chip(chip);

	if (pwm_is_enabled(pc->chip.pwms))
		return -EBUSY;

	if (pc->off_time) {
		hrtimer_start(&pc->timer, ktime_set(0, 0), HRTIMER_MODE_REL);
	} else {
		if (pc->on_time)
			gpio_pwm_on(pc);
		else
			gpio_pwm_off(pc);
	}

	return 0;
}

static void gpio_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gpio_pwm_chip *pc = to_gpio_pwm_chip(chip);

	if (!pc->off_time)
		gpio_pwm_off(pc);
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
	struct gpio_pwm_chip *pc;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &gpio_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = 1;
	pc->chip.of_xlate = of_pwm_xlate_with_flags;
	pc->chip.of_pwm_n_cells = 3;
	pc->chip.can_sleep = true;

	pc->gpiod = devm_gpiod_get(&pdev->dev, "pwm", GPIOD_OUT_LOW);

	if (IS_ERR(pc->gpiod))
		return PTR_ERR(pc->gpiod);

	hrtimer_init(&pc->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pc->timer.function = &gpio_pwm_timer;
	pc->pin_on = false;

	if (!hrtimer_is_hres_active(&pc->timer))
		dev_warn(&pdev->dev, "HR timer unavailable, restricting to "
				     "low resolution\n");

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pc);
	return 0;
}

static int gpio_pwm_remove(struct platform_device *pdev)
{
	struct gpio_pwm_chip *pc = platform_get_drvdata(pdev);

	hrtimer_cancel(&pc->timer);
	return pwmchip_remove(&pc->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id gpio_pwm_of_match[] = {
	{ .compatible = "pwm-gpio", },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_pwm_of_match);
#endif

static struct platform_driver gpio_pwm_driver = {
	.probe = gpio_pwm_probe,
	.remove = gpio_pwm_remove,
	.driver = {
		.name = "pwm-gpio",
		.of_match_table = of_match_ptr(gpio_pwm_of_match),
	},
};
module_platform_driver(gpio_pwm_driver);

MODULE_AUTHOR("Olliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("Generic GPIO bit-banged PWM driver");
MODULE_LICENSE("GPL");
