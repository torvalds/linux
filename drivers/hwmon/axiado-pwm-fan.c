// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2026 Axiado Corporation.
 */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define AX_TACH_PWM_VAL_MAX	255

/* TACH Register offsets */
#define AX_TACH_CTRL_REG	0x00
#define AX_TACH_TIMER_COUNT_REG	0x04
#define AX_TACH_COUNT_REG	0x08
#define AX_TACH_INT_STATUS_REG	0x0c

#define AX_TACH_CTRL_ENABLE	BIT(0)
#define AX_TACH_CTRL_INT_ENABLE	BIT(1)

#define AX_TACH_INT_PENDING	BIT(0)

struct axiado_pwm_fan_tach {
	int irq;
	u32 pulses_per_revolution;
	u32 timer_count;
	u32 count;
};

struct axiado_pwm_fan_ctx {
	/* Serializes PWM updates with PM and shutdown operations. */
	struct mutex pwm_lock;
	/* Protects tachometer count updated from interrupt context. */
	spinlock_t tach_lock;
	struct pwm_device *pwm;
	struct pwm_state pwm_state;
	void __iomem *tach_base;
	struct axiado_pwm_fan_tach tach;
	unsigned int pwm_value;
};

static const struct hwmon_channel_info * const pwm_fan_info[] = {
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	NULL
};

static irqreturn_t axiado_tach_irq_handler(int irq, void *dev)
{
	struct axiado_pwm_fan_ctx *ctx = dev;
	u32 status;

	status = ioread32(ctx->tach_base + AX_TACH_INT_STATUS_REG);

	if (!(status & AX_TACH_INT_PENDING))
		return IRQ_NONE;

	scoped_guard(spinlock_irqsave, &ctx->tach_lock) {
		ctx->tach.count = ioread32(ctx->tach_base + AX_TACH_COUNT_REG);
	}

	iowrite32(AX_TACH_INT_PENDING, ctx->tach_base + AX_TACH_INT_STATUS_REG);

	return IRQ_HANDLED;
}

static int axiado_set_pwm(struct axiado_pwm_fan_ctx *ctx, unsigned int pwm)
{
	struct pwm_state state;
	int ret;

	guard(mutex)(&ctx->pwm_lock);

	if (ctx->pwm_value == pwm)
		return 0;

	state = ctx->pwm_state;
	state.duty_cycle = DIV_ROUND_UP_ULL((u64)pwm * state.period,
					    AX_TACH_PWM_VAL_MAX);
	/*
	 * Keep the PWM enabled even at zero duty cycle so the output stays
	 * driven low instead of being left floating by the PWM hardware.
	 */
	state.enabled = true;

	ret = pwm_apply_might_sleep(ctx->pwm, &state);
	if (ret)
		return ret;

	ctx->pwm_state = state;
	ctx->pwm_value = pwm;

	return 0;
}

static int axiado_pwm_fan_write(struct device *dev,
				enum hwmon_sensor_types type, u32 attr,
				int channel, long val)
{
	struct axiado_pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	if (type != hwmon_pwm || attr != hwmon_pwm_input)
		return -EOPNOTSUPP;

	if (val < 0 || val > AX_TACH_PWM_VAL_MAX)
		return -EINVAL;

	return axiado_set_pwm(ctx, val);
}

static unsigned int axiado_tach_get_rpm(struct axiado_pwm_fan_ctx *ctx)
{
	u32 pulses_per_revolution = ctx->tach.pulses_per_revolution;
	u64 pulses, rpm;

	scoped_guard(spinlock_irqsave, &ctx->tach_lock)
		pulses = ctx->tach.count;

	if (!pulses_per_revolution)
		return 0;

	rpm = pulses * 60;
	do_div(rpm, pulses_per_revolution);

	return rpm;
}

