// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pwm-fan.c - Hwmon driver for fans connected to PWM lines.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Author: Kamil Debski <k.debski@samsung.com>
 */

#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/timer.h>

#define MAX_PWM 255

struct pwm_fan_tach {
	int irq;
	atomic_t pulses;
	unsigned int rpm;
	u8 pulses_per_revolution;
};

struct pwm_fan_ctx {
	struct mutex lock;
	struct pwm_device *pwm;
	struct pwm_state pwm_state;
	struct regulator *reg_en;

	int tach_count;
	struct pwm_fan_tach *tachs;
	ktime_t sample_start;
	struct timer_list rpm_timer;

	unsigned int pwm_value;
	unsigned int pwm_fan_state;
	unsigned int pwm_fan_max_state;
	unsigned int *pwm_fan_cooling_levels;
	struct thermal_cooling_device *cdev;

	struct hwmon_chip_info info;
	struct hwmon_channel_info fan_channel;
};

static const u32 pwm_fan_channel_config_pwm[] = {
	HWMON_PWM_INPUT,
	0
};

static const struct hwmon_channel_info pwm_fan_channel_pwm = {
	.type = hwmon_pwm,
	.config = pwm_fan_channel_config_pwm,
};

/* This handler assumes self resetting edge triggered interrupt. */
static irqreturn_t pulse_handler(int irq, void *dev_id)
{
	struct pwm_fan_tach *tach = dev_id;

	atomic_inc(&tach->pulses);

	return IRQ_HANDLED;
}

static void sample_timer(struct timer_list *t)
{
	struct pwm_fan_ctx *ctx = from_timer(ctx, t, rpm_timer);
	unsigned int delta = ktime_ms_delta(ktime_get(), ctx->sample_start);
	int i;

	if (delta) {
		for (i = 0; i < ctx->tach_count; i++) {
			struct pwm_fan_tach *tach = &ctx->tachs[i];
			int pulses;

			pulses = atomic_read(&tach->pulses);
			atomic_sub(pulses, &tach->pulses);
			tach->rpm = (unsigned int)(pulses * 1000 * 60) /
				(tach->pulses_per_revolution * delta);
		}

		ctx->sample_start = ktime_get();
	}

	mod_timer(&ctx->rpm_timer, jiffies + HZ);
}

static int  __set_pwm(struct pwm_fan_ctx *ctx, unsigned long pwm)
{
	unsigned long period;
	int ret = 0;
	struct pwm_state *state = &ctx->pwm_state;

	mutex_lock(&ctx->lock);
	if (ctx->pwm_value == pwm)
		goto exit_set_pwm_err;

	period = state->period;
	state->duty_cycle = DIV_ROUND_UP(pwm * (period - 1), MAX_PWM);
	state->enabled = pwm ? true : false;

	ret = pwm_apply_state(ctx->pwm, state);
	if (!ret)
		ctx->pwm_value = pwm;
exit_set_pwm_err:
	mutex_unlock(&ctx->lock);
	return ret;
}

static void pwm_fan_update_state(struct pwm_fan_ctx *ctx, unsigned long pwm)
{
	int i;

	for (i = 0; i < ctx->pwm_fan_max_state; ++i)
		if (pwm < ctx->pwm_fan_cooling_levels[i + 1])
			break;

	ctx->pwm_fan_state = i;
}

static int pwm_fan_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	if (val < 0 || val > MAX_PWM)
		return -EINVAL;

	ret = __set_pwm(ctx, val);
	if (ret)
		return ret;

	pwm_fan_update_state(ctx, val);
	return 0;
}

static int pwm_fan_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_pwm:
		*val = ctx->pwm_value;
		return 0;

	case hwmon_fan:
		*val = ctx->tachs[channel].rpm;
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static umode_t pwm_fan_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_pwm:
		return 0644;

	case hwmon_fan:
		return 0444;

	default:
		return 0;
	}
}

static const struct hwmon_ops pwm_fan_hwmon_ops = {
	.is_visible = pwm_fan_is_visible,
	.read = pwm_fan_read,
	.write = pwm_fan_write,
};

/* thermal cooling device callbacks */
static int pwm_fan_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct pwm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	*state = ctx->pwm_fan_max_state;

	return 0;
}

static int pwm_fan_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct pwm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	*state = ctx->pwm_fan_state;

	return 0;
}

static int
pwm_fan_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct pwm_fan_ctx *ctx = cdev->devdata;
	int ret;

	if (!ctx || (state > ctx->pwm_fan_max_state))
		return -EINVAL;

	if (state == ctx->pwm_fan_state)
		return 0;

	ret = __set_pwm(ctx, ctx->pwm_fan_cooling_levels[state]);
	if (ret) {
		dev_err(&cdev->device, "Cannot set pwm!\n");
		return ret;
	}

	ctx->pwm_fan_state = state;

	return ret;
}

