// SPDX-License-Identifier: GPL-2.0
/*
 * Expose a PWM controlled by the ChromeOS EC to the host processor.
 *
 * Copyright (C) 2016 Google, Inc.
 */

#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#include <dt-bindings/mfd/cros_ec.h>

/**
 * struct cros_ec_pwm_device - Driver data for EC PWM
 *
 * @dev: Device node
 * @ec: Pointer to EC device
 * @chip: PWM controller chip
 * @use_pwm_type: Use PWM types instead of generic channels
 */
struct cros_ec_pwm_device {
	struct device *dev;
	struct cros_ec_device *ec;
	struct pwm_chip chip;
	bool use_pwm_type;
};

/**
 * struct cros_ec_pwm - per-PWM driver data
 * @duty_cycle: cached duty cycle
 */
struct cros_ec_pwm {
	u16 duty_cycle;
};

static inline struct cros_ec_pwm_device *pwm_to_cros_ec_pwm(struct pwm_chip *c)
{
	return container_of(c, struct cros_ec_pwm_device, chip);
}

static int cros_ec_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct cros_ec_pwm *channel;

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		return -ENOMEM;

	pwm_set_chip_data(pwm, channel);

	return 0;
}

static void cros_ec_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct cros_ec_pwm *channel = pwm_get_chip_data(pwm);

	kfree(channel);
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

static int cros_ec_pwm_get_duty(struct cros_ec_pwm_device *ec_pwm, u8 index)
{
	struct cros_ec_device *ec = ec_pwm->ec;
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

	ret = cros_ec_cmd_xfer_status(ec, msg);
	if (ret < 0)
		return ret;

	return resp->duty;
}

static int cros_ec_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	struct cros_ec_pwm_device *ec_pwm = pwm_to_cros_ec_pwm(chip);
	struct cros_ec_pwm *channel = pwm_get_chip_data(pwm);
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

	channel->duty_cycle = state->duty_cycle;

	return 0;
}

static int cros_ec_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				 struct pwm_state *state)
{
	struct cros_ec_pwm_device *ec_pwm = pwm_to_cros_ec_pwm(chip);
	struct cros_ec_pwm *channel = pwm_get_chip_data(pwm);
	int ret;

	ret = cros_ec_pwm_get_duty(ec_pwm, pwm->hwpwm);
	if (ret < 0) {
		dev_err(chip->dev, "error getting initial duty: %d\n", ret);
		return ret;
	}

	state->enabled = (ret > 0);
	state->period = EC_PWM_MAX_DUTY;
	state->polarity = PWM_POLARITY_NORMAL;

	/*
	 * Note that "disabled" and "duty cycle == 0" are treated the same. If
	 * the cached duty cycle is not zero, used the cached duty cycle. This
	 * ensures that the configured duty cycle is kept across a disable and
	 * enable operation and avoids potentially confusing consumers.
	 *
	 * For the case of the initial hardware readout, channel->duty_cycle
	 * will be 0 and the actual duty cycle read from the EC is used.
	 */
	if (ret == 0 && channel->duty_cycle > 0)
		state->duty_cycle = channel->duty_cycle;
	else
		state->duty_cycle = ret;

	return 0;
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
	.request = cros_ec_pwm_request,
	.free = cros_ec_pwm_free,
	.get_state	= cros_ec_pwm_get_state,
	.apply		= cros_ec_pwm_apply,
	.owner		= THIS_MODULE,
};

/*
 * Determine the number of supported PWMs. The EC does not return the number
 * of PWMs it supports directly, so we have to read the pwm duty cycle for
 * subsequent channels until we get an error.
 */
static int cros_ec_num_pwms(struct cros_ec_pwm_device *ec_pwm)
{
	int i, ret;

	/* The index field is only 8 bits */
	for (i = 0; i <= U8_MAX; i++) {
		ret = cros_ec_pwm_get_duty(ec_pwm, i);
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

	if (of_device_is_compatible(np, "google,cros-ec-pwm-type"))
		ec_pwm->use_pwm_type = true;

	/* PWM chip */
	chip->dev = dev;
	chip->ops = &cros_ec_pwm_ops;
	chip->of_xlate = cros_ec_pwm_xlate;
	chip->of_pwm_n_cells = 1;

	if (ec_pwm->use_pwm_type) {
		chip->npwm = CROS_EC_PWM_DT_COUNT;
	} else {
		ret = cros_ec_num_pwms(ec_pwm);
		if (ret < 0) {
			dev_err(dev, "Couldn't find PWMs: %d\n", ret);
			return ret;
		}
		chip->npwm = ret;
	}

	dev_dbg(dev, "Probed %u PWMs\n", chip->npwm);

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(dev, "cannot register PWM: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, ec_pwm);

	return ret;
}

static void cros_ec_pwm_remove(struct platform_device *dev)
{
	struct cros_ec_pwm_device *ec_pwm = platform_get_drvdata(dev);
	struct pwm_chip *chip = &ec_pwm->chip;

	pwmchip_remove(chip);
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
	.remove_new = cros_ec_pwm_remove,
	.driver = {
		.name = "cros-ec-pwm",
		.of_match_table = of_match_ptr(cros_ec_pwm_of_match),
	},
};
module_platform_driver(cros_ec_pwm_driver);

MODULE_ALIAS("platform:cros-ec-pwm");
MODULE_DESCRIPTION("ChromeOS EC PWM driver");
MODULE_LICENSE("GPL v2");
