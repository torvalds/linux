/*
 * Imagination Technologies Pulse Width Modulator driver
 *
 * Copyright (c) 2014-2015, Imagination Technologies
 *
 * Based on drivers/pwm/pwm-tegra.c, Copyright (c) 2010, NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* PWM registers */
#define PWM_CTRL_CFG				0x0000
#define PWM_CTRL_CFG_NO_SUB_DIV			0
#define PWM_CTRL_CFG_SUB_DIV0			1
#define PWM_CTRL_CFG_SUB_DIV1			2
#define PWM_CTRL_CFG_SUB_DIV0_DIV1		3
#define PWM_CTRL_CFG_DIV_SHIFT(ch)		((ch) * 2 + 4)
#define PWM_CTRL_CFG_DIV_MASK			0x3

#define PWM_CH_CFG(ch)				(0x4 + (ch) * 4)
#define PWM_CH_CFG_TMBASE_SHIFT			0
#define PWM_CH_CFG_DUTY_SHIFT			16

#define PERIP_PWM_PDM_CONTROL			0x0140
#define PERIP_PWM_PDM_CONTROL_CH_MASK		0x1
#define PERIP_PWM_PDM_CONTROL_CH_SHIFT(ch)	((ch) * 4)

#define MAX_TMBASE_STEPS			65536

struct img_pwm_chip {
	struct device	*dev;
	struct pwm_chip	chip;
	struct clk	*pwm_clk;
	struct clk	*sys_clk;
	void __iomem	*base;
	struct regmap	*periph_regs;
};

static inline struct img_pwm_chip *to_img_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct img_pwm_chip, chip);
}

static inline void img_pwm_writel(struct img_pwm_chip *chip,
				  u32 reg, u32 val)
{
	writel(val, chip->base + reg);
}

static inline u32 img_pwm_readl(struct img_pwm_chip *chip,
					 u32 reg)
{
	return readl(chip->base + reg);
}

static int img_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	u32 val, div, duty, timebase;
	unsigned long mul, output_clk_hz, input_clk_hz;
	struct img_pwm_chip *pwm_chip = to_img_pwm_chip(chip);

	input_clk_hz = clk_get_rate(pwm_chip->pwm_clk);
	output_clk_hz = DIV_ROUND_UP(NSEC_PER_SEC, period_ns);

	mul = DIV_ROUND_UP(input_clk_hz, output_clk_hz);
	if (mul <= MAX_TMBASE_STEPS) {
		div = PWM_CTRL_CFG_NO_SUB_DIV;
		timebase = DIV_ROUND_UP(mul, 1);
	} else if (mul <= MAX_TMBASE_STEPS * 8) {
		div = PWM_CTRL_CFG_SUB_DIV0;
		timebase = DIV_ROUND_UP(mul, 8);
	} else if (mul <= MAX_TMBASE_STEPS * 64) {
		div = PWM_CTRL_CFG_SUB_DIV1;
		timebase = DIV_ROUND_UP(mul, 64);
	} else if (mul <= MAX_TMBASE_STEPS * 512) {
		div = PWM_CTRL_CFG_SUB_DIV0_DIV1;
		timebase = DIV_ROUND_UP(mul, 512);
	} else if (mul > MAX_TMBASE_STEPS * 512) {
		dev_err(chip->dev,
			"failed to configure timebase steps/divider value\n");
		return -EINVAL;
	}

	duty = DIV_ROUND_UP(timebase * duty_ns, period_ns);

	val = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);
	val &= ~(PWM_CTRL_CFG_DIV_MASK << PWM_CTRL_CFG_DIV_SHIFT(pwm->hwpwm));
	val |= (div & PWM_CTRL_CFG_DIV_MASK) <<
		PWM_CTRL_CFG_DIV_SHIFT(pwm->hwpwm);
	img_pwm_writel(pwm_chip, PWM_CTRL_CFG, val);

	val = (duty << PWM_CH_CFG_DUTY_SHIFT) |
	      (timebase << PWM_CH_CFG_TMBASE_SHIFT);
	img_pwm_writel(pwm_chip, PWM_CH_CFG(pwm->hwpwm), val);

	return 0;
}

