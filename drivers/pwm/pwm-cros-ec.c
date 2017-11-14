/*
 * Copyright (C) 2016 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2, as published by
 * the Free Software Foundation.
 *
 * Expose a PWM controlled by the ChromeOS EC to the host processor.
 */

#include <linux/module.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

/**
 * struct cros_ec_pwm_device - Driver data for EC PWM
 *
 * @dev: Device node
 * @ec: Pointer to EC device
 * @chip: PWM controller chip
 */
struct cros_ec_pwm_device {
	struct device *dev;
	struct cros_ec_device *ec;
	struct pwm_chip chip;
};

static inline struct cros_ec_pwm_device *pwm_to_cros_ec_pwm(struct pwm_chip *c)
{
	return container_of(c, struct cros_ec_pwm_device, chip);
}

static int cros_ec_pwm_set_duty(struct cros_ec_device *ec, u8 index, u16 duty)
{
	struct {
		struct cros_ec_command msg;
		struct ec_params_pwm_set_duty params;
	} __packed buf;
	struct ec_params_pwm_set_duty *params = &buf.params;
	struct cros_ec_command *msg = &buf.msg;

	memset(&buf, 0, sizeof(buf));

	msg->version = 0;
	msg->command = EC_CMD_PWM_SET_DUTY;
	msg->insize = 0;
	msg->outsize = sizeof(*params);

	params->duty = duty;
	params->pwm_type = EC_PWM_TYPE_GENERIC;
	params->index = index;

	return cros_ec_cmd_xfer_status(ec, msg);
}

static int __cros_ec_pwm_get_duty(struct cros_ec_device *ec, u8 index,
				  u32 *result)
{
	struct {
		struct cros_ec_command msg;
		union {
			struct ec_params_pwm_get_duty params;
			struct ec_response_pwm_get_duty resp;
		};
	} __packed buf;
	struct ec_params_pwm_get_duty *params = &buf.params;
	struct ec_response_pwm_get_duty *resp = &buf.resp;
	struct cros_ec_command *msg = &buf.msg;
	int ret;

	memset(&buf, 0, sizeof(buf));

	msg->version = 0;
	msg->command = EC_CMD_PWM_GET_DUTY;
	msg->insize = sizeof(*resp);
	msg->outsize = sizeof(*params);

	params->pwm_type = EC_PWM_TYPE_GENERIC;
	params->index = index;

	ret = cros_ec_cmd_xfer_status(ec, msg);
	if (result)
		*result = msg->result;
	if (ret < 0)
		return ret;

	return resp->duty;
}

static int cros_ec_pwm_get_duty(struct cros_ec_device *ec, u8 index)
{
	return __cros_ec_pwm_get_duty(ec, index, NULL);
}

static int cros_ec_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct cros_ec_pwm_device *ec_pwm = pwm_to_cros_ec_pwm(chip);
	int duty_cycle;

	/* The EC won't let us change the period */
	if (state->period != EC_PWM_MAX_DUTY)
		return -EINVAL;

	/*
	 * EC doesn't separate the concept of duty cycle and enabled, but
	 * kernel does. Translate.
	 */
	duty_cycle = state->enabled ? state->duty_cycle : 0;

	return cros_ec_pwm_set_duty(ec_pwm->ec, pwm->hwpwm, duty_cycle);
}

static void cros_ec_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct cros_ec_pwm_device *ec_pwm = pwm_to_cros_ec_pwm(chip);
	int ret;

	ret = cros_ec_pwm_get_duty(ec_pwm->ec, pwm->hwpwm);
	if (ret < 0) {
		dev_err(chip->dev, "error getting initial duty: %d\n", ret);
		return;
	}

	state->enabled = (ret > 0);
	state->period = EC_PWM_MAX_DUTY;

	/* Note that "disabled" and "duty cycle == 0" are treated the same */
	state->duty_cycle = ret;
}

static struct pwm_device *
cros_ec_pwm_xlate(struct pwm_chip *pc, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	if (args->args[0] >= pc->npwm)
		return ERR_PTR(-EINVAL);

	pwm = pwm_request_from_chip(pc, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	/* The EC won't let us change the period */
	pwm->args.period = EC_PWM_MAX_DUTY;

	return pwm;
}

static const struct pwm_ops cros_ec_pwm_ops = {
	.get_state	= cros_ec_pwm_get_state,
	.apply		= cros_ec_pwm_apply,
	.owner		= THIS_MODULE,
};

static int cros_ec_num_pwms(struct cros_ec_device *ec)
{
	int i, ret;

	/* The index field is only 8 bits */
	for (i = 0; i <= U8_MAX; i++) {
		u32 result = 0;

		ret = __cros_ec_pwm_get_duty(ec, i, &result);
		/* We want to parse EC protocol errors */
		if (ret < 0 && !(ret == -EPROTO && result))
			return ret;

		/*
		 * We look for SUCCESS, INVALID_COMMAND, or INVALID_PARAM
		 * responses; everything else is treated as an error.
		 */
		if (result == EC_RES_INVALID_COMMAND)
			return -ENODEV;
		else if (result == EC_RES_INVALID_PARAM)
			return i;
		else if (result)
			return -EPROTO;
	}

	return U8_MAX;
}

static int cros_ec_pwm_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct cros_ec_pwm_device *ec_pwm;
	struct pwm_chip *chip;
	int ret;

	if (!ec) {
		dev_err(dev, "no parent EC device\n");
		return -EINVAL;
	}

	ec_pwm = devm_kzalloc(dev, sizeof(*ec_pwm), GFP_KERNEL);
	if (!ec_pwm)
		return -ENOMEM;
	chip = &ec_pwm->chip;
	ec_pwm->ec = ec;

	/* PWM chip */
	chip->dev = dev;
	chip->ops = &cros_ec_pwm_ops;
	chip->of_xlate = cros_ec_pwm_xlate;
	chip->of_pwm_n_cells = 1;
	chip->base = -1;
	ret = cros_ec_num_pwms(ec);
	if (ret < 0) {
		dev_err(dev, "Couldn't find PWMs: %d\n", ret);
		return ret;
	}
	chip->npwm = ret;
	dev_dbg(dev, "Probed %u PWMs\n", chip->npwm);

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(dev, "cannot register PWM: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, ec_pwm);

	return ret;
}

static int cros_ec_pwm_remove(struct platform_device *dev)
{
	struct cros_ec_pwm_device *ec_pwm = platform_get_drvdata(dev);
	struct pwm_chip *chip = &ec_pwm->chip;

	return pwmchip_remove(chip);
}

#ifdef CONFIG_OF
static const struct of_device_id cros_ec_pwm_of_match[] = {
	{ .compatible = "google,cros-ec-pwm" },
	{},
};
MODULE_DEVICE_TABLE(of, cros_ec_pwm_of_match);
#endif

static struct platform_driver cros_ec_pwm_driver = {
	.probe = cros_ec_pwm_probe,
	.remove = cros_ec_pwm_remove,
	.driver = {
		.name = "cros-ec-pwm",
		.of_match_table = of_match_ptr(cros_ec_pwm_of_match),
	},
};
module_platform_driver(cros_ec_pwm_driver);

MODULE_ALIAS("platform:cros-ec-pwm");
MODULE_DESCRIPTION("ChromeOS EC PWM driver");
MODULE_LICENSE("GPL v2");
