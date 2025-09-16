// SPDX-License-Identifier: GPL-2.0-only
/*
 * PWM driver for Rockchip SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2014 ROCKCHIP, Inc.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/time.h>

#define PWM_CTRL_TIMER_EN	(1 << 0)
#define PWM_CTRL_OUTPUT_EN	(1 << 3)

#define PWM_ENABLE		(1 << 0)
#define PWM_CONTINUOUS		(1 << 1)
#define PWM_DUTY_POSITIVE	(1 << 3)
#define PWM_DUTY_NEGATIVE	(0 << 3)
#define PWM_INACTIVE_NEGATIVE	(0 << 4)
#define PWM_INACTIVE_POSITIVE	(1 << 4)
#define PWM_POLARITY_MASK	(PWM_DUTY_POSITIVE | PWM_INACTIVE_POSITIVE)
#define PWM_OUTPUT_LEFT		(0 << 5)
#define PWM_LOCK_EN		(1 << 6)
#define PWM_LP_DISABLE		(0 << 8)

struct rockchip_pwm_chip {
	struct clk *clk;
	struct clk *pclk;
	const struct rockchip_pwm_data *data;
	void __iomem *base;
};

struct rockchip_pwm_regs {
	unsigned long duty;
	unsigned long period;
	unsigned long cntr;
	unsigned long ctrl;
};

struct rockchip_pwm_data {
	struct rockchip_pwm_regs regs;
	unsigned int prescaler;
	bool supports_polarity;
	bool supports_lock;
	u32 enable_conf;
};

static inline struct rockchip_pwm_chip *to_rockchip_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int rockchip_pwm_get_state(struct pwm_chip *chip,
				  struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u64 prescaled_ns = (u64)pc->data->prescaler * NSEC_PER_SEC;
	u32 enable_conf = pc->data->enable_conf;
	unsigned long clk_rate;
	u64 tmp;
	u32 val;
	int ret;

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	clk_rate = clk_get_rate(pc->clk);

	tmp = readl_relaxed(pc->base + pc->data->regs.period);
	tmp *= prescaled_ns;
	state->period = DIV_U64_ROUND_UP(tmp, clk_rate);

	tmp = readl_relaxed(pc->base + pc->data->regs.duty);
	tmp *= prescaled_ns;
	state->duty_cycle =  DIV_U64_ROUND_UP(tmp, clk_rate);

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);
	state->enabled = (val & enable_conf) == enable_conf;

	if (pc->data->supports_polarity && !(val & PWM_DUTY_POSITIVE))
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	clk_disable(pc->clk);
	clk_disable(pc->pclk);

	return 0;
}

static void rockchip_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u64 prescaled_ns = (u64)pc->data->prescaler * NSEC_PER_SEC;
	u64 clk_rate, tmp;
	u32 period_ticks, duty_ticks;
	u32 ctrl;

	clk_rate = clk_get_rate(pc->clk);

	/*
	 * Since period and duty cycle registers have a width of 32
	 * bits, every possible input period can be obtained using the
	 * default prescaler value for all practical clock rate values.
	 */
	tmp = mul_u64_u64_div_u64(clk_rate, state->period, prescaled_ns);
	if (tmp > U32_MAX)
		tmp = U32_MAX;
	period_ticks = tmp;

	tmp = mul_u64_u64_div_u64(clk_rate, state->duty_cycle, prescaled_ns);
	if (tmp > U32_MAX)
		tmp = U32_MAX;
	duty_ticks = tmp;

	/*
	 * Lock the period and duty of previous configuration, then
	 * change the duty and period, that would not be effective.
	 */
	ctrl = readl_relaxed(pc->base + pc->data->regs.ctrl);
	if (pc->data->supports_lock) {
		ctrl |= PWM_LOCK_EN;
		writel_relaxed(ctrl, pc->base + pc->data->regs.ctrl);
	}

	writel(period_ticks, pc->base + pc->data->regs.period);
	writel(duty_ticks, pc->base + pc->data->regs.duty);

	if (pc->data->supports_polarity) {
		ctrl &= ~PWM_POLARITY_MASK;
		if (state->polarity == PWM_POLARITY_INVERSED)
			ctrl |= PWM_DUTY_NEGATIVE | PWM_INACTIVE_POSITIVE;
		else
			ctrl |= PWM_DUTY_POSITIVE | PWM_INACTIVE_NEGATIVE;
	}

	/*
	 * Unlock and set polarity at the same time,
	 * the configuration of duty, period and polarity
	 * would be effective together at next period.
	 */
	if (pc->data->supports_lock)
		ctrl &= ~PWM_LOCK_EN;

	writel(ctrl, pc->base + pc->data->regs.ctrl);
}

static int rockchip_pwm_enable(struct pwm_chip *chip,
			       struct pwm_device *pwm,
			       bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 enable_conf = pc->data->enable_conf;
	int ret;
	u32 val;

	if (enable) {
		ret = clk_enable(pc->clk);
		if (ret)
			return ret;
	}

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);

	if (enable)
		val |= enable_conf;
	else
		val &= ~enable_conf;

	writel_relaxed(val, pc->base + pc->data->regs.ctrl);

	if (!enable)
		clk_disable(pc->clk);

	return 0;
}

