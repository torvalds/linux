/*
 * Marvell Berlin PWM driver
 *
 * Copyright (C) 2015 Marvell Technology Group Ltd.
 *
 * Author: Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define BERLIN_PWM_EN			0x0
#define  BERLIN_PWM_ENABLE		BIT(0)
#define BERLIN_PWM_CONTROL		0x4
#define  BERLIN_PWM_PRESCALE_MASK	0x7
#define  BERLIN_PWM_PRESCALE_MAX	4096
#define  BERLIN_PWM_INVERT_POLARITY	BIT(3)
#define BERLIN_PWM_DUTY			0x8
#define BERLIN_PWM_TCNT			0xc
#define  BERLIN_PWM_MAX_TCNT		65535

struct berlin_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *base;
};

static inline struct berlin_pwm_chip *to_berlin_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct berlin_pwm_chip, chip);
}

static const u32 prescaler_table[] = {
	1, 4, 8, 16, 64, 256, 1024, 4096
};

static inline u32 berlin_pwm_readl(struct berlin_pwm_chip *chip,
				   unsigned int channel, unsigned long offset)
{
	return readl_relaxed(chip->base + channel * 0x10 + offset);
}

static inline void berlin_pwm_writel(struct berlin_pwm_chip *chip,
				     unsigned int channel, u32 value,
				     unsigned long offset)
{
	writel_relaxed(value, chip->base + channel * 0x10 + offset);
}

static int berlin_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			     int duty_ns, int period_ns)
{
	struct berlin_pwm_chip *pwm = to_berlin_pwm_chip(chip);
	unsigned int prescale;
	u32 value, duty, period;
	u64 cycles, tmp;

	cycles = clk_get_rate(pwm->clk);
	cycles *= period_ns;
	do_div(cycles, NSEC_PER_SEC);

	for (prescale = 0; prescale < ARRAY_SIZE(prescaler_table); prescale++) {
		tmp = cycles;
		do_div(tmp, prescaler_table[prescale]);

		if (tmp <= BERLIN_PWM_MAX_TCNT)
			break;
	}

	if (tmp > BERLIN_PWM_MAX_TCNT)
		return -ERANGE;

	period = tmp;
	cycles = tmp * duty_ns;
	do_div(cycles, period_ns);
	duty = cycles;

	value = berlin_pwm_readl(pwm, pwm_dev->hwpwm, BERLIN_PWM_CONTROL);
	value &= ~BERLIN_PWM_PRESCALE_MASK;
	value |= prescale;
	berlin_pwm_writel(pwm, pwm_dev->hwpwm, value, BERLIN_PWM_CONTROL);

	berlin_pwm_writel(pwm, pwm_dev->hwpwm, duty, BERLIN_PWM_DUTY);
	berlin_pwm_writel(pwm, pwm_dev->hwpwm, period, BERLIN_PWM_TCNT);

	return 0;
}

static int berlin_pwm_set_polarity(struct pwm_chip *chip,
				   struct pwm_device *pwm_dev,
				   enum pwm_polarity polarity)
{
	struct berlin_pwm_chip *pwm = to_berlin_pwm_chip(chip);
	u32 value;

	value = berlin_pwm_readl(pwm, pwm_dev->hwpwm, BERLIN_PWM_CONTROL);

	if (polarity == PWM_POLARITY_NORMAL)
		value &= ~BERLIN_PWM_INVERT_POLARITY;
	else
		value |= BERLIN_PWM_INVERT_POLARITY;

	berlin_pwm_writel(pwm, pwm_dev->hwpwm, value, BERLIN_PWM_CONTROL);

	return 0;
}

static int berlin_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm_dev)
{
	struct berlin_pwm_chip *pwm = to_berlin_pwm_chip(chip);
	u32 value;

	value = berlin_pwm_readl(pwm, pwm_dev->hwpwm, BERLIN_PWM_EN);
	value |= BERLIN_PWM_ENABLE;
	berlin_pwm_writel(pwm, pwm_dev->hwpwm, value, BERLIN_PWM_EN);

	return 0;
}

static void berlin_pwm_disable(struct pwm_chip *chip,
			       struct pwm_device *pwm_dev)
{
	struct berlin_pwm_chip *pwm = to_berlin_pwm_chip(chip);
	u32 value;

	value = berlin_pwm_readl(pwm, pwm_dev->hwpwm, BERLIN_PWM_EN);
	value &= ~BERLIN_PWM_ENABLE;
	berlin_pwm_writel(pwm, pwm_dev->hwpwm, value, BERLIN_PWM_EN);
}

static const struct pwm_ops berlin_pwm_ops = {
	.config = berlin_pwm_config,
	.set_polarity = berlin_pwm_set_polarity,
	.enable = berlin_pwm_enable,
	.disable = berlin_pwm_disable,
	.owner = THIS_MODULE,
};

static const struct of_device_id berlin_pwm_match[] = {
	{ .compatible = "marvell,berlin-pwm" },
	{ },
};
MODULE_DEVICE_TABLE(of, berlin_pwm_match);

static int berlin_pwm_probe(struct platform_device *pdev)
{
	struct berlin_pwm_chip *pwm;
	struct resource *res;
	int ret;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm->base))
		return PTR_ERR(pwm->base);

	pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pwm->clk))
		return PTR_ERR(pwm->clk);

	ret = clk_prepare_enable(pwm->clk);
	if (ret)
		return ret;

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &berlin_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 4;
	pwm->chip.can_sleep = true;
	pwm->chip.of_xlate = of_pwm_xlate_with_flags;
	pwm->chip.of_pwm_n_cells = 3;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		clk_disable_unprepare(pwm->clk);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);

	return 0;
}

static int berlin_pwm_remove(struct platform_device *pdev)
{
	struct berlin_pwm_chip *pwm = platform_get_drvdata(pdev);
	int ret;

	ret = pwmchip_remove(&pwm->chip);
	clk_disable_unprepare(pwm->clk);

	return ret;
}

static struct platform_driver berlin_pwm_driver = {
	.probe = berlin_pwm_probe,
	.remove = berlin_pwm_remove,
	.driver = {
		.name = "berlin-pwm",
		.of_match_table = berlin_pwm_match,
	},
};
module_platform_driver(berlin_pwm_driver);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_DESCRIPTION("Marvell Berlin PWM driver");
MODULE_LICENSE("GPL v2");
