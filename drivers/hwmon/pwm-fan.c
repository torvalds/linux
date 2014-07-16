/*
 * pwm-fan.c - Hwmon driver for fans connected to PWM lines.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Author: Kamil Debski <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/sysfs.h>

#define MAX_PWM 255

struct pwm_fan_ctx {
	struct mutex lock;
	struct pwm_device *pwm;
	unsigned char pwm_value;
};

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	unsigned long pwm, duty;
	ssize_t ret;

	if (kstrtoul(buf, 10, &pwm) || pwm > MAX_PWM)
		return -EINVAL;

	mutex_lock(&ctx->lock);

	if (ctx->pwm_value == pwm)
		goto exit_set_pwm_no_change;

	if (pwm == 0) {
		pwm_disable(ctx->pwm);
		goto exit_set_pwm;
	}

	duty = DIV_ROUND_UP(pwm * (ctx->pwm->period - 1), MAX_PWM);
	ret = pwm_config(ctx->pwm, duty, ctx->pwm->period);
	if (ret)
		goto exit_set_pwm_err;

	if (ctx->pwm_value == 0) {
		ret = pwm_enable(ctx->pwm);
		if (ret)
			goto exit_set_pwm_err;
	}

exit_set_pwm:
	ctx->pwm_value = pwm;
exit_set_pwm_no_change:
	ret = count;
exit_set_pwm_err:
	mutex_unlock(&ctx->lock);
	return ret;
}

static ssize_t show_pwm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ctx->pwm_value);
}


static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm, set_pwm, 0);

static struct attribute *pwm_fan_attrs[] = {
	&sensor_dev_attr_pwm1.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(pwm_fan);

static int pwm_fan_probe(struct platform_device *pdev)
{
	struct device *hwmon;
	struct pwm_fan_ctx *ctx;
	int duty_cycle;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	ctx->pwm = devm_of_pwm_get(&pdev->dev, pdev->dev.of_node, NULL);
	if (IS_ERR(ctx->pwm)) {
		dev_err(&pdev->dev, "Could not get PWM\n");
		return PTR_ERR(ctx->pwm);
	}

	dev_set_drvdata(&pdev->dev, ctx);
	platform_set_drvdata(pdev, ctx);

	/* Set duty cycle to maximum allowed */
	duty_cycle = ctx->pwm->period - 1;
	ctx->pwm_value = MAX_PWM;

	ret = pwm_config(ctx->pwm, duty_cycle, ctx->pwm->period);
	if (ret) {
		dev_err(&pdev->dev, "Failed to configure PWM\n");
		return ret;
	}

	/* Enbale PWM output */
	ret = pwm_enable(ctx->pwm);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PWM\n");
		return ret;
	}

	hwmon = devm_hwmon_device_register_with_groups(&pdev->dev, "pwmfan",
						       ctx, pwm_fan_groups);
	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device\n");
		pwm_disable(ctx->pwm);
		return PTR_ERR(hwmon);
	}
	return 0;
}

static int pwm_fan_remove(struct platform_device *pdev)
{
	struct pwm_fan_ctx *ctx = platform_get_drvdata(pdev);

	if (ctx->pwm_value)
		pwm_disable(ctx->pwm);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pwm_fan_suspend(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	if (ctx->pwm_value)
		pwm_disable(ctx->pwm);
	return 0;
}

static int pwm_fan_resume(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	if (ctx->pwm_value)
		return pwm_enable(ctx->pwm);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pwm_fan_pm, pwm_fan_suspend, pwm_fan_resume);

static struct of_device_id of_pwm_fan_match[] = {
	{ .compatible = "pwm-fan", },
	{},
};

static struct platform_driver pwm_fan_driver = {
	.probe		= pwm_fan_probe,
	.remove		= pwm_fan_remove,
	.driver	= {
		.name		= "pwm-fan",
		.pm		= &pwm_fan_pm,
		.of_match_table	= of_pwm_fan_match,
	},
};

module_platform_driver(pwm_fan_driver);

MODULE_AUTHOR("Kamil Debski <k.debski@samsung.com>");
MODULE_ALIAS("platform:pwm-fan");
MODULE_DESCRIPTION("PWM FAN driver");
MODULE_LICENSE("GPL");
