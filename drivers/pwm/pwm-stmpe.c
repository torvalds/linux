/*
 * Copyright (C) 2016 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mfd/stmpe.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define STMPE24XX_PWMCS		0x30
#define PWMCS_EN_PWM0		BIT(0)
#define PWMCS_EN_PWM1		BIT(1)
#define PWMCS_EN_PWM2		BIT(2)
#define STMPE24XX_PWMIC0	0x38
#define STMPE24XX_PWMIC1	0x39
#define STMPE24XX_PWMIC2	0x3a

#define STMPE_PWM_24XX_PINBASE	21

struct stmpe_pwm {
	struct stmpe *stmpe;
	struct pwm_chip chip;
	u8 last_duty;
};

static inline struct stmpe_pwm *to_stmpe_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct stmpe_pwm, chip);
}

static int stmpe_24xx_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct stmpe_pwm *stmpe_pwm = to_stmpe_pwm(chip);
	u8 value;
	int ret;

	ret = stmpe_reg_read(stmpe_pwm->stmpe, STMPE24XX_PWMCS);
	if (ret < 0) {
		dev_err(chip->dev, "error reading PWM#%u control\n",
			pwm->hwpwm);
		return ret;
	}

	value = ret | BIT(pwm->hwpwm);

	ret = stmpe_reg_write(stmpe_pwm->stmpe, STMPE24XX_PWMCS, value);
	if (ret) {
		dev_err(chip->dev, "error writing PWM#%u control\n",
			pwm->hwpwm);
		return ret;
	}

	return 0;
}

static void stmpe_24xx_pwm_disable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct stmpe_pwm *stmpe_pwm = to_stmpe_pwm(chip);
	u8 value;
	int ret;

	ret = stmpe_reg_read(stmpe_pwm->stmpe, STMPE24XX_PWMCS);
	if (ret < 0) {
		dev_err(chip->dev, "error reading PWM#%u control\n",
			pwm->hwpwm);
		return;
	}

	value = ret & ~BIT(pwm->hwpwm);

	ret = stmpe_reg_write(stmpe_pwm->stmpe, STMPE24XX_PWMCS, value);
	if (ret) {
		dev_err(chip->dev, "error writing PWM#%u control\n",
			pwm->hwpwm);
		return;
	}
}

/* STMPE 24xx PWM instructions */
#define SMAX		0x007f
#define SMIN		0x00ff
#define GTS		0x0000
#define LOAD		BIT(14) /* Only available on 2403 */
#define RAMPUP		0x0000
#define RAMPDOWN	BIT(7)
#define PRESCALE_512	BIT(14)
#define STEPTIME_1	BIT(8)
#define BRANCH		(BIT(15) | BIT(13))

