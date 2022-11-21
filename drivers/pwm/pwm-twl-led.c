// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for TWL4030/6030 Pulse Width Modulator used as LED driver
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This driver is a complete rewrite of the former pwm-twl6030.c authorded by:
 * Hemanth V <hemanthv@ti.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/mfd/twl.h>
#include <linux/slab.h>

/*
 * This driver handles the PWM driven LED terminals of TWL4030 and TWL6030.
 * To generate the signal on TWL4030:
 *  - LEDA uses PWMA
 *  - LEDB uses PWMB
 * TWL6030 has one LED pin with dedicated LEDPWM
 */

#define TWL4030_LED_MAX		0x7f
#define TWL6030_LED_MAX		0xff

/* Registers, bits and macro for TWL4030 */
#define TWL4030_LEDEN_REG	0x00
#define TWL4030_PWMA_REG	0x01

#define TWL4030_LEDXON		(1 << 0)
#define TWL4030_LEDXPWM		(1 << 4)
#define TWL4030_LED_PINS	(TWL4030_LEDXON | TWL4030_LEDXPWM)
#define TWL4030_LED_TOGGLE(led, x)	((x) << (led))

/* Register, bits and macro for TWL6030 */
#define TWL6030_LED_PWM_CTRL1	0xf4
#define TWL6030_LED_PWM_CTRL2	0xf5

#define TWL6040_LED_MODE_HW	0x00
#define TWL6040_LED_MODE_ON	0x01
#define TWL6040_LED_MODE_OFF	0x02
#define TWL6040_LED_MODE_MASK	0x03

struct twl_pwmled_chip {
	struct pwm_chip chip;
	struct mutex mutex;
};

static inline struct twl_pwmled_chip *to_twl(struct pwm_chip *chip)
{
	return container_of(chip, struct twl_pwmled_chip, chip);
}

static int twl4030_pwmled_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	int duty_cycle = DIV_ROUND_UP(duty_ns * TWL4030_LED_MAX, period_ns) + 1;
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
	else if (duty_cycle > TWL4030_LED_MAX)
		duty_cycle = 1;

	base = pwm->hwpwm * 2 + TWL4030_PWMA_REG;

	pwm_config[1] = duty_cycle;

	ret = twl_i2c_write(TWL4030_MODULE_LED, pwm_config, base, 2);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to configure PWM\n", pwm->label);

	return ret;
}

static int twl4030_pwmled_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwmled_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL4030_MODULE_LED, &val, TWL4030_LEDEN_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read LEDEN\n", pwm->label);
		goto out;
	}

	val |= TWL4030_LED_TOGGLE(pwm->hwpwm, TWL4030_LED_PINS);

	ret = twl_i2c_write_u8(TWL4030_MODULE_LED, val, TWL4030_LEDEN_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to enable PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
	return ret;
}

static void twl4030_pwmled_disable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct twl_pwmled_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL4030_MODULE_LED, &val, TWL4030_LEDEN_REG);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read LEDEN\n", pwm->label);
		goto out;
	}

	val &= ~TWL4030_LED_TOGGLE(pwm->hwpwm, TWL4030_LED_PINS);

	ret = twl_i2c_write_u8(TWL4030_MODULE_LED, val, TWL4030_LEDEN_REG);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
}

static int twl6030_pwmled_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	int duty_cycle = (duty_ns * TWL6030_LED_MAX) / period_ns;
	u8 on_time;
	int ret;

	on_time = duty_cycle & 0xff;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, on_time,
			       TWL6030_LED_PWM_CTRL1);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to configure PWM\n", pwm->label);

	return ret;
}

static int twl6030_pwmled_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwmled_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read PWM_CTRL2\n",
			pwm->label);
		goto out;
	}

	val &= ~TWL6040_LED_MODE_MASK;
	val |= TWL6040_LED_MODE_ON;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to enable PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
	return ret;
}

static void twl6030_pwmled_disable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct twl_pwmled_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read PWM_CTRL2\n",
			pwm->label);
		goto out;
	}

	val &= ~TWL6040_LED_MODE_MASK;
	val |= TWL6040_LED_MODE_OFF;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to disable PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
}

static int twl6030_pwmled_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwmled_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read PWM_CTRL2\n",
			pwm->label);
		goto out;
	}

	val &= ~TWL6040_LED_MODE_MASK;
	val |= TWL6040_LED_MODE_OFF;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to request PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
	return ret;
}

static void twl6030_pwmled_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct twl_pwmled_chip *twl = to_twl(chip);
	int ret;
	u8 val;

	mutex_lock(&twl->mutex);
	ret = twl_i2c_read_u8(TWL6030_MODULE_ID1, &val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0) {
		dev_err(chip->dev, "%s: Failed to read PWM_CTRL2\n",
			pwm->label);
		goto out;
	}

	val &= ~TWL6040_LED_MODE_MASK;
	val |= TWL6040_LED_MODE_HW;

	ret = twl_i2c_write_u8(TWL6030_MODULE_ID1, val, TWL6030_LED_PWM_CTRL2);
	if (ret < 0)
		dev_err(chip->dev, "%s: Failed to free PWM\n", pwm->label);

out:
	mutex_unlock(&twl->mutex);
}

static const struct pwm_ops twl4030_pwmled_ops = {
	.enable = twl4030_pwmled_enable,
	.disable = twl4030_pwmled_disable,
	.config = twl4030_pwmled_config,
	.owner = THIS_MODULE,
};

static const struct pwm_ops twl6030_pwmled_ops = {
	.enable = twl6030_pwmled_enable,
	.disable = twl6030_pwmled_disable,
	.config = twl6030_pwmled_config,
	.request = twl6030_pwmled_request,
	.free = twl6030_pwmled_free,
	.owner = THIS_MODULE,
};

static int twl_pwmled_probe(struct platform_device *pdev)
{
	struct twl_pwmled_chip *twl;

	twl = devm_kzalloc(&pdev->dev, sizeof(*twl), GFP_KERNEL);
	if (!twl)
		return -ENOMEM;

	if (twl_class_is_4030()) {
		twl->chip.ops = &twl4030_pwmled_ops;
		twl->chip.npwm = 2;
	} else {
		twl->chip.ops = &twl6030_pwmled_ops;
		twl->chip.npwm = 1;
	}

	twl->chip.dev = &pdev->dev;

	mutex_init(&twl->mutex);

	return devm_pwmchip_add(&pdev->dev, &twl->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id twl_pwmled_of_match[] = {
	{ .compatible = "ti,twl4030-pwmled" },
	{ .compatible = "ti,twl6030-pwmled" },
	{ },
};
MODULE_DEVICE_TABLE(of, twl_pwmled_of_match);
#endif

static struct platform_driver twl_pwmled_driver = {
	.driver = {
		.name = "twl-pwmled",
		.of_match_table = of_match_ptr(twl_pwmled_of_match),
	},
	.probe = twl_pwmled_probe,
};
module_platform_driver(twl_pwmled_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("PWM driver for TWL4030 and TWL6030 LED outputs");
MODULE_ALIAS("platform:twl-pwmled");
MODULE_LICENSE("GPL");
