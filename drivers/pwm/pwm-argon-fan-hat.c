// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Marek Vasut
 *
 * Limitations:
 * - no support for offset/polarity
 * - fixed 30 kHz period
 *
 * Argon Fan HAT https://argon40.com/products/argon-fan-hat
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pwm.h>

#define ARGON40_FAN_HAT_PERIOD_NS	33333	/* ~30 kHz */

#define ARGON40_FAN_HAT_REG_DUTY_CYCLE	0x80

static int argon_fan_hat_round_waveform_tohw(struct pwm_chip *chip,
					     struct pwm_device *pwm,
					     const struct pwm_waveform *wf,
					     void *_wfhw)
{
	u8 *wfhw = _wfhw;

	if (wf->duty_length_ns > ARGON40_FAN_HAT_PERIOD_NS)
		*wfhw = 100;
	else
		*wfhw = mul_u64_u64_div_u64(wf->duty_length_ns, 100, ARGON40_FAN_HAT_PERIOD_NS);

	return 0;
}

static int argon_fan_hat_round_waveform_fromhw(struct pwm_chip *chip,
					       struct pwm_device *pwm,
					       const void *_wfhw,
					       struct pwm_waveform *wf)
{
	const u8 *wfhw = _wfhw;

	wf->period_length_ns = ARGON40_FAN_HAT_PERIOD_NS;
	wf->duty_length_ns = DIV64_U64_ROUND_UP(wf->period_length_ns * *wfhw, 100);
	wf->duty_offset_ns = 0;

	return 0;
}

static int argon_fan_hat_write_waveform(struct pwm_chip *chip,
					struct pwm_device *pwm,
					const void *_wfhw)
{
	struct i2c_client *i2c = pwmchip_get_drvdata(chip);
	const u8 *wfhw = _wfhw;

	return i2c_smbus_write_byte_data(i2c, ARGON40_FAN_HAT_REG_DUTY_CYCLE, *wfhw);
}

static const struct pwm_ops argon_fan_hat_pwm_ops = {
	.sizeof_wfhw = sizeof(u8),
	.round_waveform_fromhw = argon_fan_hat_round_waveform_fromhw,
	.round_waveform_tohw = argon_fan_hat_round_waveform_tohw,
	.write_waveform = argon_fan_hat_write_waveform,
	/*
	 * The controller does not provide any way to read info back,
	 * reading from the controller stops the fan, therefore there
	 * is no .read_waveform here.
	 */
};

static int argon_fan_hat_i2c_probe(struct i2c_client *i2c)
{
	struct pwm_chip *chip = devm_pwmchip_alloc(&i2c->dev, 1, 0);
	int ret;

	if (IS_ERR(chip))
		return PTR_ERR(chip);

	chip->ops = &argon_fan_hat_pwm_ops;
	pwmchip_set_drvdata(chip, i2c);

	ret = devm_pwmchip_add(&i2c->dev, chip);
	if (ret)
		return dev_err_probe(&i2c->dev, ret, "Could not add PWM chip\n");

	return 0;
}

static const struct of_device_id argon_fan_hat_dt_ids[] = {
	{ .compatible = "argon40,fan-hat" },
	{ },
};
MODULE_DEVICE_TABLE(of, argon_fan_hat_dt_ids);

static struct i2c_driver argon_fan_hat_driver = {
	.driver = {
		.name = "argon-fan-hat",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = argon_fan_hat_dt_ids,
	},
	.probe = argon_fan_hat_i2c_probe,
};

module_i2c_driver(argon_fan_hat_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut+renesas@mailbox.org>");
MODULE_DESCRIPTION("Argon40 Fan HAT");
MODULE_LICENSE("GPL");
