// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PWM framework driver for Cirrus Logic EP93xx
 *
 * Copyright (c) 2009        Matthieu Crapet <mcrapet@gmail.com>
 * Copyright (c) 2009, 2013  H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * EP9301/02 have only one channel:
 *   platform device ep93xx-pwm.1 - PWMOUT1 (EGPIO14)
 *
 * EP9307 has only one channel:
 *   platform device ep93xx-pwm.0 - PWMOUT
 *
 * EP9312/15 have two channels:
 *   platform device ep93xx-pwm.0 - PWMOUT
 *   platform device ep93xx-pwm.1 - PWMOUT1 (EGPIO14)
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pwm.h>

#include <asm/div64.h>

#include <linux/soc/cirrus/ep93xx.h>	/* for ep93xx_pwm_{acquire,release}_gpio() */

#define EP93XX_PWMx_TERM_COUNT	0x00
#define EP93XX_PWMx_DUTY_CYCLE	0x04
#define EP93XX_PWMx_ENABLE	0x08
#define EP93XX_PWMx_INVERT	0x0c

struct ep93xx_pwm {
	void __iomem *base;
	struct clk *clk;
	struct pwm_chip chip;
};

static inline struct ep93xx_pwm *to_ep93xx_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct ep93xx_pwm, chip);
}

static int ep93xx_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct platform_device *pdev = to_platform_device(chip->dev);

	return ep93xx_pwm_acquire_gpio(pdev);
}

static void ep93xx_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct platform_device *pdev = to_platform_device(chip->dev);

	ep93xx_pwm_release_gpio(pdev);
}

static int ep93xx_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			     int duty_ns, int period_ns)
{
	struct ep93xx_pwm *ep93xx_pwm = to_ep93xx_pwm(chip);
	void __iomem *base = ep93xx_pwm->base;
	unsigned long long c;
	unsigned long period_cycles;
	unsigned long duty_cycles;
	unsigned long term;
	int ret = 0;

	/*
	 * The clock needs to be enabled to access the PWM registers.
	 * Configuration can be changed at any time.
	 */
	if (!pwm_is_enabled(pwm)) {
		ret = clk_enable(ep93xx_pwm->clk);
		if (ret)
			return ret;
	}

	c = clk_get_rate(ep93xx_pwm->clk);
	c *= period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	c = period_cycles;
	c *= duty_ns;
	do_div(c, period_ns);
	duty_cycles = c;

	if (period_cycles < 0x10000 && duty_cycles < 0x10000) {
		term = readw(base + EP93XX_PWMx_TERM_COUNT);

		/* Order is important if PWM is running */
		if (period_cycles > term) {
			writew(period_cycles, base + EP93XX_PWMx_TERM_COUNT);
			writew(duty_cycles, base + EP93XX_PWMx_DUTY_CYCLE);
		} else {
			writew(duty_cycles, base + EP93XX_PWMx_DUTY_CYCLE);
			writew(period_cycles, base + EP93XX_PWMx_TERM_COUNT);
		}
	} else {
		ret = -EINVAL;
	}

	if (!pwm_is_enabled(pwm))
		clk_disable(ep93xx_pwm->clk);

	return ret;
}

static int ep93xx_pwm_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
			       enum pwm_polarity polarity)
{
	struct ep93xx_pwm *ep93xx_pwm = to_ep93xx_pwm(chip);
	int ret;

	/*
	 * The clock needs to be enabled to access the PWM registers.
	 * Polarity can only be changed when the PWM is disabled.
	 */
	ret = clk_enable(ep93xx_pwm->clk);
	if (ret)
		return ret;

	if (polarity == PWM_POLARITY_INVERSED)
		writew(0x1, ep93xx_pwm->base + EP93XX_PWMx_INVERT);
	else
		writew(0x0, ep93xx_pwm->base + EP93XX_PWMx_INVERT);

	clk_disable(ep93xx_pwm->clk);

	return 0;
}

static int ep93xx_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ep93xx_pwm *ep93xx_pwm = to_ep93xx_pwm(chip);
	int ret;

	ret = clk_enable(ep93xx_pwm->clk);
	if (ret)
		return ret;

	writew(0x1, ep93xx_pwm->base + EP93XX_PWMx_ENABLE);

	return 0;
}

static void ep93xx_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ep93xx_pwm *ep93xx_pwm = to_ep93xx_pwm(chip);

	writew(0x0, ep93xx_pwm->base + EP93XX_PWMx_ENABLE);
	clk_disable(ep93xx_pwm->clk);
}

static const struct pwm_ops ep93xx_pwm_ops = {
	.request = ep93xx_pwm_request,
	.free = ep93xx_pwm_free,
	.config = ep93xx_pwm_config,
	.set_polarity = ep93xx_pwm_polarity,
	.enable = ep93xx_pwm_enable,
	.disable = ep93xx_pwm_disable,
	.owner = THIS_MODULE,
};

static int ep93xx_pwm_probe(struct platform_device *pdev)
{
	struct ep93xx_pwm *ep93xx_pwm;
	struct resource *res;
	int ret;

	ep93xx_pwm = devm_kzalloc(&pdev->dev, sizeof(*ep93xx_pwm), GFP_KERNEL);
	if (!ep93xx_pwm)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ep93xx_pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ep93xx_pwm->base))
		return PTR_ERR(ep93xx_pwm->base);

	ep93xx_pwm->clk = devm_clk_get(&pdev->dev, "pwm_clk");
	if (IS_ERR(ep93xx_pwm->clk))
		return PTR_ERR(ep93xx_pwm->clk);

	ep93xx_pwm->chip.dev = &pdev->dev;
	ep93xx_pwm->chip.ops = &ep93xx_pwm_ops;
	ep93xx_pwm->chip.base = -1;
	ep93xx_pwm->chip.npwm = 1;

	ret = pwmchip_add(&ep93xx_pwm->chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, ep93xx_pwm);
	return 0;
}

static int ep93xx_pwm_remove(struct platform_device *pdev)
{
	struct ep93xx_pwm *ep93xx_pwm = platform_get_drvdata(pdev);

	return pwmchip_remove(&ep93xx_pwm->chip);
}

static struct platform_driver ep93xx_pwm_driver = {
	.driver = {
		.name = "ep93xx-pwm",
	},
	.probe = ep93xx_pwm_probe,
	.remove = ep93xx_pwm_remove,
};
module_platform_driver(ep93xx_pwm_driver);

MODULE_DESCRIPTION("Cirrus Logic EP93xx PWM driver");
MODULE_AUTHOR("Matthieu Crapet <mcrapet@gmail.com>");
MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_ALIAS("platform:ep93xx-pwm");
MODULE_LICENSE("GPL");