static int stmpe_24xx_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
				 int duty_ns, int period_ns)
{
	struct stmpe_pwm *stmpe_pwm = to_stmpe_pwm(chip);
	unsigned int i, pin;
	u16 program[3] = {
		SMAX,
		GTS,
		GTS,
	};
	u8 offset;
	int ret;

	/* Make sure we are disabled */
	if (pwm_is_enabled(pwm)) {
		stmpe_24xx_pwm_disable(chip, pwm);
	} else {
		/* Connect the PWM to the pin */
		pin = pwm->hwpwm;

		/* On STMPE2401 and 2403 pins 21,22,23 are used */
		if (stmpe_pwm->stmpe->partnum == STMPE2401 ||
		    stmpe_pwm->stmpe->partnum == STMPE2403)
			pin += STMPE_PWM_24XX_PINBASE;

		ret = stmpe_set_altfunc(stmpe_pwm->stmpe, BIT(pin),
					STMPE_BLOCK_PWM);
		if (ret) {
			dev_err(chip->dev, "unable to connect PWM#%u to pin\n",
				pwm->hwpwm);
			return ret;
		}
	}

	/* STMPE24XX */
	switch (pwm->hwpwm) {
	case 0:
		offset = STMPE24XX_PWMIC0;
		break;

	case 1:
		offset = STMPE24XX_PWMIC1;
		break;

	case 2:
		offset = STMPE24XX_PWMIC2;
		break;

	default:
		/* Should not happen as npwm is 3 */
		return -ENODEV;
	}

	dev_dbg(chip->dev, "PWM#%u: config duty %d ns, period %d ns\n",
		pwm->hwpwm, duty_ns, period_ns);

	if (duty_ns == 0) {
		if (stmpe_pwm->stmpe->partnum == STMPE2401)
			program[0] = SMAX; /* off all the time */

		if (stmpe_pwm->stmpe->partnum == STMPE2403)
			program[0] = LOAD | 0xff; /* LOAD 0xff */

		stmpe_pwm->last_duty = 0x00;
	} else if (duty_ns == period_ns) {
		if (stmpe_pwm->stmpe->partnum == STMPE2401)
			program[0] = SMIN; /* on all the time */

		if (stmpe_pwm->stmpe->partnum == STMPE2403)
			program[0] = LOAD | 0x00; /* LOAD 0x00 */

		stmpe_pwm->last_duty = 0xff;
	} else {
		u8 value, last = stmpe_pwm->last_duty;
		unsigned long duty;

		/*
		 * Counter goes from 0x00 to 0xff repeatedly at 32768 Hz,
		 * (means a period of 30517 ns) then this is compared to the
		 * counter from the ramp, if this is >= PWM counter the output
		 * is high. With LOAD we can define how much of the cycle it
		 * is on.
		 *
		 * Prescale = 0 -> 2 kHz -> T = 1/f = 488281.25 ns
		 */

		/* Scale to 0..0xff */
		duty = duty_ns * 256;
		duty = DIV_ROUND_CLOSEST(duty, period_ns);
		value = duty;

		if (value == last) {
			/* Run the old program */
			if (pwm_is_enabled(pwm))
				stmpe_24xx_pwm_enable(chip, pwm);

			return 0;
		} else if (stmpe_pwm->stmpe->partnum == STMPE2403) {
			/* STMPE2403 can simply set the right PWM value */
			program[0] = LOAD | value;
			program[1] = 0x0000;
		} else if (stmpe_pwm->stmpe->partnum == STMPE2401) {
			/* STMPE2401 need a complex program */
			u16 incdec = 0x0000;

			if (last < value)
				/* Count up */
				incdec = RAMPUP | (value - last);
			else
				/* Count down */
				incdec = RAMPDOWN | (last - value);

			/* Step to desired value, smoothly */
			program[0] = PRESCALE_512 | STEPTIME_1 | incdec;

			/* Loop eternally to 0x00 */
			program[1] = BRANCH;
		}

		dev_dbg(chip->dev,
			"PWM#%u: value = %02x, last_duty = %02x, program=%04x,%04x,%04x\n",
			pwm->hwpwm, value, last, program[0], program[1],
			program[2]);
		stmpe_pwm->last_duty = value;
	}

	/*
	 * We can write programs of up to 64 16-bit words into this channel.
	 */
	for (i = 0; i < ARRAY_SIZE(program); i++) {
		u8 value;

		value = (program[i] >> 8) & 0xff;

		ret = stmpe_reg_write(stmpe_pwm->stmpe, offset, value);
		if (ret) {
			dev_err(chip->dev, "error writing register %02x: %d\n",
				offset, ret);
			return ret;
		}

		value = program[i] & 0xff;

		ret = stmpe_reg_write(stmpe_pwm->stmpe, offset, value);
		if (ret) {
			dev_err(chip->dev, "error writing register %02x: %d\n",
				offset, ret);
			return ret;
		}
	}

	/* If we were enabled, re-enable this PWM */
	if (pwm_is_enabled(pwm))
		stmpe_24xx_pwm_enable(chip, pwm);

	/* Sleep for 200ms so we're sure it will take effect */
	msleep(200);

	dev_dbg(chip->dev, "programmed PWM#%u, %u bytes\n", pwm->hwpwm, i);

	return 0;
}

static const struct pwm_ops stmpe_24xx_pwm_ops = {
	.config = stmpe_24xx_pwm_config,
	.enable = stmpe_24xx_pwm_enable,
	.disable = stmpe_24xx_pwm_disable,
	.owner = THIS_MODULE,
};

static int __init stmpe_pwm_probe(struct platform_device *pdev)
{
	struct stmpe *stmpe = dev_get_drvdata(pdev->dev.parent);
	struct stmpe_pwm *pwm;
	int ret;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->stmpe = stmpe;
	pwm->chip.dev = &pdev->dev;
	pwm->chip.base = -1;

	if (stmpe->partnum == STMPE2401 || stmpe->partnum == STMPE2403) {
		pwm->chip.ops = &stmpe_24xx_pwm_ops;
		pwm->chip.npwm = 3;
	} else {
		if (stmpe->partnum == STMPE1601)
			dev_err(&pdev->dev, "STMPE1601 not yet supported\n");
		else
			dev_err(&pdev->dev, "Unknown STMPE PWM\n");

		return -ENODEV;
	}

	ret = stmpe_enable(stmpe, STMPE_BLOCK_PWM);
	if (ret)
		return ret;

	ret = pwmchip_add(&pwm->chip);
	if (ret) {
		stmpe_disable(stmpe, STMPE_BLOCK_PWM);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);

	return 0;
}

static struct platform_driver stmpe_pwm_driver = {
	.driver = {
		.name = "stmpe-pwm",
	},
};
builtin_platform_driver_probe(stmpe_pwm_driver, stmpe_pwm_probe);
