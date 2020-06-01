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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
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

#define IMG_PWM_PM_TIMEOUT			1000 /* ms */

/*
 * PWM period is specified with a timebase register,
 * in number of step periods. The PWM duty cycle is also
 * specified in step periods, in the [0, $timebase] range.
 * In other words, the timebase imposes the duty cycle
 * resolution. Therefore, let's constraint the timebase to
 * a minimum value to allow a sane range of duty cycle values.
 * Imposing a minimum timebase, will impose a maximum PWM frequency.
 *
 * The value chosen is completely arbitrary.
 */
#define MIN_TMBASE_STEPS			16

#define IMG_PWM_NPWM				4

struct img_pwm_soc_data {
	u32 max_timebase;
};

struct img_pwm_chip {
	struct device	*dev;
	struct pwm_chip	chip;
	struct clk	*pwm_clk;
	struct clk	*sys_clk;
	void __iomem	*base;
	struct regmap	*periph_regs;
	int		max_period_ns;
	int		min_period_ns;
	const struct img_pwm_soc_data   *data;
	u32		suspend_ctrl_cfg;
	u32		suspend_ch_cfg[IMG_PWM_NPWM];
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
	unsigned int max_timebase = pwm_chip->data->max_timebase;
	int ret;

	if (period_ns < pwm_chip->min_period_ns ||
	    period_ns > pwm_chip->max_period_ns) {
		dev_err(chip->dev, "configured period not in range\n");
		return -ERANGE;
	}

	input_clk_hz = clk_get_rate(pwm_chip->pwm_clk);
	output_clk_hz = DIV_ROUND_UP(NSEC_PER_SEC, period_ns);

	mul = DIV_ROUND_UP(input_clk_hz, output_clk_hz);
	if (mul <= max_timebase) {
		div = PWM_CTRL_CFG_NO_SUB_DIV;
		timebase = DIV_ROUND_UP(mul, 1);
	} else if (mul <= max_timebase * 8) {
		div = PWM_CTRL_CFG_SUB_DIV0;
		timebase = DIV_ROUND_UP(mul, 8);
	} else if (mul <= max_timebase * 64) {
		div = PWM_CTRL_CFG_SUB_DIV1;
		timebase = DIV_ROUND_UP(mul, 64);
	} else if (mul <= max_timebase * 512) {
		div = PWM_CTRL_CFG_SUB_DIV0_DIV1;
		timebase = DIV_ROUND_UP(mul, 512);
	} else if (mul > max_timebase * 512) {
		dev_err(chip->dev,
			"failed to configure timebase steps/divider value\n");
		return -EINVAL;
	}

	duty = DIV_ROUND_UP(timebase * duty_ns, period_ns);

	ret = pm_runtime_get_sync(chip->dev);
	if (ret < 0) {
		pm_runtime_put_autosuspend(chip->dev);
		return ret;
	}

	val = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);
	val &= ~(PWM_CTRL_CFG_DIV_MASK << PWM_CTRL_CFG_DIV_SHIFT(pwm->hwpwm));
	val |= (div & PWM_CTRL_CFG_DIV_MASK) <<
		PWM_CTRL_CFG_DIV_SHIFT(pwm->hwpwm);
	img_pwm_writel(pwm_chip, PWM_CTRL_CFG, val);

	val = (duty << PWM_CH_CFG_DUTY_SHIFT) |
	      (timebase << PWM_CH_CFG_TMBASE_SHIFT);
	img_pwm_writel(pwm_chip, PWM_CH_CFG(pwm->hwpwm), val);

	pm_runtime_mark_last_busy(chip->dev);
	pm_runtime_put_autosuspend(chip->dev);

	return 0;
}

static int img_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val;
	struct img_pwm_chip *pwm_chip = to_img_pwm_chip(chip);
	int ret;

	ret = pm_runtime_get_sync(chip->dev);
	if (ret < 0)
		return ret;

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

	pm_runtime_mark_last_busy(chip->dev);
	pm_runtime_put_autosuspend(chip->dev);
}

static const struct pwm_ops img_pwm_ops = {
	.config = img_pwm_config,
	.enable = img_pwm_enable,
	.disable = img_pwm_disable,
	.owner = THIS_MODULE,
};

static const struct img_pwm_soc_data pistachio_pwm = {
	.max_timebase = 255,
};

