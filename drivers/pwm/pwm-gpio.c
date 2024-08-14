// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic software PWM for modulating GPIOs
 *
 * Copyright (C) 2020 Axis Communications AB
 * Copyright (C) 2020 Nicola Di Lieto
 * Copyright (C) 2024 Stefan Wahren
 * Copyright (C) 2024 Linus Walleij
 */

#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/hrtimer.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/types.h>

struct pwm_gpio {
	struct hrtimer gpio_timer;
	struct gpio_desc *gpio;
	struct pwm_state state;
	struct pwm_state next_state;

	/* Protect internal state between pwm_ops and hrtimer */
	spinlock_t lock;

	bool changing;
	bool running;
	bool level;
};

static void pwm_gpio_round(struct pwm_state *dest, const struct pwm_state *src)
{
	u64 dividend;
	u32 remainder;

	*dest = *src;

	/* Round down to hrtimer resolution */
	dividend = dest->period;
	remainder = do_div(dividend, hrtimer_resolution);
	dest->period -= remainder;

	dividend = dest->duty_cycle;
	remainder = do_div(dividend, hrtimer_resolution);
	dest->duty_cycle -= remainder;
}

static u64 pwm_gpio_toggle(struct pwm_gpio *gpwm, bool level)
{
	const struct pwm_state *state = &gpwm->state;
	bool invert = state->polarity == PWM_POLARITY_INVERSED;

	gpwm->level = level;
	gpiod_set_value(gpwm->gpio, gpwm->level ^ invert);

	if (!state->duty_cycle || state->duty_cycle == state->period) {
		gpwm->running = false;
		return 0;
	}

	gpwm->running = true;
	return level ? state->duty_cycle : state->period - state->duty_cycle;
}

static enum hrtimer_restart pwm_gpio_timer(struct hrtimer *gpio_timer)
{
	struct pwm_gpio *gpwm = container_of(gpio_timer, struct pwm_gpio,
					     gpio_timer);
	u64 next_toggle;
	bool new_level;

	guard(spinlock_irqsave)(&gpwm->lock);

	/* Apply new state at end of current period */
	if (!gpwm->level && gpwm->changing) {
		gpwm->changing = false;
		gpwm->state = gpwm->next_state;
		new_level = !!gpwm->state.duty_cycle;
	} else {
		new_level = !gpwm->level;
	}

	next_toggle = pwm_gpio_toggle(gpwm, new_level);
	if (next_toggle)
		hrtimer_forward(gpio_timer, hrtimer_get_expires(gpio_timer),
				ns_to_ktime(next_toggle));

	return next_toggle ? HRTIMER_RESTART : HRTIMER_NORESTART;
}

static int pwm_gpio_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	struct pwm_gpio *gpwm = pwmchip_get_drvdata(chip);
	bool invert = state->polarity == PWM_POLARITY_INVERSED;

	if (state->duty_cycle && state->duty_cycle < hrtimer_resolution)
		return -EINVAL;

	if (state->duty_cycle != state->period &&
	    (state->period - state->duty_cycle < hrtimer_resolution))
		return -EINVAL;

	if (!state->enabled) {
		hrtimer_cancel(&gpwm->gpio_timer);
	} else if (!gpwm->running) {
		int ret;

		/*
		 * This just enables the output, but pwm_gpio_toggle()
		 * really starts the duty cycle.
		 */
		ret = gpiod_direction_output(gpwm->gpio, invert);
		if (ret)
			return ret;
	}

	guard(spinlock_irqsave)(&gpwm->lock);

	if (!state->enabled) {
		pwm_gpio_round(&gpwm->state, state);
		gpwm->running = false;
		gpwm->changing = false;

		gpiod_set_value(gpwm->gpio, invert);
	} else if (gpwm->running) {
		pwm_gpio_round(&gpwm->next_state, state);
		gpwm->changing = true;
	} else {
		unsigned long next_toggle;

		pwm_gpio_round(&gpwm->state, state);
		gpwm->changing = false;

		next_toggle = pwm_gpio_toggle(gpwm, !!state->duty_cycle);
		if (next_toggle)
			hrtimer_start(&gpwm->gpio_timer, next_toggle,
				      HRTIMER_MODE_REL);
	}

	return 0;
}

static int pwm_gpio_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct pwm_gpio *gpwm = pwmchip_get_drvdata(chip);

	guard(spinlock_irqsave)(&gpwm->lock);

	if (gpwm->changing)
		*state = gpwm->next_state;
	else
		*state = gpwm->state;

	return 0;
}

static const struct pwm_ops pwm_gpio_ops = {
	.apply = pwm_gpio_apply,
	.get_state = pwm_gpio_get_state,
};

static void pwm_gpio_disable_hrtimer(void *data)
{
	struct pwm_gpio *gpwm = data;

	hrtimer_cancel(&gpwm->gpio_timer);
}

static int pwm_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct pwm_gpio *gpwm;
	int ret;

	chip = devm_pwmchip_alloc(dev, 1, sizeof(*gpwm));
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	gpwm = pwmchip_get_drvdata(chip);

	spin_lock_init(&gpwm->lock);

	gpwm->gpio = devm_gpiod_get(dev, NULL, GPIOD_ASIS);
	if (IS_ERR(gpwm->gpio))
		return dev_err_probe(dev, PTR_ERR(gpwm->gpio),
				     "%pfw: could not get gpio\n",
				     dev_fwnode(dev));

	if (gpiod_cansleep(gpwm->gpio))
		return dev_err_probe(dev, -EINVAL,
				     "%pfw: sleeping GPIO not supported\n",
				     dev_fwnode(dev));

	chip->ops = &pwm_gpio_ops;
	chip->atomic = true;

	hrtimer_init(&gpwm->gpio_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ret = devm_add_action_or_reset(dev, pwm_gpio_disable_hrtimer, gpwm);
	if (ret)
		return ret;

	gpwm->gpio_timer.function = pwm_gpio_timer;

	ret = pwmchip_add(chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "could not add pwmchip\n");

	return 0;
}

static const struct of_device_id pwm_gpio_dt_ids[] = {
	{ .compatible = "pwm-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_gpio_dt_ids);

static struct platform_driver pwm_gpio_driver = {
	.driver = {
		.name = "pwm-gpio",
		.of_match_table = pwm_gpio_dt_ids,
	},
	.probe = pwm_gpio_probe,
};
module_platform_driver(pwm_gpio_driver);

MODULE_DESCRIPTION("PWM GPIO driver");
MODULE_AUTHOR("Vincent Whitchurch");
MODULE_LICENSE("GPL");
