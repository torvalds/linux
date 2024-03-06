// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for TWL4030/6030 Generic Pulse Width Modulator
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/mfd/twl.h>
#include <linux/slab.h>

/*
 * This driver handles the PWMs of TWL4030 and TWL6030.
 * The TRM names for the PWMs on TWL4030 are: PWM0, PWM1
 * TWL6030 also have two PWMs named in the TRM as PWM1, PWM2
 */

#define TWL_PWM_MAX		0x7f

/* Registers, bits and macro for TWL4030 */
#define TWL4030_GPBR1_REG	0x0c
#define TWL4030_PMBR1_REG	0x0d

/* GPBR1 register bits */
#define TWL4030_PWMXCLK_ENABLE	(1 << 0)
#define TWL4030_PWMX_ENABLE	(1 << 2)
#define TWL4030_PWMX_BITS	(TWL4030_PWMX_ENABLE | TWL4030_PWMXCLK_ENABLE)
#define TWL4030_PWM_TOGGLE(pwm, x)	((x) << (pwm))

/* PMBR1 register bits */
#define TWL4030_GPIO6_PWM0_MUTE_MASK		(0x03 << 2)
#define TWL4030_GPIO6_PWM0_MUTE_PWM0		(0x01 << 2)
#define TWL4030_GPIO7_VIBRASYNC_PWM1_MASK	(0x03 << 4)
#define TWL4030_GPIO7_VIBRASYNC_PWM1_PWM1	(0x03 << 4)

/* Register, bits and macro for TWL6030 */
#define TWL6030_TOGGLE3_REG	0x92

#define TWL6030_PWMXR		(1 << 0)
#define TWL6030_PWMXS		(1 << 1)
#define TWL6030_PWMXEN		(1 << 2)
#define TWL6030_PWM_TOGGLE(pwm, x)	((x) << (pwm * 3))

struct twl_pwm_chip {
	struct pwm_chip chip;
	struct mutex mutex;
	u8 twl6030_toggle3;
	u8 twl4030_pwm_mux;
};

static inline struct twl_pwm_chip *to_twl(struct pwm_chip *chip)
{
	return container_of(chip, struct twl_pwm_chip, chip);
}

static int twl_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  u64 duty_ns, u64 period_ns)
{
	int duty_cycle = DIV64_U64_ROUND_UP(duty_ns * TWL_PWM_MAX, period_ns) + 1;
	u8 pwm_config[2] = { 1, 0 };
	int base, ret;

	/*
	 * To configure the duty period:
	 * On-cycle is set to 1 (the minimum allowed value)
	 * The off time of 0 is not configurable, so the mapping is:
	 * 0 -> off cycle = 2,
	 * 1 -> off cycle = 2,
	 * 2 -> off cycle = 3,
	 * 126 - > off cycle 127,
	 * 127 - > off cycle 1
	 * When on cycle == off cycle the PWM will be always on
	 */
	if (duty_cycle == 1)
		duty_cycle = 2;
	else if (duty_cycle > TWL_PWM_MAX)
		duty_cycle = 1;

	base = pwm->hwpwm * 3;

	pwm_config[1] = duty_cycle;

	ret = twl_i2c_write(TWL_MODULE_PWM, pwm_config, base, 2);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to configure PWM\n", pwm->label);

	return ret;
}

static int twl4030_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwm_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL4030_MODULE_INTBR, &val, TWL4030_GPBR1_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read GPBR1\n", pwm->label);
		goto out;
	}

	val |= TWL4030_PWM_TOGGLE(pwm->hwpwm, TWL4030_PWMXCLK_ENABLE);

	ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, val, TWL4030_GPBR1_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to enable PWM\n", pwm->label);

	val |= TWL4030_PWM_TOGGLE(pwm->hwpwm, TWL4030_PWMX_ENABLE);

	ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, val, TWL4030_GPBR1_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to enable PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
	return ret;
}

static void twl4030_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwm_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL4030_MODULE_INTBR, &val, TWL4030_GPBR1_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read GPBR1\n", pwm->label);
		goto out;
	}

	val &= ~TWL4030_PWM_TOGGLE(pwm->hwpwm, TWL4030_PWMX_ENABLE);

	ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, val, TWL4030_GPBR1_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);

	val &= ~TWL4030_PWM_TOGGLE(pwm->hwpwm, TWL4030_PWMXCLK_ENABLE);

	ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, val, TWL4030_GPBR1_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
}