static const struct of_device_id img_pwm_of_match[] = {
	{
		.compatible = "img,pistachio-pwm",
		.data = &pistachio_pwm,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, img_pwm_of_match);

static int img_pwm_runtime_suspend(struct device *dev)
{
	struct img_pwm_chip *pwm_chip = dev_get_drvdata(dev);

	clk_disable_unprepare(pwm_chip->pwm_clk);
	clk_disable_unprepare(pwm_chip->sys_clk);

	return 0;
}

static int img_pwm_runtime_resume(struct device *dev)
{
	struct img_pwm_chip *pwm_chip = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(pwm_chip->sys_clk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable sys clock\n");
		return ret;
	}

	ret = clk_prepare_enable(pwm_chip->pwm_clk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable pwm clock\n");
		clk_disable_unprepare(pwm_chip->sys_clk);
		return ret;
	}

	return 0;
}

static int img_pwm_probe(struct platform_device *pdev)
{
	int ret;
	u64 val;
	unsigned long clk_rate;
	struct resource *res;
	struct img_pwm_chip *pwm;
	const struct of_device_id *of_dev_id;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm->base))
		return PTR_ERR(pwm->base);

	of_dev_id = of_match_device(img_pwm_of_match, &pdev->dev);
	if (!of_dev_id)
		return -ENODEV;
	pwm->data = of_dev_id->data;

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

	pm_runtime_set_autosuspend_delay(&pdev->dev, IMG_PWM_PM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = img_pwm_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	clk_rate = clk_get_rate(pwm->pwm_clk);
	if (!clk_rate) {
		dev_err(&pdev->dev, "pwm clock has no frequency\n");
		ret = -EINVAL;
		goto err_suspend;
	}

	/* The maximum input clock divider is 512 */
	val = (u64)NSEC_PER_SEC * 512 * pwm->data->max_timebase;
	do_div(val, clk_rate);
	pwm->max_period_ns = val;

	val = (u64)NSEC_PER_SEC * MIN_TMBASE_STEPS;
	do_div(val, clk_rate);
	pwm->min_period_ns = val;

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &img_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = IMG_PWM_NPWM;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add failed: %d\n", ret);
		goto err_suspend;
	}

	platform_set_drvdata(pdev, pwm);
	return 0;

err_suspend:
	if (!pm_runtime_enabled(&pdev->dev))
		img_pwm_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	return ret;
}

static int img_pwm_remove(struct platform_device *pdev)
{
	struct img_pwm_chip *pwm_chip = platform_get_drvdata(pdev);
	u32 val;
	unsigned int i;
	int ret;

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put(&pdev->dev);
		return ret;
	}

	for (i = 0; i < pwm_chip->chip.npwm; i++) {
		val = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);
		val &= ~BIT(i);
		img_pwm_writel(pwm_chip, PWM_CTRL_CFG, val);
	}

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		img_pwm_runtime_suspend(&pdev->dev);

	return pwmchip_remove(&pwm_chip->chip);
}

#ifdef CONFIG_PM_SLEEP
static int img_pwm_suspend(struct device *dev)
{
	struct img_pwm_chip *pwm_chip = dev_get_drvdata(dev);
	int i, ret;

	if (pm_runtime_status_suspended(dev)) {
		ret = img_pwm_runtime_resume(dev);
		if (ret)
			return ret;
	}

	for (i = 0; i < pwm_chip->chip.npwm; i++)
		pwm_chip->suspend_ch_cfg[i] = img_pwm_readl(pwm_chip,
							    PWM_CH_CFG(i));

	pwm_chip->suspend_ctrl_cfg = img_pwm_readl(pwm_chip, PWM_CTRL_CFG);

	img_pwm_runtime_suspend(dev);

	return 0;
}

static int img_pwm_resume(struct device *dev)
{
	struct img_pwm_chip *pwm_chip = dev_get_drvdata(dev);
	int ret;
	int i;

	ret = img_pwm_runtime_resume(dev);
	if (ret)
		return ret;

	for (i = 0; i < pwm_chip->chip.npwm; i++)
		img_pwm_writel(pwm_chip, PWM_CH_CFG(i),
			       pwm_chip->suspend_ch_cfg[i]);

	img_pwm_writel(pwm_chip, PWM_CTRL_CFG, pwm_chip->suspend_ctrl_cfg);

	for (i = 0; i < pwm_chip->chip.npwm; i++)
		if (pwm_chip->suspend_ctrl_cfg & BIT(i))
			regmap_update_bits(pwm_chip->periph_regs,
					   PERIP_PWM_PDM_CONTROL,
					   PERIP_PWM_PDM_CONTROL_CH_MASK <<
					   PERIP_PWM_PDM_CONTROL_CH_SHIFT(i),
					   0);

	if (pm_runtime_status_suspended(dev))
		img_pwm_runtime_suspend(dev);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops img_pwm_pm_ops = {
	SET_RUNTIME_PM_OPS(img_pwm_runtime_suspend,
			   img_pwm_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(img_pwm_suspend, img_pwm_resume)
};

static struct platform_driver img_pwm_driver = {
	.driver = {
		.name = "img-pwm",
		.pm = &img_pwm_pm_ops,
		.of_match_table = img_pwm_of_match,
	},
	.probe = img_pwm_probe,
	.remove = img_pwm_remove,
};
module_platform_driver(img_pwm_driver);

MODULE_AUTHOR("Sai Masarapu <Sai.Masarapu@imgtec.com>");
MODULE_DESCRIPTION("Imagination Technologies PWM DAC driver");
MODULE_LICENSE("GPL v2");
