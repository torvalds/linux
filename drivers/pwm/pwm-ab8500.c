// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
 * Datasheet: https://web.archive.org/web/20130614115108/http://www.stericsson.com/developers/CD00291561_UM1031_AB8500_user_manual-rev5_CTDS_public.pdf
 */
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/module.h>

/*
 * PWM Out generators
 * Bank: 0x10
 */
#define AB8500_PWM_OUT_CTRL1_REG	0x60
#define AB8500_PWM_OUT_CTRL2_REG	0x61
#define AB8500_PWM_OUT_CTRL7_REG	0x66

#define AB8500_PWM_CLKRATE 9600000

struct ab8500_pwm_chip {
	unsigned int hwid;
};

static struct ab8500_pwm_chip *ab8500_pwm_from_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int ab8500_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	int ret;
	u8 reg;
	u8 higher_val, lower_val;
	unsigned int duty_steps, div;
	struct ab8500_pwm_chip *ab8500 = ab8500_pwm_from_chip(chip);

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (state->enabled) {
		/*
		 * A time quantum is
		 *   q = (32 - FreqPWMOutx[3:0]) / AB8500_PWM_CLKRATE
		 * The period is always 1024 q, duty_cycle is between 1q and 1024q.
		 *
		 * FreqPWMOutx[3:0] | output frequency | output frequency | 1024q = period
		 *                  | (from manual)    |   (1 / 1024q)    | = 1 / freq
		 * -----------------+------------------+------------------+--------------
		 *      b0000       |      293 Hz      |  292.968750 Hz   | 3413333.33 ns
		 *      b0001       |      302 Hz      |  302.419355 Hz   | 3306666.66 ns
		 *      b0010       |      312 Hz      |  312.500000 Hz   | 3200000    ns
		 *      b0011       |      323 Hz      |  323.275862 Hz   | 3093333.33 ns
		 *      b0100       |      334 Hz      |  334.821429 Hz   | 2986666.66 ns
		 *      b0101       |      347 Hz      |  347.222222 Hz   | 2880000    ns
		 *      b0110       |      360 Hz      |  360.576923 Hz   | 2773333.33 ns
		 *      b0111       |      375 Hz      |  375.000000 Hz   | 2666666.66 ns
		 *      b1000       |      390 Hz      |  390.625000 Hz   | 2560000    ns
		 *      b1001       |      407 Hz      |  407.608696 Hz   | 2453333.33 ns
		 *      b1010       |      426 Hz      |  426.136364 Hz   | 2346666.66 ns
		 *      b1011       |      446 Hz      |  446.428571 Hz   | 2240000    ns
		 *      b1100       |      468 Hz      |  468.750000 Hz   | 2133333.33 ns
		 *      b1101       |      493 Hz      |  493.421053 Hz   | 2026666.66 ns
		 *      b1110       |      520 Hz      |  520.833333 Hz   | 1920000    ns
		 *      b1111       |      551 Hz      |  551.470588 Hz   | 1813333.33 ns
		 *
		 *
		 * AB8500_PWM_CLKRATE is a multiple of 1024, so the division by
		 * 1024 can be done in this factor without loss of precision.
		 */
		div = min_t(u64, mul_u64_u64_div_u64(state->period,
						     AB8500_PWM_CLKRATE >> 10,
						     NSEC_PER_SEC), 32); /* 32 - FreqPWMOutx[3:0] */
		if (div <= 16)
			/* requested period < 3413333.33 */
			return -EINVAL;

		duty_steps = max_t(u64, mul_u64_u64_div_u64(state->duty_cycle,
							    AB8500_PWM_CLKRATE,
							    (u64)NSEC_PER_SEC * div), 1024);
	}

	/*
	 * The hardware doesn't support duty_steps = 0 explicitly, but emits low
	 * when disabled.
	 */
	if (!state->enabled || duty_steps == 0) {
		ret = abx500_mask_and_set_register_interruptible(pwmchip_parent(chip),
					AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
					1 << ab8500->hwid, 0);

		if (ret < 0)
			dev_err(pwmchip_parent(chip), "%s: Failed to disable PWM, Error %d\n",
								pwm->label, ret);
		return ret;
	}

	/*
	 * The lower 8 bits of duty_steps is written to ...
	 * AB8500_PWM_OUT_CTRL1_REG[0:7]
	 */
	lower_val = (duty_steps - 1) & 0x00ff;
	/*
	 * The two remaining high bits to
	 * AB8500_PWM_OUT_CTRL2_REG[0:1]; together with FreqPWMOutx.
	 */
	higher_val = ((duty_steps - 1) & 0x0300) >> 8 | (32 - div) << 4;

	reg = AB8500_PWM_OUT_CTRL1_REG + (ab8500->hwid * 2);

	ret = abx500_set_register_interruptible(pwmchip_parent(chip), AB8500_MISC,
			reg, lower_val);
	if (ret < 0)
		return ret;

	ret = abx500_set_register_interruptible(pwmchip_parent(chip), AB8500_MISC,
			(reg + 1), higher_val);
	if (ret < 0)
		return ret;

	/* enable */
	ret = abx500_mask_and_set_register_interruptible(pwmchip_parent(chip),
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << ab8500->hwid, 1 << ab8500->hwid);
	if (ret < 0)
		dev_err(pwmchip_parent(chip), "%s: Failed to enable PWM, Error %d\n",
							pwm->label, ret);

	return ret;
}