static int rockchip_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	struct pwm_state curstate;
	bool enabled;
	int ret = 0;

	ret = clk_enable(pc->pclk);
	if (ret)
		return ret;

	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	pwm_get_state(pwm, &curstate);
	enabled = curstate.enabled;

	if (state->polarity != curstate.polarity && enabled &&
	    !pc->data->supports_lock) {
		ret = rockchip_pwm_enable(chip, pwm, false);
		if (ret)
			goto out;
		enabled = false;
	}

	rockchip_pwm_config(chip, pwm, state);
	if (state->enabled != enabled) {
		ret = rockchip_pwm_enable(chip, pwm, state->enabled);
		if (ret)
			goto out;
	}

out:
	clk_disable(pc->clk);
	clk_disable(pc->pclk);

	return ret;
}

static const struct pwm_ops rockchip_pwm_ops = {
	.get_state = rockchip_pwm_get_state,
	.apply = rockchip_pwm_apply,
};

static const struct rockchip_pwm_data pwm_data_v1 = {
	.regs = {
		.duty = 0x04,
		.period = 0x08,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 2,
	.supports_polarity = false,
	.supports_lock = false,
	.enable_conf = PWM_CTRL_OUTPUT_EN | PWM_CTRL_TIMER_EN,
};

static const struct rockchip_pwm_data pwm_data_v2 = {
	.regs = {
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = false,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
};

static const struct rockchip_pwm_data pwm_data_vop = {
	.regs = {
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x0c,
		.ctrl = 0x00,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = false,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
};

static const struct rockchip_pwm_data pwm_data_v3 = {
	.regs = {
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 1,
	.supports_polarity = true,
	.supports_lock = true,
	.enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
		       PWM_CONTINUOUS,
};

static const struct of_device_id rockchip_pwm_dt_ids[] = {
	{ .compatible = "rockchip,rk2928-pwm", .data = &pwm_data_v1},
	{ .compatible = "rockchip,rk3288-pwm", .data = &pwm_data_v2},
	{ .compatible = "rockchip,vop-pwm", .data = &pwm_data_vop},
	{ .compatible = "rockchip,rk3328-pwm", .data = &pwm_data_v3},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_pwm_dt_ids);

static int rockchip_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct rockchip_pwm_chip *pc;
	u32 enable_conf, ctrl;
	bool enabled;
	int ret, count;

	chip = devm_pwmchip_alloc(&pdev->dev, 1, sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_rockchip_pwm_chip(chip);

	pc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	pc->clk = devm_clk_get(&pdev->dev, "pwm");
	if (IS_ERR(pc->clk)) {
		pc->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(pc->clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk),
					     "Can't get PWM clk\n");
	}

	count = of_count_phandle_with_args(pdev->dev.of_node,
					   "clocks", "#clock-cells");
	if (count == 2)
		pc->pclk = devm_clk_get(&pdev->dev, "pclk");
	else
		pc->pclk = pc->clk;

	if (IS_ERR(pc->pclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pc->pclk), "Can't get APB clk\n");

	ret = clk_prepare_enable(pc->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Can't prepare enable PWM clk\n");

	ret = clk_prepare_enable(pc->pclk);
	if (ret) {
		dev_err_probe(&pdev->dev, ret, "Can't prepare enable APB clk\n");
		goto err_clk;
	}

	platform_set_drvdata(pdev, chip);

	pc->data = device_get_match_data(&pdev->dev);
	chip->ops = &rockchip_pwm_ops;

	enable_conf = pc->data->enable_conf;
	ctrl = readl_relaxed(pc->base + pc->data->regs.ctrl);
	enabled = (ctrl & enable_conf) == enable_conf;

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err_probe(&pdev->dev, ret, "pwmchip_add() failed\n");
		goto err_pclk;
	}

	/* Keep the PWM clk enabled if the PWM appears to be up and running. */
	if (!enabled)
		clk_disable(pc->clk);

	clk_disable(pc->pclk);

	return 0;

err_pclk:
	clk_disable_unprepare(pc->pclk);
err_clk:
	clk_disable_unprepare(pc->clk);

	return ret;
}

static void rockchip_pwm_remove(struct platform_device *pdev)
{
	struct pwm_chip *chip = platform_get_drvdata(pdev);
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);

	pwmchip_remove(chip);

	clk_unprepare(pc->pclk);
	clk_unprepare(pc->clk);
}

static struct platform_driver rockchip_pwm_driver = {
	.driver = {
		.name = "rockchip-pwm",
		.of_match_table = rockchip_pwm_dt_ids,
	},
	.probe = rockchip_pwm_probe,
	.remove = rockchip_pwm_remove,
};
module_platform_driver(rockchip_pwm_driver);

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("Rockchip SoC PWM driver");
MODULE_LICENSE("GPL v2");
