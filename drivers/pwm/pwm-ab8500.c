// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Arun R Murthy <arun.murthy@stericsson.com>
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

struct ab8500_pwm_chip {
	struct pwm_chip chip;
	unsigned int hwid;
};

static struct ab8500_pwm_chip *ab8500_pwm_from_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ab8500_pwm_chip, chip);
}

static int ab8500_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	int ret;
	u8 reg;
	unsigned int higher_val, lower_val;
	struct ab8500_pwm_chip *ab8500 = ab8500_pwm_from_chip(chip);

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		ret = abx500_mask_and_set_register_interruptible(chip->dev,
					AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
					1 << ab8500->hwid, 0);

		if (ret < 0)
			dev_err(chip->dev, "%s: Failed to disable PWM, Error %d\n",
								pwm->label, ret);
		return ret;
	}

	/*
	 * get the first 8 bits that are be written to
	 * AB8500_PWM_OUT_CTRL1_REG[0:7]
	 */
	lower_val = state->duty_cycle & 0x00FF;
	/*
	 * get bits [9:10] that are to be written to
	 * AB8500_PWM_OUT_CTRL2_REG[0:1]
	 */
	higher_val = ((state->duty_cycle & 0x0300) >> 8);

	reg = AB8500_PWM_OUT_CTRL1_REG + (ab8500->hwid * 2);

	ret = abx500_set_register_interruptible(chip->dev, AB8500_MISC,
			reg, (u8)lower_val);
	if (ret < 0)
		return ret;

	ret = abx500_set_register_interruptible(chip->dev, AB8500_MISC,
			(reg + 1), (u8)higher_val);
	if (ret < 0)
		return ret;

	ret = abx500_mask_and_set_register_interruptible(chip->dev,
				AB8500_MISC, AB8500_PWM_OUT_CTRL7_REG,
				1 << ab8500->hwid, 1 << ab8500->hwid);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to enable PWM, Error %d\n",
							pwm->label, ret);

	return ret;
}

static const struct pwm_ops ab8500_pwm_ops = {
	.apply = ab8500_pwm_apply,
	.owner = THIS_MODULE,
};

static int ab8500_pwm_probe(struct platform_device *pdev)
{
	struct ab8500_pwm_chip *ab8500;
	int err;

	if (pdev->id < 1 || pdev->id > 31)
		return dev_err_probe(&pdev->dev, EINVAL, "Invalid device id %d\n", pdev->id);

	/*
	 * Nothing to be done in probe, this is required to get the
	 * device which is required for ab8500 read and write
	 */
	ab8500 = devm_kzalloc(&pdev->dev, sizeof(*ab8500), GFP_KERNEL);
	if (ab8500 == NULL)
		return -ENOMEM;

	ab8500->chip.dev = &pdev->dev;
	ab8500->chip.ops = &ab8500_pwm_ops;
	ab8500->chip.npwm = 1;
	ab8500->hwid = pdev->id - 1;

	err = pwmchip_add(&ab8500->chip);
	if (err < 0)
		return dev_err_probe(&pdev->dev, err, "Failed to add pwm chip\n");

	dev_dbg(&pdev->dev, "pwm probe successful\n");
	platform_set_drvdata(pdev, ab8500);

	return 0;
}

static int ab8500_pwm_remove(struct platform_device *pdev)
{
	struct ab8500_pwm_chip *ab8500 = platform_get_drvdata(pdev);
	int err;

	err = pwmchip_remove(&ab8500->chip);
	if (err < 0)
		return err;

	dev_dbg(&pdev->dev, "pwm driver removed\n");

	return 0;
}

static struct platform_driver ab8500_pwm_driver = {
	.driver = {
		.name = "ab8500-pwm",
	},
	.probe = ab8500_pwm_probe,
	.remove = ab8500_pwm_remove,
};
module_platform_driver(ab8500_pwm_driver);

MODULE_AUTHOR("Arun MURTHY <arun.murthy@stericsson.com>");
MODULE_DESCRIPTION("AB8500 Pulse Width Modulation Driver");
MODULE_ALIAS("platform:ab8500-pwm");
MODULE_LICENSE("GPL v2");