static const struct thermal_cooling_device_ops pwm_fan_cooling_ops = {
	.get_max_state = pwm_fan_get_max_state,
	.get_cur_state = pwm_fan_get_cur_state,
	.set_cur_state = pwm_fan_set_cur_state,
};

static int pwm_fan_of_get_cooling_data(struct device *dev,
				       struct pwm_fan_ctx *ctx)
{
	struct device_node *np = dev->of_node;
	int num, i, ret;

	if (!of_find_property(np, "cooling-levels", NULL))
		return 0;

	ret = of_property_count_u32_elems(np, "cooling-levels");
	if (ret <= 0) {
		dev_err(dev, "Wrong data!\n");
		return ret ? : -EINVAL;
	}

	num = ret;
	ctx->pwm_fan_cooling_levels = devm_kcalloc(dev, num, sizeof(u32),
						   GFP_KERNEL);
	if (!ctx->pwm_fan_cooling_levels)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "cooling-levels",
					 ctx->pwm_fan_cooling_levels, num);
	if (ret) {
		dev_err(dev, "Property 'cooling-levels' cannot be read!\n");
		return ret;
	}

	for (i = 0; i < num; i++) {
		if (ctx->pwm_fan_cooling_levels[i] > MAX_PWM) {
			dev_err(dev, "PWM fan state[%d]:%d > %d\n", i,
				ctx->pwm_fan_cooling_levels[i], MAX_PWM);
			return -EINVAL;
		}
	}

	ctx->pwm_fan_max_state = num - 1;

	return 0;
}

static void pwm_fan_regulator_disable(void *data)
{
	regulator_disable(data);
}

static void pwm_fan_pwm_disable(void *__ctx)
{
	struct pwm_fan_ctx *ctx = __ctx;

	ctx->pwm_state.enabled = false;
	pwm_apply_state(ctx->pwm, &ctx->pwm_state);
	del_timer_sync(&ctx->rpm_timer);
}

