/*
 * PWM Greybus driver.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pwm.h>

#include "greybus.h"
#include "gpbridge.h"

struct gb_pwm_chip {
	struct gb_connection	*connection;
	u8			pwm_max;	/* max pwm number */

	struct pwm_chip		chip;
	struct pwm_chip		*pwm;
};
#define pwm_chip_to_gb_pwm_chip(chip) \
	container_of(chip, struct gb_pwm_chip, chip)


static int gb_pwm_count_operation(struct gb_pwm_chip *pwmc)
{
	struct gb_pwm_count_response response;
	int ret;

	ret = gb_operation_sync(pwmc->connection, GB_PWM_TYPE_PWM_COUNT,
				NULL, 0, &response, sizeof(response));
	if (ret)
		return ret;
	pwmc->pwm_max = response.count;
	return 0;
}

static int gb_pwm_activate_operation(struct gb_pwm_chip *pwmc,
				     u8 which)
{
	struct gb_pwm_activate_request request;

	if (which > pwmc->pwm_max)
		return -EINVAL;

	request.which = which;
	return gb_operation_sync(pwmc->connection, GB_PWM_TYPE_ACTIVATE,
				 &request, sizeof(request), NULL, 0);
}

static int gb_pwm_deactivate_operation(struct gb_pwm_chip *pwmc,
				       u8 which)
{
	struct gb_pwm_deactivate_request request;

	if (which > pwmc->pwm_max)
		return -EINVAL;

	request.which = which;
	return gb_operation_sync(pwmc->connection, GB_PWM_TYPE_DEACTIVATE,
				 &request, sizeof(request), NULL, 0);
}

static int gb_pwm_config_operation(struct gb_pwm_chip *pwmc,
				   u8 which, u32 duty, u32 period)
{
	struct gb_pwm_config_request request;

	if (which > pwmc->pwm_max)
		return -EINVAL;

	request.which = which;
	request.duty = cpu_to_le32(duty);
	request.period = cpu_to_le32(period);
	return gb_operation_sync(pwmc->connection, GB_PWM_TYPE_CONFIG,
				 &request, sizeof(request), NULL, 0);
}


static int gb_pwm_set_polarity_operation(struct gb_pwm_chip *pwmc,
					 u8 which, u8 polarity)
{
	struct gb_pwm_polarity_request request;

	if (which > pwmc->pwm_max)
		return -EINVAL;

	request.which = which;
	request.polarity = polarity;
	return gb_operation_sync(pwmc->connection, GB_PWM_TYPE_POLARITY,
				 &request, sizeof(request), NULL, 0);
}

static int gb_pwm_enable_operation(struct gb_pwm_chip *pwmc,
				   u8 which)
{
	struct gb_pwm_enable_request request;

	if (which > pwmc->pwm_max)
		return -EINVAL;

	request.which = which;
	return gb_operation_sync(pwmc->connection, GB_PWM_TYPE_ENABLE,
				 &request, sizeof(request), NULL, 0);
}

static int gb_pwm_disable_operation(struct gb_pwm_chip *pwmc,
				    u8 which)
{
	struct gb_pwm_disable_request request;

	if (which > pwmc->pwm_max)
		return -EINVAL;

	request.which = which;
	return gb_operation_sync(pwmc->connection, GB_PWM_TYPE_DISABLE,
				 &request, sizeof(request), NULL, 0);
}

static int gb_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gb_pwm_chip *pwmc = pwm_chip_to_gb_pwm_chip(chip);

	return gb_pwm_activate_operation(pwmc, pwm->hwpwm);
};

static void gb_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gb_pwm_chip *pwmc = pwm_chip_to_gb_pwm_chip(chip);

	if (test_bit(PWMF_ENABLED, &pwm->flags))
		dev_warn(chip->dev, "freeing PWM device without disabling\n");

	gb_pwm_deactivate_operation(pwmc, pwm->hwpwm);
}

