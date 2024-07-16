// SPDX-License-Identifier: GPL-2.0
/*
 * Expose a PWM controlled by the ChromeOS EC to the host processor.
 *
 * Copyright (C) 2016 Google, Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#include <dt-bindings/mfd/cros_ec.h>

/**
 * struct cros_ec_pwm_device - Driver data for EC PWM
 *
 * @ec: Pointer to EC device
 * @use_pwm_type: Use PWM types instead of generic channels
 */
struct cros_ec_pwm_device {
	struct cros_ec_device *ec;
	bool use_pwm_type;
};

static inline struct cros_ec_pwm_device *pwm_to_cros_ec_pwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int cros_ec_dt_type_to_pwm_type(u8 dt_index, u8 *pwm_type)
{
	switch (dt_index) {
	case CROS_EC_PWM_DT_KB_LIGHT:
		*pwm_type = EC_PWM_TYPE_KB_LIGHT;
		return 0;
	case CROS_EC_PWM_DT_DISPLAY_LIGHT:
		*pwm_type = EC_PWM_TYPE_DISPLAY_LIGHT;
		return 0;
	default:
		return -EINVAL;
	}
}

static int cros_ec_pwm_set_duty(struct cros_ec_pwm_device *ec_pwm, u8 index,
				u16 duty)
{
	struct cros_ec_device *ec = ec_pwm->ec;
	struct {
		struct cros_ec_command msg;
		struct ec_params_pwm_set_duty params;
	} __packed buf;
	struct ec_params_pwm_set_duty *params = &buf.params;
	struct cros_ec_command *msg = &buf.msg;
	int ret;

	memset(&buf, 0, sizeof(buf));

	msg->version = 0;
	msg->command = EC_CMD_PWM_SET_DUTY;
	msg->insize = 0;
	msg->outsize = sizeof(*params);

	params->duty = duty;

	if (ec_pwm->use_pwm_type) {
		ret = cros_ec_dt_type_to_pwm_type(index, &params->pwm_type);
		if (ret) {
			dev_err(ec->dev, "Invalid PWM type index: %d\n", index);
			return ret;
		}
		params->index = 0;
	} else {
		params->pwm_type = EC_PWM_TYPE_GENERIC;
		params->index = index;
	}

	return cros_ec_cmd_xfer_status(ec, msg);
}

static int cros_ec_pwm_get_duty(struct cros_ec_device *ec, bool use_pwm_type, u8 index)
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

	if (use_pwm_type) {
		ret = cros_ec_dt_type_to_pwm_type(index, &params->pwm_type);
		if (ret) {
			dev_err(ec->dev, "Invalid PWM type index: %d\n", index);
			return ret;
		}
		params->index = 0;
	} else {
		params->pwm_type = EC_PWM_TYPE_GENERIC;
		params->index = index;
	}

	ret = cros_ec_cmd_xfer_status(ec, msg);
	if (ret < 0)
		return ret;

	return resp->duty;
}

static int cros_ec_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	struct cros_ec_pwm_device *ec_pwm = pwm_to_cros_ec_pwm(chip);
	u16 duty_cycle;
	int ret;

	/* The EC won't let us change the period */
	if (state->period != EC_PWM_MAX_DUTY)
		return -EINVAL;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/*
	 * EC doesn't separate the concept of duty cycle and enabled, but
	 * kernel does. Translate.
	 */
	duty_cycle = state->enabled ? state->duty_cycle : 0;

	ret = cros_ec_pwm_set_duty(ec_pwm, pwm->hwpwm, duty_cycle);
	if (ret < 0)
		return ret;

	return 0;
}