static int ab8500_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	u8 ctrl7, lower_val, higher_val;
	int ret;
	struct ab8500_pwm_chip *ab8500 = ab8500_pwm_from_chip(chip);
	unsigned int div, duty_steps;

	ret = abx500_get_register_interruptible(pwmchip_parent(chip), AB8500_MISC,
						AB8500_PWM_OUT_CTRL7_REG,
						&ctrl7);
	if (ret)
		return ret;

	state->polarity = PWM_POLARITY_NORMAL;

	if (!(ctrl7 & 1 << ab8500->hwid)) {
		state->enabled = false;
		return 0;
	}

	ret = abx500_get_register_interruptible(pwmchip_parent(chip), AB8500_MISC,
						AB8500_PWM_OUT_CTRL1_REG + (ab8500->hwid * 2),
						&lower_val);
	if (ret)
		return ret;

	ret = abx500_get_register_interruptible(pwmchip_parent(chip), AB8500_MISC,
						AB8500_PWM_OUT_CTRL2_REG + (ab8500->hwid * 2),
						&higher_val);
	if (ret)
		return ret;

	div = 32 - ((higher_val & 0xf0) >> 4);
	duty_steps = ((higher_val & 3) << 8 | lower_val) + 1;

	state->period = DIV64_U64_ROUND_UP((u64)div << 10, AB8500_PWM_CLKRATE);
	state->duty_cycle = DIV64_U64_ROUND_UP((u64)div * duty_steps, AB8500_PWM_CLKRATE);

	return 0;
}

static const struct pwm_ops ab8500_pwm_ops = {
	.apply = ab8500_pwm_apply,
	.get_state = ab8500_pwm_get_state,
};

static int ab8500_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct ab8500_pwm_chip *ab8500;
	int err;

	if (pdev->id < 1 || pdev->id > 31)
		return dev_err_probe(&pdev->dev, -EINVAL, "Invalid device id %d\n", pdev->id);

	/*
	 * Nothing to be done in probe, this is required to get the
	 * device which is required for ab8500 read and write
	 */
	chip = devm_pwmchip_alloc(&pdev->dev, 1, sizeof(*ab8500));
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	ab8500 = ab8500_pwm_from_chip(chip);

	chip->ops = &ab8500_pwm_ops;
	ab8500->hwid = pdev->id - 1;

	err = devm_pwmchip_add(&pdev->dev, chip);
	if (err < 0)
		return dev_err_probe(&pdev->dev, err, "Failed to add pwm chip\n");

	dev_dbg(&pdev->dev, "pwm probe successful\n");

	return 0;
}

static struct platform_driver ab8500_pwm_driver = {
	.driver = {
		.name = "ab8500-pwm",
	},
	.probe = ab8500_pwm_probe,
};
module_platform_driver(ab8500_pwm_driver);

MODULE_AUTHOR("Arun MURTHY <arun.murthy@stericsson.com>");
MODULE_DESCRIPTION("AB8500 Pulse Width Modulation Driver");
MODULE_ALIAS("platform:ab8500-pwm");
MODULE_LICENSE("GPL v2");
