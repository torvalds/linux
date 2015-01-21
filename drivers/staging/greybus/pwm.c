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

struct gb_pwm_chip {
	struct gb_connection	*connection;
	u8			version_major;
	u8			version_minor;
	u8			pwm_max;	/* max pwm number */

	struct pwm_chip		chip;
	struct pwm_chip		*pwm;
};
#define pwm_chip_to_gb_pwm_chip(chip) \
	container_of(chip, struct gb_pwm_chip, chip)

/* Version of the Greybus PWM protocol we support */
#define	GB_PWM_VERSION_MAJOR		0x00
#define	GB_PWM_VERSION_MINOR		0x01

/* Greybus PWM request types */
#define	GB_PWM_TYPE_INVALID		0x00
#define	GB_PWM_TYPE_PROTOCOL_VERSION	0x01
#define	GB_PWM_TYPE_PWM_COUNT		0x02
#define	GB_PWM_TYPE_ACTIVATE		0x03
#define	GB_PWM_TYPE_DEACTIVATE		0x04
#define	GB_PWM_TYPE_CONFIG		0x05
#define	GB_PWM_TYPE_POLARITY		0x06
#define	GB_PWM_TYPE_ENABLE		0x07
#define	GB_PWM_TYPE_DISABLE		0x08
#define	GB_PWM_TYPE_RESPONSE		0x80	/* OR'd with rest */

/* pwm count request has no payload */
struct gb_pwm_count_response {
	__u8	count;
};

struct gb_pwm_activate_request {
	__u8	which;
};

struct gb_pwm_deactivate_request {
	__u8	which;
};

struct gb_pwm_config_request {
	__u8	which;
	__le32	duty;
	__le32	period;
};

struct gb_pwm_polarity_request {
	__u8	which;
	__u8	polarity;
};

struct gb_pwm_enable_request {
	__u8	which;
};

struct gb_pwm_disable_request {
	__u8	which;
};

/* Define get_version() routine */
define_get_version(gb_pwm_chip, PWM);

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

static int gb_pwm_connection_init(struct gb_connection *connection)
{
	struct gb_pwm_chip *pwmc;
	struct pwm_chip *pwm;
	int ret;

	pwmc = kzalloc(sizeof(*pwmc), GFP_KERNEL);
	if (!pwmc)
		return -ENOMEM;
	pwmc->connection = connection;
	connection->private = pwmc;

	/* Check for compatible protocol version */
	ret = get_version(pwmc);
	if (ret)
		goto out_err;

	/* Query number of pwms present */
	ret = gb_pwm_count_operation(pwmc);
	if (ret)
		goto out_err;

	pwm = &pwmc->chip;

	pwm->dev = &connection->dev;
	pwm->ops = &gb_pwm_ops;
	pwm->base = -1;			/* Allocate base dynamically */
	pwm->npwm = pwmc->pwm_max + 1;
	pwm->can_sleep = true;		/* FIXME */

	ret = pwmchip_add(pwm);
	if (ret) {
		pr_err("Failed to register PWM\n");
		return ret;
	}

	return 0;
out_err:
	kfree(pwmc);
	return ret;
}

static void gb_pwm_connection_exit(struct gb_connection *connection)
{
	struct gb_pwm_chip *pwmc = connection->private;

	if (!pwmc)
		return;

	pwmchip_remove(&pwmc->chip);
	/* kref_put(pwmc->connection) */
	kfree(pwmc);
}

static struct gb_protocol pwm_protocol = {
	.name			= "pwm",
	.id			= GREYBUS_PROTOCOL_PWM,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_pwm_connection_init,
	.connection_exit	= gb_pwm_connection_exit,
	.request_recv		= NULL, /* no incoming requests */
};

int gb_pwm_protocol_init(void)
{
	return gb_protocol_register(&pwm_protocol);
}

void gb_pwm_protocol_exit(void)
{
	gb_protocol_deregister(&pwm_protocol);
}