static int pwm_fan_probe(struct platform_device *pdev)
{
	struct thermal_cooling_device *cdev;
	struct device *dev = &pdev->dev;
	struct pwm_fan_ctx *ctx;
	struct device *hwmon;
	int ret;
	const struct hwmon_channel_info **channels;
	u32 *fan_channel_config;
	int channel_count = 1;	/* We always have a PWM channel. */
	int i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	ctx->pwm = devm_of_pwm_get(dev, dev->of_node, NULL);
	if (IS_ERR(ctx->pwm))
		return dev_err_probe(dev, PTR_ERR(ctx->pwm), "Could not get PWM\n");

	platform_set_drvdata(pdev, ctx);

	ctx->reg_en = devm_regulator_get_optional(dev, "fan");
	if (IS_ERR(ctx->reg_en)) {
		if (PTR_ERR(ctx->reg_en) != -ENODEV)
			return PTR_ERR(ctx->reg_en);

		ctx->reg_en = NULL;
	} else {
		ret = regulator_enable(ctx->reg_en);
		if (ret) {
			dev_err(dev, "Failed to enable fan supply: %d\n", ret);
			return ret;
		}
		ret = devm_add_action_or_reset(dev, pwm_fan_regulator_disable,
					       ctx->reg_en);
		if (ret)
			return ret;
	}

	ctx->pwm_value = MAX_PWM;

	pwm_init_state(ctx->pwm, &ctx->pwm_state);

	/*
	 * __set_pwm assumes that MAX_PWM * (period - 1) fits into an unsigned
	 * long. Check this here to prevent the fan running at a too low
	 * frequency.
	 */
	if (ctx->pwm_state.period > ULONG_MAX / MAX_PWM + 1) {
		dev_err(dev, "Configured period too big\n");
		return -EINVAL;
	}

	/* Set duty cycle to maximum allowed and enable PWM output */
	ret = __set_pwm(ctx, MAX_PWM);
	if (ret) {
		dev_err(dev, "Failed to configure PWM: %d\n", ret);
		return ret;
	}
	timer_setup(&ctx->rpm_timer, sample_timer, 0);
	ret = devm_add_action_or_reset(dev, pwm_fan_pwm_disable, ctx);
	if (ret)
		return ret;

	ctx->tach_count = platform_irq_count(pdev);
	if (ctx->tach_count < 0)
		return dev_err_probe(dev, ctx->tach_count,
				     "Could not get number of fan tachometer inputs\n");
	dev_dbg(dev, "%d fan tachometer inputs\n", ctx->tach_count);

	if (ctx->tach_count) {
		channel_count++;	/* We also have a FAN channel. */

		ctx->tachs = devm_kcalloc(dev, ctx->tach_count,
					  sizeof(struct pwm_fan_tach),
					  GFP_KERNEL);
		if (!ctx->tachs)
			return -ENOMEM;

		ctx->fan_channel.type = hwmon_fan;
		fan_channel_config = devm_kcalloc(dev, ctx->tach_count + 1,
						  sizeof(u32), GFP_KERNEL);
		if (!fan_channel_config)
			return -ENOMEM;
		ctx->fan_channel.config = fan_channel_config;
	}

	channels = devm_kcalloc(dev, channel_count + 1,
				sizeof(struct hwmon_channel_info *), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	channels[0] = &pwm_fan_channel_pwm;

	for (i = 0; i < ctx->tach_count; i++) {
		struct pwm_fan_tach *tach = &ctx->tachs[i];
		u32 ppr = 2;

		tach->irq = platform_get_irq(pdev, i);
		if (tach->irq == -EPROBE_DEFER)
			return tach->irq;
		if (tach->irq > 0) {
			ret = devm_request_irq(dev, tach->irq, pulse_handler, 0,
					       pdev->name, tach);
			if (ret) {
				dev_err(dev,
					"Failed to request interrupt: %d\n",
					ret);
				return ret;
			}
		}

		of_property_read_u32_index(dev->of_node,
					   "pulses-per-revolution",
					   i,
					   &ppr);
		tach->pulses_per_revolution = ppr;
		if (!tach->pulses_per_revolution) {
			dev_err(dev, "pulses-per-revolution can't be zero.\n");
			return -EINVAL;
		}

		fan_channel_config[i] = HWMON_F_INPUT;

		dev_dbg(dev, "tach%d: irq=%d, pulses_per_revolution=%d\n",
			i, tach->irq, tach->pulses_per_revolution);
	}

	if (ctx->tach_count > 0) {
		ctx->sample_start = ktime_get();
		mod_timer(&ctx->rpm_timer, jiffies + HZ);

		channels[1] = &ctx->fan_channel;
	}

	ctx->info.ops = &pwm_fan_hwmon_ops;
	ctx->info.info = channels;

	hwmon = devm_hwmon_device_register_with_info(dev, "pwmfan",
						     ctx, &ctx->info, NULL);
	if (IS_ERR(hwmon)) {
		dev_err(dev, "Failed to register hwmon device\n");
		return PTR_ERR(hwmon);
	}

	ret = pwm_fan_of_get_cooling_data(dev, ctx);
	if (ret)
		return ret;

	ctx->pwm_fan_state = ctx->pwm_fan_max_state;
	if (IS_ENABLED(CONFIG_THERMAL)) {
		cdev = devm_thermal_of_cooling_device_register(dev,
			dev->of_node, "pwm-fan", ctx, &pwm_fan_cooling_ops);
		if (IS_ERR(cdev)) {
			ret = PTR_ERR(cdev);
			dev_err(dev,
				"Failed to register pwm-fan as cooling device: %d\n",
				ret);
			return ret;
		}
		ctx->cdev = cdev;
	}

	return 0;
}

static int pwm_fan_disable(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	if (ctx->pwm_value) {
		/* keep ctx->pwm_state unmodified for pwm_fan_resume() */
		struct pwm_state state = ctx->pwm_state;

		state.duty_cycle = 0;
		state.enabled = false;
		ret = pwm_apply_state(ctx->pwm, &state);
		if (ret < 0)
			return ret;
	}

	if (ctx->reg_en) {
		ret = regulator_disable(ctx->reg_en);
		if (ret) {
			dev_err(dev, "Failed to disable fan supply: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static void pwm_fan_shutdown(struct platform_device *pdev)
{
	pwm_fan_disable(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int pwm_fan_suspend(struct device *dev)
{
	return pwm_fan_disable(dev);
}

static int pwm_fan_resume(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	if (ctx->reg_en) {
		ret = regulator_enable(ctx->reg_en);
		if (ret) {
			dev_err(dev, "Failed to enable fan supply: %d\n", ret);
			return ret;
		}
	}

	if (ctx->pwm_value == 0)
		return 0;

	return pwm_apply_state(ctx->pwm, &ctx->pwm_state);
}
#endif

static SIMPLE_DEV_PM_OPS(pwm_fan_pm, pwm_fan_suspend, pwm_fan_resume);

static const struct of_device_id of_pwm_fan_match[] = {
	{ .compatible = "pwm-fan", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_fan_match);

static struct platform_driver pwm_fan_driver = {
	.probe		= pwm_fan_probe,
	.shutdown	= pwm_fan_shutdown,
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
