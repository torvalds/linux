/*
 * Blackfin Pulse Width Modulation (PWM) core
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#include <asm/gptimers.h>
#include <asm/portmux.h>

struct bfin_pwm_chip {
	struct pwm_chip chip;
};

struct bfin_pwm {
	unsigned short pin;
};

static const unsigned short pwm_to_gptimer_per[] = {
	P_TMR0, P_TMR1, P_TMR2, P_TMR3, P_TMR4, P_TMR5,
	P_TMR6, P_TMR7, P_TMR8, P_TMR9, P_TMR10, P_TMR11,
};

static int bfin_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bfin_pwm *priv;
	int ret;

	if (pwm->hwpwm >= ARRAY_SIZE(pwm_to_gptimer_per))
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pin = pwm_to_gptimer_per[pwm->hwpwm];

	ret = peripheral_request(priv->pin, NULL);
	if (ret) {
		kfree(priv);
		return ret;
	}

	pwm_set_chip_data(pwm, priv);

	return 0;
}

static void bfin_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bfin_pwm *priv = pwm_get_chip_data(pwm);

	if (priv) {
		peripheral_free(priv->pin);
		kfree(priv);
	}
}

static int bfin_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	struct bfin_pwm *priv = pwm_get_chip_data(pwm);
	unsigned long period, duty;
	unsigned long long val;

	if (duty_ns < 0 || duty_ns > period_ns)
		return -EINVAL;

	val = (unsigned long long)get_sclk() * period_ns;
	do_div(val, NSEC_PER_SEC);
	period = val;

	val = (unsigned long long)period * duty_ns;
	do_div(val, period_ns);
	duty = period - val;

	if (duty >= period)
		duty = period - 1;

	set_gptimer_config(priv->pin, TIMER_MODE_PWM | TIMER_PERIOD_CNT);
	set_gptimer_pwidth(priv->pin, duty);
	set_gptimer_period(priv->pin, period);

	return 0;
}

static int bfin_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bfin_pwm *priv = pwm_get_chip_data(pwm);

	enable_gptimer(priv->pin);

	return 0;
}

static void bfin_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct bfin_pwm *priv = pwm_get_chip_data(pwm);

	disable_gptimer(priv->pin);
}

static struct pwm_ops bfin_pwm_ops = {
	.request = bfin_pwm_request,
	.free = bfin_pwm_free,
	.config = bfin_pwm_config,
	.enable = bfin_pwm_enable,
	.disable = bfin_pwm_disable,
	.owner = THIS_MODULE,
};

static int bfin_pwm_probe(struct platform_device *pdev)
{
	struct bfin_pwm_chip *pwm;
	int ret;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, pwm);

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &bfin_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 12;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int __devexit bfin_pwm_remove(struct platform_device *pdev)
{
	struct bfin_pwm_chip *pwm = platform_get_drvdata(pdev);

	pwmchip_remove(&pwm->chip);

	return 0;
}

static struct platform_driver bfin_pwm_driver = {
	.driver = {
		.name = "bfin-pwm",
	},
	.probe = bfin_pwm_probe,
	.remove = __devexit_p(bfin_pwm_remove),
};

module_platform_driver(bfin_pwm_driver);

MODULE_LICENSE("GPL");
