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
#include <linux/slab.h>

#define BERLIN_PWM_EN			0x0
#define  BERLIN_PWM_ENABLE		BIT(0)
#define BERLIN_PWM_CONTROL		0x4
/*
 * The prescaler claims to support 8 different moduli, configured using the
 * low three bits of PWM_CONTROL. (Sequentially, they are 1, 4, 8, 16, 64,
 * 256, 1024, and 4096.)  However, the moduli from 4 to 1024 appear to be
 * implemented by internally shifting TCNT left without adding additional
 * bits. So, the max TCNT that actually works for a modulus of 4 is 0x3fff;
 * for 8, 0x1fff; and so on. This means that those moduli are entirely
 * useless, as we could just do the shift ourselves. The 4096 modulus is
 * implemented with a real prescaler, so we do use that, but we treat it
 * as a flag instead of pretending the modulus is actually configurable.
 */
#define  BERLIN_PWM_PRESCALE_4096	0x7
#define  BERLIN_PWM_INVERT_POLARITY	BIT(3)
#define BERLIN_PWM_DUTY			0x8
#define BERLIN_PWM_TCNT			0xc
#define  BERLIN_PWM_MAX_TCNT		65535

struct berlin_pwm_channel {
	u32 enable;
	u32 ctrl;
	u32 duty;
	u32 tcnt;
};

struct berlin_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *base;
};

static inline struct berlin_pwm_chip *to_berlin_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct berlin_pwm_chip, chip);
}

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

static int berlin_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct berlin_pwm_channel *channel;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	return pwm_set_chip_data(pwm, channel);
}

static void berlin_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct berlin_pwm_channel *channel = pwm_get_chip_data(pwm);

	pwm_set_chip_data(pwm, NULL);
	kfree(channel);
}

static int berlin_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			     int duty_ns, int period_ns)
{
	struct berlin_pwm_chip *pwm = to_berlin_pwm_chip(chip);
	bool prescale_4096 = false;
	u32 value, duty, period;
	u64 cycles;

	cycles = clk_get_rate(pwm->clk);
	cycles *= period_ns;
	do_div(cycles, NSEC_PER_SEC);

	if (cycles > BERLIN_PWM_MAX_TCNT) {
		prescale_4096 = true;
		cycles >>= 12; // Prescaled by 4096

		if (cycles > BERLIN_PWM_MAX_TCNT)
			return -ERANGE;
	}

	period = cycles;
	cycles *= duty_ns;
	do_div(cycles, period_ns);
	duty = cycles;

	value = berlin_pwm_readl(pwm, pwm_dev->hwpwm, BERLIN_PWM_CONTROL);
	if (prescale_4096)
		value |= BERLIN_PWM_PRESCALE_4096;
	else
		value &= ~BERLIN_PWM_PRESCALE_4096;
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
	.request = berlin_pwm_request,
	.free = berlin_pwm_free,
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

#ifdef CONFIG_PM_SLEEP
static int berlin_pwm_suspend(struct device *dev)
{
	struct berlin_pwm_chip *pwm = dev_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < pwm->chip.npwm; i++) {
		struct berlin_pwm_channel *channel;

		channel = pwm_get_chip_data(&pwm->chip.pwms[i]);
		if (!channel)
			continue;

		channel->enable = berlin_pwm_readl(pwm, i, BERLIN_PWM_ENABLE);
		channel->ctrl = berlin_pwm_readl(pwm, i, BERLIN_PWM_CONTROL);
		channel->duty = berlin_pwm_readl(pwm, i, BERLIN_PWM_DUTY);
		channel->tcnt = berlin_pwm_readl(pwm, i, BERLIN_PWM_TCNT);
	}

	clk_disable_unprepare(pwm->clk);

	return 0;
}

static int berlin_pwm_resume(struct device *dev)
{
	struct berlin_pwm_chip *pwm = dev_get_drvdata(dev);
	unsigned int i;
	int ret;

	ret = clk_prepare_enable(pwm->clk);
	if (ret)
		return ret;

	for (i = 0; i < pwm->chip.npwm; i++) {
		struct berlin_pwm_channel *channel;

		channel = pwm_get_chip_data(&pwm->chip.pwms[i]);
		if (!channel)
			continue;

		berlin_pwm_writel(pwm, i, channel->ctrl, BERLIN_PWM_CONTROL);
		berlin_pwm_writel(pwm, i, channel->duty, BERLIN_PWM_DUTY);
		berlin_pwm_writel(pwm, i, channel->tcnt, BERLIN_PWM_TCNT);
		berlin_pwm_writel(pwm, i, channel->enable, BERLIN_PWM_ENABLE);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(berlin_pwm_pm_ops, berlin_pwm_suspend,
			 berlin_pwm_resume);

static struct platform_driver berlin_pwm_driver = {
	.probe = berlin_pwm_probe,
	.remove = berlin_pwm_remove,
	.driver = {
		.name = "berlin-pwm",
		.of_match_table = berlin_pwm_match,
		.pm = &berlin_pwm_pm_ops,
	},
};
module_platform_driver(berlin_pwm_driver);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_DESCRIPTION("Marvell Berlin PWM driver");
MODULE_LICENSE("GPL v2");
