// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2025 Loongson Technology Corporation Limited.
 *
 * Loongson PWM driver
 *
 * For Loongson's PWM IP block documentation please refer Chapter 11 of
 * Reference Manual: https://loongson.github.io/LoongArch-Documentation/Loongson-7A1000-usermanual-EN.pdf
 *
 * Author: Juxin Gao <gaojuxin@loongson.cn>
 * Further cleanup and restructuring by:
 *         Binbin Zhou <zhoubinbin@loongson.cn>
 *
 * Limitations:
 * - If both DUTY and PERIOD are set to 0, the output is a constant low signal.
 * - When disabled the output is driven to 0 independent of the configured
 *   polarity.
 * - If the register is reconfigured while PWM is running, it does not complete
 *   the currently running period.
 * - Disabling the PWM stops the output immediately (without waiting for current
 *   period to complete first).
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/units.h>

/* Loongson PWM registers */
#define LOONGSON_PWM_REG_DUTY		0x4 /* Low Pulse Buffer Register */
#define LOONGSON_PWM_REG_PERIOD		0x8 /* Pulse Period Buffer Register */
#define LOONGSON_PWM_REG_CTRL		0xc /* Control Register */

/* Control register bits */
#define LOONGSON_PWM_CTRL_REG_EN	BIT(0)  /* Counter Enable Bit */
#define LOONGSON_PWM_CTRL_REG_OE	BIT(3)  /* Pulse Output Enable Control Bit, Valid Low */
#define LOONGSON_PWM_CTRL_REG_SINGLE	BIT(4)  /* Single Pulse Control Bit */
#define LOONGSON_PWM_CTRL_REG_INTE	BIT(5)  /* Interrupt Enable Bit */
#define LOONGSON_PWM_CTRL_REG_INT	BIT(6)  /* Interrupt Bit */
#define LOONGSON_PWM_CTRL_REG_RST	BIT(7)  /* Counter Reset Bit */
#define LOONGSON_PWM_CTRL_REG_CAPTE	BIT(8)  /* Measurement Pulse Enable Bit */
#define LOONGSON_PWM_CTRL_REG_INVERT	BIT(9)  /* Output flip-flop Enable Bit */
#define LOONGSON_PWM_CTRL_REG_DZONE	BIT(10) /* Anti-dead Zone Enable Bit */

/* default input clk frequency for the ACPI case */
#define LOONGSON_PWM_FREQ_DEFAULT	50000000 /* Hz */

struct pwm_loongson_ddata {
	struct clk *clk;
	void __iomem *base;
	u64 clk_rate;
};

static inline __pure struct pwm_loongson_ddata *to_pwm_loongson_ddata(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static inline u32 pwm_loongson_readl(struct pwm_loongson_ddata *ddata, u32 offset)
{
	return readl(ddata->base + offset);
}

static inline void pwm_loongson_writel(struct pwm_loongson_ddata *ddata,
				       u32 val, u32 offset)
{
	writel(val, ddata->base + offset);
}

static int pwm_loongson_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
				     enum pwm_polarity polarity)
{
	u16 val;
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);

	val = pwm_loongson_readl(ddata, LOONGSON_PWM_REG_CTRL);

	if (polarity == PWM_POLARITY_INVERSED)
		/* Duty cycle defines LOW period of PWM */
		val |= LOONGSON_PWM_CTRL_REG_INVERT;
	else
		/* Duty cycle defines HIGH period of PWM */
		val &= ~LOONGSON_PWM_CTRL_REG_INVERT;

	pwm_loongson_writel(ddata, val, LOONGSON_PWM_REG_CTRL);

	return 0;
}

static void pwm_loongson_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val;
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);

	val = pwm_loongson_readl(ddata, LOONGSON_PWM_REG_CTRL);
	val &= ~LOONGSON_PWM_CTRL_REG_EN;
	pwm_loongson_writel(ddata, val, LOONGSON_PWM_REG_CTRL);
}

static int pwm_loongson_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	u32 val;
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);

	val = pwm_loongson_readl(ddata, LOONGSON_PWM_REG_CTRL);
	val |= LOONGSON_PWM_CTRL_REG_EN;
	pwm_loongson_writel(ddata, val, LOONGSON_PWM_REG_CTRL);

	return 0;
}

static int pwm_loongson_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       u64 duty_ns, u64 period_ns)
{
	u64 duty, period;
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);

	/* duty = duty_ns * ddata->clk_rate / NSEC_PER_SEC */
	duty = mul_u64_u64_div_u64(duty_ns, ddata->clk_rate, NSEC_PER_SEC);
	if (duty > U32_MAX)
		duty = U32_MAX;

	/* period = period_ns * ddata->clk_rate / NSEC_PER_SEC */
	period = mul_u64_u64_div_u64(period_ns, ddata->clk_rate, NSEC_PER_SEC);
	if (period > U32_MAX)
		period = U32_MAX;

	pwm_loongson_writel(ddata, duty, LOONGSON_PWM_REG_DUTY);
	pwm_loongson_writel(ddata, period, LOONGSON_PWM_REG_PERIOD);

	return 0;
}

