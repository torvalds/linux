/*
 * twl6030_pwm.c
 * Driver for PHOENIX (TWL6030) Pulse Width Modulator
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/i2c/twl.h>
#include <linux/slab.h>

#define LED_PWM_CTRL1	0xF4
#define LED_PWM_CTRL2	0xF5

/* Max value for CTRL1 register */
#define PWM_CTRL1_MAX	255

/* Pull down disable */
#define PWM_CTRL2_DIS_PD	(1 << 6)

/* Current control 2.5 milli Amps */
#define PWM_CTRL2_CURR_02	(2 << 4)

/* LED supply source */
#define PWM_CTRL2_SRC_VAC	(1 << 2)

/* LED modes */
#define PWM_CTRL2_MODE_HW	(0 << 0)
#define PWM_CTRL2_MODE_SW	(1 << 0)
#define PWM_CTRL2_MODE_DIS	(2 << 0)

#define PWM_CTRL2_MODE_MASK	0x3

struct twl6030_pwm_chip {
	struct pwm_chip chip;
};

static int twl6030_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret;
	u8 val;

	/* Configure PWM */
	val = PWM_CTRL2_DIS_PD | PWM_CTRL2_CURR_02 | PWM_CTRL2_SRC_VAC |
	      PWM_CTRL2_MODE_HW;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to configure PWM, Error %d\n",
			pwm->label, ret);
		return ret;
	}

	return 0;
}

static int twl6030_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	u8 duty_cycle = (duty_ns * PWM_CTRL1_MAX) / period_ns;
	int ret;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, duty_cycle, LED_PWM_CTRL1);
	if (ret < 0) {
		pr_err("%s: Failed to configure PWM, Error %d\n",
			pwm->label, ret);
		return ret;
	}

	return 0;
}

static int twl6030_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret;
	u8 val;

	ret = twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to enable PWM, Error %d\n",
			pwm->label, ret);
		return ret;
	}

	/* Change mode to software control */
	val &= ~PWM_CTRL2_MODE_MASK;
	val |= PWM_CTRL2_MODE_SW;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to enable PWM, Error %d\n",
			pwm->label, ret);
		return ret;
	}

	twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, LED_PWM_CTRL2);
	return 0;
}

static void twl6030_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret;
	u8 val;

	ret = twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to disable PWM, Error %d\n",
			pwm->label, ret);
		return;
	}

	val &= ~PWM_CTRL2_MODE_MASK;
	val |= PWM_CTRL2_MODE_HW;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to disable PWM, Error %d\n",
			pwm->label, ret);
	}
}

static const struct pwm_ops twl6030_pwm_ops = {
	.request = twl6030_pwm_request,
	.config = twl6030_pwm_config,
	.enable = twl6030_pwm_enable,
	.disable = twl6030_pwm_disable,
};

static int twl6030_pwm_probe(struct platform_device *pdev)
{
	struct twl6030_pwm_chip *twl6030;
	int ret;

	twl6030 = devm_kzalloc(&pdev->dev, sizeof(*twl6030), GFP_KERNEL);
	if (!twl6030)
		return -ENOMEM;

	twl6030->chip.dev = &pdev->dev;
	twl6030->chip.ops = &twl6030_pwm_ops;
	twl6030->chip.base = -1;
	twl6030->chip.npwm = 1;

	ret = pwmchip_add(&twl6030->chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, twl6030);

	return 0;
}

static int twl6030_pwm_remove(struct platform_device *pdev)
{
	struct twl6030_pwm_chip *twl6030 = platform_get_drvdata(pdev);

	return pwmchip_remove(&twl6030->chip);
}

static struct platform_driver twl6030_pwm_driver = {
	.driver = {
		.name = "twl6030-pwm",
	},
	.probe = twl6030_pwm_probe,
	.remove = __devexit_p(twl6030_pwm_remove),
};
module_platform_driver(twl6030_pwm_driver);

MODULE_ALIAS("platform:twl6030-pwm");
MODULE_LICENSE("GPL");