static int cros_ec_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				 struct pwm_state *state)
{
	struct cros_ec_pwm_device *ec_pwm = pwm_to_cros_ec_pwm(chip);
	int ret;

	ret = cros_ec_pwm_get_duty(ec_pwm->ec, ec_pwm->use_pwm_type, pwm->hwpwm);
	if (ret < 0) {
		dev_err(pwmchip_parent(chip), "error getting initial duty: %d\n", ret);
		return ret;
	}

	state->enabled = (ret > 0);
	state->duty_cycle = ret;
	state->period = EC_PWM_MAX_DUTY;
	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops cros_ec_pwm_ops = {
	.get_state	= cros_ec_pwm_get_state,
	.apply		= cros_ec_pwm_apply,
};

/*
 * Determine the number of supported PWMs. The EC does not return the number
 * of PWMs it supports directly, so we have to read the pwm duty cycle for
 * subsequent channels until we get an error.
 */
static int cros_ec_num_pwms(struct cros_ec_device *ec)
{
	int i, ret;

	/* The index field is only 8 bits */
	for (i = 0; i <= U8_MAX; i++) {
		/*
		 * Note that this function is only called when use_pwm_type is
		 * false. With use_pwm_type == true the number of PWMs is fixed.
		 */
		ret = cros_ec_pwm_get_duty(ec, false, i);
		/*
		 * We look for SUCCESS, INVALID_COMMAND, or INVALID_PARAM
		 * responses; everything else is treated as an error.
		 * The EC error codes map to -EOPNOTSUPP and -EINVAL,
		 * so check for those.
		 */
		switch (ret) {
		case -EOPNOTSUPP:	/* invalid command */
			return -ENODEV;
		case -EINVAL:		/* invalid parameter */
			return i;
		default:
			if (ret < 0)
				return ret;
			break;
		}
	}

	return U8_MAX;
}

static int cros_ec_pwm_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct cros_ec_pwm_device *ec_pwm;
	struct pwm_chip *chip;
	bool use_pwm_type = false;
	unsigned int i, npwm;
	int ret;

	if (!ec)
		return dev_err_probe(dev, -EINVAL, "no parent EC device\n");

	if (of_device_is_compatible(np, "google,cros-ec-pwm-type")) {
		use_pwm_type = true;
		npwm = CROS_EC_PWM_DT_COUNT;
	} else {
		ret = cros_ec_num_pwms(ec);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Couldn't find PWMs\n");
		npwm = ret;
	}

	chip = devm_pwmchip_alloc(dev, npwm, sizeof(*ec_pwm));
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	ec_pwm = pwm_to_cros_ec_pwm(chip);
	ec_pwm->use_pwm_type = use_pwm_type;
	ec_pwm->ec = ec;

	/* PWM chip */
	chip->ops = &cros_ec_pwm_ops;

	/*
	 * The device tree binding for this device is special as it only uses a
	 * single cell (for the hwid) and so doesn't provide a default period.
	 * This isn't a big problem though as the hardware only supports a
	 * single period length, it's just a bit ugly to make this fit into the
	 * pwm core abstractions. So initialize the period here, as
	 * of_pwm_xlate_with_flags() won't do that for us.
	 */
	for (i = 0; i < npwm; ++i)
		chip->pwms[i].args.period = EC_PWM_MAX_DUTY;

	dev_dbg(dev, "Probed %u PWMs\n", chip->npwm);

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "cannot register PWM\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id cros_ec_pwm_of_match[] = {
	{ .compatible = "google,cros-ec-pwm" },
	{ .compatible = "google,cros-ec-pwm-type" },
	{},
};
MODULE_DEVICE_TABLE(of, cros_ec_pwm_of_match);
#endif

static struct platform_driver cros_ec_pwm_driver = {
	.probe = cros_ec_pwm_probe,
	.driver = {
		.name = "cros-ec-pwm",
		.of_match_table = of_match_ptr(cros_ec_pwm_of_match),
	},
};
module_platform_driver(cros_ec_pwm_driver);

MODULE_ALIAS("platform:cros-ec-pwm");
MODULE_DESCRIPTION("ChromeOS EC PWM driver");
MODULE_LICENSE("GPL v2");