static int twl4030_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwm_chip *twl = to_twl(chip);
	int ret;
	u8 val, mask, bits;

	if (pwm->hwpwm == 1) {
		mask = TWL4030_GPIO7_VIBRASYNC_PWM1_MASK;
		bits = TWL4030_GPIO7_VIBRASYNC_PWM1_PWM1;
	} else {
		mask = TWL4030_GPIO6_PWM0_MUTE_MASK;
		bits = TWL4030_GPIO6_PWM0_MUTE_PWM0;
	}

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL4030_MODULE_INTBR, &val, TWL4030_PMBR1_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read PMBR1\n", pwm->label);
		goto out;
	}

	/* Save the current MUX configuration for the PWM */
	twl->twl4030_pwm_mux &= ~mask;
	twl->twl4030_pwm_mux |= (val & mask);

	/* Select PWM functionality */
	val &= ~mask;
	val |= bits;

	ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, val, TWL4030_PMBR1_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to request PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
	return ret;
}

static void twl4030_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwm_chip *twl = to_twl(chip);
	int ret;
	u8 val, mask;

	if (pwm->hwpwm == 1)
		mask = TWL4030_GPIO7_VIBRASYNC_PWM1_MASK;
	else
		mask = TWL4030_GPIO6_PWM0_MUTE_MASK;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL4030_MODULE_INTBR, &val, TWL4030_PMBR1_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read PMBR1\n", pwm->label);
		goto out;
	}

	/* Restore the MUX configuration for the PWM */
	val &= ~mask;
	val |= (twl->twl4030_pwm_mux & mask);

	ret = twl_i2c_write_u8(TWL4030_MODULE_INTBR, val, TWL4030_PMBR1_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to free PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
}

static int twl6030_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwm_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	val = twl->twl6030_toggle3;
	val |= TWL6030_PWM_TOGGLE(pwm->hwpwm, TWL6030_PWMXS | TWL6030_PWMXEN);
	val &= ~TWL6030_PWM_TOGGLE(pwm->hwpwm, TWL6030_PWMXR);

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_TOGGLE3_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to enable PWM\n", pwm->label);
		goto out;
	}

	twl->twl6030_toggle3 = val;
out:
	mutex_unlock(&twl->mutex);
	return ret;
}

static void twl6030_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwm_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	val = twl->twl6030_toggle3;
	val |= TWL6030_PWM_TOGGLE(pwm->hwpwm, TWL6030_PWMXR);
	val &= ~TWL6030_PWM_TOGGLE(pwm->hwpwm, TWL6030_PWMXS | TWL6030_PWMXEN);

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_TOGGLE3_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);
		goto out;
	}

	val |= TWL6030_PWM_TOGGLE(pwm->hwpwm, TWL6030_PWMXEN);

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_TOGGLE3_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);
		goto out;
	}

	val &= ~TWL6030_PWM_TOGGLE(pwm->hwpwm, TWL6030_PWMXEN);

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_TOGGLE3_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);
		goto out;
	}

	twl->twl6030_toggle3 = val;
out:
	mutex_unlock(&twl->mutex);
}

static int twl4030_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled)
			twl4030_pwm_disable(chip, pwm);

		return 0;
	}

	err = twl_pwm_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!pwm->state.enabled)
		err = twl4030_pwm_enable(chip, pwm);

	return err;
}

static int twl6030_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled)
			twl6030_pwm_disable(chip, pwm);

		return 0;
	}

	err = twl_pwm_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!pwm->state.enabled)
		err = twl6030_pwm_enable(chip, pwm);

	return err;
}

static const struct pwm_ops twl4030_pwm_ops = {
	.apply = twl4030_pwm_apply,
	.request = twl4030_pwm_request,
	.free = twl4030_pwm_free,
};

static const struct pwm_ops twl6030_pwm_ops = {
	.apply = twl6030_pwm_apply,
};

static int twl_pwm_probe(struct platform_device *pdev)
{
	struct twl_pwm_chip *twl;

	twl = devm_kzalloc(&pdev->dev, sizeof(*twl), GFP_KERNEL);
	if (!twl)
		return -ENOMEM;

	if (twl_class_is_4030())
		twl->chip.ops = &twl4030_pwm_ops;
	else
		twl->chip.ops = &twl6030_pwm_ops;

	twl->chip.dev = &pdev->dev;
	twl->chip.npwm = 2;

	mutex_init(&twl->mutex);

	return devm_pwmchip_add(&pdev->dev, &twl->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id twl_pwm_of_match[] = {
	{ .compatible = "ti,twl4030-pwm" },
	{ .compatible = "ti,twl6030-pwm" },
	{ },
};
MODULE_DEVICE_TABLE(of, twl_pwm_of_match);
#endif

static struct platform_driver twl_pwm_driver = {
	.driver = {
		.name = "twl-pwm",
		.of_match_table = of_match_ptr(twl_pwm_of_match),
	},
	.probe = twl_pwm_probe,
};
module_platform_driver(twl_pwm_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("PWM driver for TWL4030 and TWL6030");
MODULE_ALIAS("platform:twl-pwm");
MODULE_LICENSE("GPL");