static int img_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val;
	struct img_pwm_chip *pwm_chip = to_img_pwm_chip(chip);

	val = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);
	val |= BIT(pwm->hwpwm);
	img_pwm_writel(pwm_chip, PWM_CTRL_CFG, val);

	regmap_update_bits(pwm_chip->periph_regs, PERIP_PWM_PDM_CONTROL,
			   PERIP_PWM_PDM_CONTROL_CH_MASK <<
			   PERIP_PWM_PDM_CONTROL_CH_SHIFT(pwm->hwpwm), 0);

	return 0;
}

static void img_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val;
	struct img_pwm_chip *pwm_chip = to_img_pwm_chip(chip);

	val = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);
	val &= ~BIT(pwm->hwpwm);
	img_pwm_writel(pwm_chip, PWM_CTRL_CFG, val);
}

static const struct pwm_ops img_pwm_ops = {
	.config = img_pwm_config,
	.enable = img_pwm_enable,
	.disable = img_pwm_disable,
	.owner = THIS_MODULE,
};

static int img_pwm_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct img_pwm_chip *pwm;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm->base))
		return PTR_ERR(pwm->base);

	pwm->periph_regs = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
							   "img,cr-periph");
	if (IS_ERR(pwm->periph_regs))
		return PTR_ERR(pwm->periph_regs);

	pwm->sys_clk = devm_clk_get(&pdev->dev, "sys");
	if (IS_ERR(pwm->sys_clk)) {
		dev_err(&pdev->dev, "failed to get system clock\n");
		return PTR_ERR(pwm->sys_clk);
	}

	pwm->pwm_clk = devm_clk_get(&pdev->dev, "pwm");
	if (IS_ERR(pwm->pwm_clk)) {
		dev_err(&pdev->dev, "failed to get pwm clock\n");
		return PTR_ERR(pwm->pwm_clk);
	}

	ret = clk_prepare_enable(pwm->sys_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not prepare or enable sys clock\n");
		return ret;
	}

	ret = clk_prepare_enable(pwm->pwm_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not prepare or enable pwm clock\n");
		goto disable_sysclk;
	}

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &img_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 4;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add failed: %d\n", ret);
		goto disable_pwmclk;
	}

	platform_set_drvdata(pdev, pwm);
	return 0;

disable_pwmclk:
	clk_disable_unprepare(pwm->pwm_clk);
disable_sysclk:
	clk_disable_unprepare(pwm->sys_clk);
	return ret;
}

static int img_pwm_remove(struct platform_device *pdev)
{
	struct img_pwm_chip *pwm_chip = platform_get_drvdata(pdev);
	u32 val;
	unsigned int i;

	for (i = 0; i < pwm_chip->chip.npwm; i++) {
		val = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);
		val &= ~BIT(i);
		img_pwm_writel(pwm_chip, PWM_CTRL_CFG, val);
	}

	clk_disable_unprepare(pwm_chip->pwm_clk);
	clk_disable_unprepare(pwm_chip->sys_clk);

	return pwmchip_remove(&pwm_chip->chip);
}

static const struct of_device_id img_pwm_of_match[] = {
	{ .compatible = "img,pistachio-pwm", },
	{ }
};
MODULE_DEVICE_TABLE(of, img_pwm_of_match);

static struct platform_driver img_pwm_driver = {
	.driver = {
		.name = "img-pwm",
		.of_match_table = img_pwm_of_match,
	},
	.probe = img_pwm_probe,
	.remove = img_pwm_remove,
};
module_platform_driver(img_pwm_driver);

MODULE_AUTHOR("Sai Masarapu <Sai.Masarapu@imgtec.com>");
MODULE_DESCRIPTION("Imagination Technologies PWM DAC driver");
MODULE_LICENSE("GPL v2");