static int axiado_pwm_fan_read(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, long *val)
{
	struct axiado_pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_pwm:
		if (attr != hwmon_pwm_input)
			return -EOPNOTSUPP;

		*val = ctx->pwm_value;

		return 0;

	case hwmon_fan:
		if (attr != hwmon_fan_input)
			return -EOPNOTSUPP;

		*val = axiado_tach_get_rpm(ctx);

		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static umode_t axiado_pwm_fan_is_visible(const void *data,
					 enum hwmon_sensor_types type, u32 attr,
					 int channel)
{
	if (type == hwmon_fan && attr == hwmon_fan_input)
		return 0444;

	if (type == hwmon_pwm && attr == hwmon_pwm_input)
		return 0644;

	return 0;
}

static const struct hwmon_ops pwm_fan_hwmon_ops = {
	.is_visible = axiado_pwm_fan_is_visible,
	.read = axiado_pwm_fan_read,
	.write = axiado_pwm_fan_write,
};

static const struct hwmon_chip_info pwm_fan_chip_info = {
	.ops = &pwm_fan_hwmon_ops,
	.info = pwm_fan_info,
};

static int axiado_pwm_apply_disabled(struct axiado_pwm_fan_ctx *ctx)
{
	struct pwm_state state;

	guard(mutex)(&ctx->pwm_lock);

	if (!ctx->pwm_state.enabled)
		return 0;

	state = ctx->pwm_state;
	state.duty_cycle = 0;
	state.enabled = false;

	return pwm_apply_might_sleep(ctx->pwm, &state);
}

static void axiado_pwm_disable(void *data)
{
	struct axiado_pwm_fan_ctx *ctx = data;

	axiado_pwm_apply_disabled(ctx);
}

static void axiado_tach_enable(struct axiado_pwm_fan_ctx *ctx)
{
	iowrite32(AX_TACH_INT_PENDING,
		  ctx->tach_base + AX_TACH_INT_STATUS_REG);
	iowrite32(ctx->tach.timer_count,
		  ctx->tach_base + AX_TACH_TIMER_COUNT_REG);
	iowrite32(AX_TACH_CTRL_ENABLE | AX_TACH_CTRL_INT_ENABLE,
		  ctx->tach_base + AX_TACH_CTRL_REG);
}

static void axiado_tach_disable(void *data)
{
	struct axiado_pwm_fan_ctx *ctx = data;

	iowrite32(0, ctx->tach_base + AX_TACH_CTRL_REG);
	iowrite32(AX_TACH_INT_PENDING,
		  ctx->tach_base + AX_TACH_INT_STATUS_REG);
}

static int axiado_pwm_fan_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fan_node;
	struct axiado_pwm_fan_ctx *ctx;
	unsigned long tach_clk_rate;
	struct device *hwmon;
	struct clk *tach_clk;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->pwm_lock);
	spin_lock_init(&ctx->tach_lock);

	ctx->tach_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctx->tach_base))
		return PTR_ERR(ctx->tach_base);

	fan_node = device_get_named_child_node(dev, "fan");
	if (!fan_node)
		return dev_err_probe(dev, -EINVAL,
				     "Missing fan child node\n");

	ctx->pwm = devm_fwnode_pwm_get(dev, fan_node, NULL);
	if (IS_ERR(ctx->pwm)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->pwm),
				    "Could not get PWM\n");
		goto put_fan_node;
	}

	ctx->tach.pulses_per_revolution = 2;

	ret = fwnode_property_read_u32(fan_node, "pulses-per-revolution",
				       &ctx->tach.pulses_per_revolution);
	if (ret && ret != -EINVAL) {
		ret = dev_err_probe(dev, ret,
				    "Failed to read pulses-per-revolution\n");
		goto put_fan_node;
	}

	if (!ctx->tach.pulses_per_revolution) {
		ret = dev_err_probe(dev, -EINVAL,
				    "pulses-per-revolution cannot be zero\n");
		goto put_fan_node;
	}

	fwnode_handle_put(fan_node);

	platform_set_drvdata(pdev, ctx);

	pwm_init_state(ctx->pwm, &ctx->pwm_state);

	if (!ctx->pwm_state.period)
		return dev_err_probe(dev, -EINVAL, "PWM period is zero\n");

	tach_clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(tach_clk))
		return dev_err_probe(dev, PTR_ERR(tach_clk),
				     "Failed to get tachometer clock\n");

	tach_clk_rate = clk_get_rate(tach_clk);
	if (!tach_clk_rate || tach_clk_rate > U32_MAX)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid tachometer clock rate: %lu\n",
				     tach_clk_rate);

	ctx->tach.timer_count = tach_clk_rate;

	ctx->tach.irq = platform_get_irq(pdev, 0);
	if (ctx->tach.irq < 0)
		return dev_err_probe(dev, ctx->tach.irq,
				     "Failed to get tachometer IRQ\n");

	ret = devm_request_irq(dev, ctx->tach.irq, axiado_tach_irq_handler, 0,
			       dev_name(dev), ctx);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request tach IRQ\n");

	ret = devm_add_action_or_reset(dev, axiado_tach_disable, ctx);
	if (ret)
		return ret;

	dev_dbg(dev, "Fan tachometer: irq=%d, pulses_per_revolution=%u\n",
		ctx->tach.irq, ctx->tach.pulses_per_revolution);

	axiado_tach_enable(ctx);

	ret = axiado_set_pwm(ctx, AX_TACH_PWM_VAL_MAX);

	if (ret)
		return dev_err_probe(dev, ret, "Failed to configure PWM\n");

	ret = devm_add_action_or_reset(dev, axiado_pwm_disable, ctx);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to add PWM disable action\n");

	hwmon = devm_hwmon_device_register_with_info(dev, "axpwmfan", ctx,
						     &pwm_fan_chip_info, NULL);
	if (IS_ERR(hwmon))
		return dev_err_probe(dev, PTR_ERR(hwmon),
				     "Failed to register hwmon device\n");

	return 0;