static int pwm_loongson_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	int ret;
	bool enabled = pwm->state.enabled;

	if (!state->enabled) {
		if (enabled)
			pwm_loongson_disable(chip, pwm);
		return 0;
	}

	ret = pwm_loongson_set_polarity(chip, pwm, state->polarity);
	if (ret)
		return ret;

	ret = pwm_loongson_config(chip, pwm, state->duty_cycle, state->period);
	if (ret)
		return ret;

	if (!enabled && state->enabled)
		ret = pwm_loongson_enable(chip, pwm);

	return ret;
}

static int pwm_loongson_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	u32 duty, period, ctrl;
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);

	duty = pwm_loongson_readl(ddata, LOONGSON_PWM_REG_DUTY);
	period = pwm_loongson_readl(ddata, LOONGSON_PWM_REG_PERIOD);
	ctrl = pwm_loongson_readl(ddata, LOONGSON_PWM_REG_CTRL);

	/* duty & period have a max of 2^32, so we can't overflow */
	state->duty_cycle = DIV64_U64_ROUND_UP((u64)duty * NSEC_PER_SEC, ddata->clk_rate);
	state->period = DIV64_U64_ROUND_UP((u64)period * NSEC_PER_SEC, ddata->clk_rate);
	state->polarity = (ctrl & LOONGSON_PWM_CTRL_REG_INVERT) ? PWM_POLARITY_INVERSED :
			  PWM_POLARITY_NORMAL;
	state->enabled = (ctrl & LOONGSON_PWM_CTRL_REG_EN) ? true : false;

	return 0;
}

static const struct pwm_ops pwm_loongson_ops = {
	.apply = pwm_loongson_apply,
	.get_state = pwm_loongson_get_state,
};

static int pwm_loongson_probe(struct platform_device *pdev)
{
	int ret;
	struct pwm_chip *chip;
	struct pwm_loongson_ddata *ddata;
	struct device *dev = &pdev->dev;

	chip = devm_pwmchip_alloc(dev, 1, sizeof(*ddata));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	ddata = to_pwm_loongson_ddata(chip);

	ddata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);

	ddata->clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(ddata->clk))
		return dev_err_probe(dev, PTR_ERR(ddata->clk),
				     "Failed to get pwm clock\n");
	if (ddata->clk) {
		ret = devm_clk_rate_exclusive_get(dev, ddata->clk);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to get exclusive rate\n");

		ddata->clk_rate = clk_get_rate(ddata->clk);
		if (!ddata->clk_rate)
			return dev_err_probe(dev, -EINVAL,
					     "Failed to get frequency\n");
	} else {
		ddata->clk_rate = LOONGSON_PWM_FREQ_DEFAULT;
	}

	/* This check is done to prevent an overflow in .apply */
	if (ddata->clk_rate > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL, "PWM clock out of range\n");

	chip->ops = &pwm_loongson_ops;
	chip->atomic = true;
	dev_set_drvdata(dev, chip);

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to add PWM chip\n");

	return 0;
}

static int pwm_loongson_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);
	struct pwm_device *pwm = &chip->pwms[0];

	if (pwm->state.enabled)
		return -EBUSY;

	clk_disable_unprepare(ddata->clk);

	return 0;
}

static int pwm_loongson_resume(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct pwm_loongson_ddata *ddata = to_pwm_loongson_ddata(chip);

	return clk_prepare_enable(ddata->clk);
}

static DEFINE_SIMPLE_DEV_PM_OPS(pwm_loongson_pm_ops, pwm_loongson_suspend,
				pwm_loongson_resume);

static const struct of_device_id pwm_loongson_of_ids[] = {
	{ .compatible = "loongson,ls7a-pwm" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, pwm_loongson_of_ids);

static const struct acpi_device_id pwm_loongson_acpi_ids[] = {
	{ "LOON0006" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, pwm_loongson_acpi_ids);

static struct platform_driver pwm_loongson_driver = {
	.probe = pwm_loongson_probe,
	.driver = {
		.name = "loongson-pwm",
		.pm = pm_ptr(&pwm_loongson_pm_ops),
		.of_match_table = pwm_loongson_of_ids,
		.acpi_match_table = pwm_loongson_acpi_ids,
	},
};
module_platform_driver(pwm_loongson_driver);

MODULE_DESCRIPTION("Loongson PWM driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited.");
MODULE_LICENSE("GPL");