static int gb_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			 int duty_ns, int period_ns)
{
	struct gb_pwm_chip *pwmc = pwm_chip_to_gb_pwm_chip(chip);

	return gb_pwm_config_operation(pwmc, pwm->hwpwm, duty_ns, period_ns);
};

static int gb_pwm_set_polarity(struct pwm_chip *chip, struct pwm_device *pwm,
			       enum pwm_polarity polarity)
{
	struct gb_pwm_chip *pwmc = pwm_chip_to_gb_pwm_chip(chip);

	return gb_pwm_set_polarity_operation(pwmc, pwm->hwpwm, polarity);
};

static int gb_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gb_pwm_chip *pwmc = pwm_chip_to_gb_pwm_chip(chip);

	return gb_pwm_enable_operation(pwmc, pwm->hwpwm);
};

static void gb_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct gb_pwm_chip *pwmc = pwm_chip_to_gb_pwm_chip(chip);

	gb_pwm_disable_operation(pwmc, pwm->hwpwm);
};

static const struct pwm_ops gb_pwm_ops = {
	.request = gb_pwm_request,
	.free = gb_pwm_free,
	.config = gb_pwm_config,
	.set_polarity = gb_pwm_set_polarity,
	.enable = gb_pwm_enable,
	.disable = gb_pwm_disable,
	.owner = THIS_MODULE,
};

static int gb_pwm_probe(struct gpbridge_device *gpbdev,
			const struct gpbridge_device_id *id)
{
	struct gb_connection *connection;
	struct gb_pwm_chip *pwmc;
	struct pwm_chip *pwm;
	int ret;

	pwmc = kzalloc(sizeof(*pwmc), GFP_KERNEL);
	if (!pwmc)
		return -ENOMEM;

	connection = gb_connection_create(gpbdev->bundle,
					  le16_to_cpu(gpbdev->cport_desc->id),
					  NULL);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto exit_pwmc_free;
	}

	pwmc->connection = connection;
	gb_connection_set_data(connection, pwmc);
	gb_gpbridge_set_data(gpbdev, pwmc);

	ret = gb_connection_enable(connection);
	if (ret)
		goto exit_connection_destroy;

	ret = gb_gpbridge_get_version(connection);
	if (ret)
		goto exit_connection_disable;

	/* Query number of pwms present */
	ret = gb_pwm_count_operation(pwmc);
	if (ret)
		goto exit_connection_disable;

	pwm = &pwmc->chip;

	pwm->dev = &gpbdev->dev;
	pwm->ops = &gb_pwm_ops;
	pwm->base = -1;			/* Allocate base dynamically */
	pwm->npwm = pwmc->pwm_max + 1;
	pwm->can_sleep = true;		/* FIXME */

	ret = pwmchip_add(pwm);
	if (ret) {
		dev_err(&gpbdev->dev,
			"failed to register PWM: %d\n", ret);
		goto exit_connection_disable;
	}

	return 0;

exit_connection_disable:
	gb_connection_disable(connection);
exit_connection_destroy:
	gb_connection_destroy(connection);
exit_pwmc_free:
	kfree(pwmc);
	return ret;
}

static void gb_pwm_remove(struct gpbridge_device *gpbdev)
{
	struct gb_pwm_chip *pwmc = gb_gpbridge_get_data(gpbdev);
	struct gb_connection *connection = pwmc->connection;

	pwmchip_remove(&pwmc->chip);
	gb_connection_disable(connection);
	gb_connection_destroy(connection);
	kfree(pwmc);
}

static const struct gpbridge_device_id gb_pwm_id_table[] = {
	{ GPBRIDGE_PROTOCOL(GREYBUS_PROTOCOL_PWM) },
	{ },
};
MODULE_DEVICE_TABLE(gpbridge, gb_pwm_id_table);

static struct gpbridge_driver pwm_driver = {
	.name		= "pwm",
	.probe		= gb_pwm_probe,
	.remove		= gb_pwm_remove,
	.id_table	= gb_pwm_id_table,
};

module_gpbridge_driver(pwm_driver);
MODULE_LICENSE("GPL v2");
