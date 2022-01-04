// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The Netronix embedded controller is a microcontroller found in some
 * e-book readers designed by the original design manufacturer Netronix, Inc.
 * It contains RTC, battery monitoring, system power management, and PWM
 * functionality.
 *
 * This driver implements PWM output.
 *
 * Copyright 2020 Jonathan Neuschäfer <j.neuschaefer@gmx.net>
 *
 * Limitations:
 * - The get_state callback is not implemented, because the current state of
 *   the PWM output can't be read back from the hardware.
 * - The hardware can only generate normal polarity output.
 * - The period and duty cycle can't be changed together in one atomic action.
 */

#include <linux/mfd/ntxec.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/types.h>

struct ntxec_pwm {
	struct device *dev;
	struct ntxec *ec;
	struct pwm_chip chip;
};

static struct ntxec_pwm *ntxec_pwm_from_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ntxec_pwm, chip);
}

#define NTXEC_REG_AUTO_OFF_HI	0xa1
#define NTXEC_REG_AUTO_OFF_LO	0xa2
#define NTXEC_REG_ENABLE	0xa3
#define NTXEC_REG_PERIOD_LOW	0xa4
#define NTXEC_REG_PERIOD_HIGH	0xa5
#define NTXEC_REG_DUTY_LOW	0xa6
#define NTXEC_REG_DUTY_HIGH	0xa7

/*
 * The time base used in the EC is 8MHz, or 125ns. Period and duty cycle are
 * measured in this unit.
 */
#define TIME_BASE_NS 125

/*
 * The maximum input value (in nanoseconds) is determined by the time base and
 * the range of the hardware registers that hold the converted value.
 * It fits into 32 bits, so we can do our calculations in 32 bits as well.
 */
#define MAX_PERIOD_NS (TIME_BASE_NS * 0xffff)

static int ntxec_pwm_set_raw_period_and_duty_cycle(struct pwm_chip *chip,
						   int period, int duty)
{
	struct ntxec_pwm *priv = ntxec_pwm_from_chip(chip);

	/*
	 * Changes to the period and duty cycle take effect as soon as the
	 * corresponding low byte is written, so the hardware may be configured
	 * to an inconsistent state after the period is written and before the
	 * duty cycle is fully written. If, in such a case, the old duty cycle
	 * is longer than the new period, the EC may output 100% for a moment.
	 *
	 * To minimize the time between the changes to period and duty cycle
	 * taking effect, the writes are interleaved.
	 */

	struct reg_sequence regs[] = {
		{ NTXEC_REG_PERIOD_HIGH, ntxec_reg8(period >> 8) },
		{ NTXEC_REG_DUTY_HIGH, ntxec_reg8(duty >> 8) },
		{ NTXEC_REG_PERIOD_LOW, ntxec_reg8(period) },
		{ NTXEC_REG_DUTY_LOW, ntxec_reg8(duty) },
	};

	return regmap_multi_reg_write(priv->ec->regmap, regs, ARRAY_SIZE(regs));
}

static int ntxec_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm_dev,
			   const struct pwm_state *state)
{
	struct ntxec_pwm *priv = ntxec_pwm_from_chip(chip);
	unsigned int period, duty;
	int res;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	period = min_t(u64, state->period, MAX_PERIOD_NS);
	duty   = min_t(u64, state->duty_cycle, period);

	period /= TIME_BASE_NS;
	duty   /= TIME_BASE_NS;

	/*
	 * Writing a duty cycle of zero puts the device into a state where
	 * writing a higher duty cycle doesn't result in the brightness that it
	 * usually results in. This can be fixed by cycling the ENABLE register.
	 *
	 * As a workaround, write ENABLE=0 when the duty cycle is zero.
	 * The case that something has previously set the duty cycle to zero
	 * but ENABLE=1, is not handled.
	 */
	if (state->enabled && duty != 0) {
		res = ntxec_pwm_set_raw_period_and_duty_cycle(chip, period, duty);
		if (res)
			return res;

		res = regmap_write(priv->ec->regmap, NTXEC_REG_ENABLE, ntxec_reg8(1));
		if (res)
			return res;

		/* Disable the auto-off timer */
		res = regmap_write(priv->ec->regmap, NTXEC_REG_AUTO_OFF_HI, ntxec_reg8(0xff));
		if (res)
			return res;

		return regmap_write(priv->ec->regmap, NTXEC_REG_AUTO_OFF_LO, ntxec_reg8(0xff));
	} else {
		return regmap_write(priv->ec->regmap, NTXEC_REG_ENABLE, ntxec_reg8(0));
	}
}

static const struct pwm_ops ntxec_pwm_ops = {
	.owner = THIS_MODULE,
	.apply = ntxec_pwm_apply,
	/*
	 * No .get_state callback, because the current state cannot be read
	 * back from the hardware.
	 */
};

static int ntxec_pwm_probe(struct platform_device *pdev)
{
	struct ntxec *ec = dev_get_drvdata(pdev->dev.parent);
	struct ntxec_pwm *priv;
	struct pwm_chip *chip;

	pdev->dev.of_node = pdev->dev.parent->of_node;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ec = ec;
	priv->dev = &pdev->dev;

	platform_set_drvdata(pdev, priv);

	chip = &priv->chip;
	chip->dev = &pdev->dev;
	chip->ops = &ntxec_pwm_ops;
	chip->base = -1;
	chip->npwm = 1;

	return pwmchip_add(chip);
}

static int ntxec_pwm_remove(struct platform_device *pdev)
{
	struct ntxec_pwm *priv = platform_get_drvdata(pdev);
	struct pwm_chip *chip = &priv->chip;

	return pwmchip_remove(chip);
}

static struct platform_driver ntxec_pwm_driver = {
	.driver = {
		.name = "ntxec-pwm",
	},
	.probe = ntxec_pwm_probe,
	.remove = ntxec_pwm_remove,
};
module_platform_driver(ntxec_pwm_driver);

MODULE_AUTHOR("Jonathan Neuschäfer <j.neuschaefer@gmx.net>");
MODULE_DESCRIPTION("PWM driver for Netronix EC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ntxec-pwm");