put_fan_node:
	fwnode_handle_put(fan_node);

	return ret;
}

static int axiado_pwm_fan_disable(struct device *dev)
{
	struct axiado_pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	ret = axiado_pwm_apply_disabled(ctx);
	if (ret)
		return ret;

	axiado_tach_disable(ctx);
	synchronize_irq(ctx->tach.irq);

	return 0;
}

static void axiado_pwm_fan_shutdown(struct platform_device *pdev)
{
	struct axiado_pwm_fan_ctx *ctx = platform_get_drvdata(pdev);

	/* Best effort during shutdown. */
	axiado_pwm_apply_disabled(ctx);

	axiado_tach_disable(ctx);
	synchronize_irq(ctx->tach.irq);
}

static int axiado_pwm_fan_suspend(struct device *dev)
{
	return axiado_pwm_fan_disable(dev);
}

static int axiado_pwm_fan_resume(struct device *dev)
{
	struct axiado_pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	axiado_tach_enable(ctx);

	scoped_guard(mutex, &ctx->pwm_lock)
		ret = pwm_apply_might_sleep(ctx->pwm, &ctx->pwm_state);

	if (ret)
		axiado_tach_disable(ctx);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(axiado_pwm_fan_pm,
				axiado_pwm_fan_suspend,
				axiado_pwm_fan_resume);

static const struct of_device_id axiado_pwm_fan_match[] = {
	{ .compatible = "axiado,ax3000-pwm-fan" },
	{ }
};
MODULE_DEVICE_TABLE(of, axiado_pwm_fan_match);

static struct platform_driver axiado_pwm_fan_driver = {
	.probe		= axiado_pwm_fan_probe,
	.shutdown	= axiado_pwm_fan_shutdown,
	.driver	= {
		.name		= "axiado-pwm-fan",
		.pm		= pm_sleep_ptr(&axiado_pwm_fan_pm),
		.of_match_table	= axiado_pwm_fan_match,
	},
};
module_platform_driver(axiado_pwm_fan_driver);

MODULE_AUTHOR("Axiado Corporation");
MODULE_DESCRIPTION("Axiado PWM fan controller driver");
MODULE_LICENSE("GPL");
