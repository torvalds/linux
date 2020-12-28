// SPDX-License-Identifier: GPL-2.0-only
/*
 * PWM driver for Rockchip SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2014 ROCKCHIP, Inc.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
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
	struct pwm_chip chip;
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

static inline struct rockchip_pwm_chip *to_rockchip_pwm_chip(struct pwm_chip *c)
{
	return container_of(c, struct rockchip_pwm_chip, chip);
}

static void rockchip_pwm_get_state(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 enable_conf = pc->data->enable_conf;
	unsigned long clk_rate;
	u64 tmp;
	u32 val;
	int ret;

	ret = clk_enable(pc->pclk);
	if (ret)
		return;

	clk_rate = clk_get_rate(pc->clk);

	tmp = readl_relaxed(pc->base + pc->data->regs.period);
	tmp *= pc->data->prescaler * NSEC_PER_SEC;
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, clk_rate);

	tmp = readl_relaxed(pc->base + pc->data->regs.duty);
	tmp *= pc->data->prescaler * NSEC_PER_SEC;
	state->duty_cycle =  DIV_ROUND_CLOSEST_ULL(tmp, clk_rate);

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);
	state->enabled = (val & enable_conf) == enable_conf;

	if (pc->data->supports_polarity && !(val & PWM_DUTY_POSITIVE))
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	clk_disable(pc->pclk);
}

static void rockchip_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       const struct pwm_state *state)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	unsigned long period, duty;
	u64 clk_rate, div;
	u32 ctrl;

	clk_rate = clk_get_rate(pc->clk);

	/*
	 * Since period and duty cycle registers have a width of 32
	 * bits, every possible input period can be obtained using the
	 * default prescaler value for all practical clock rate values.
	 */
	div = clk_rate * state->period;
	period = DIV_ROUND_CLOSEST_ULL(div,
				       pc->data->prescaler * NSEC_PER_SEC);

	div = clk_rate * state->duty_cycle;
	duty = DIV_ROUND_CLOSEST_ULL(div, pc->data->prescaler * NSEC_PER_SEC);

	/*
	 * Lock the period and duty of previous configuration, then
	 * change the duty and period, that would not be effective.
	 */
	ctrl = readl_relaxed(pc->base + pc->data->regs.ctrl);
	if (pc->data->supports_lock) {
		ctrl |= PWM_LOCK_EN;
		writel_relaxed(ctrl, pc->base + pc->data->regs.ctrl);
	}

	writel(period, pc->base + pc->data->regs.period);
	writel(duty, pc->base + pc->data->regs.duty);

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
	clk_disable(pc->pclk);

	return ret;
}

static const struct pwm_ops rockchip_pwm_ops = {
	.get_state = rockchip_pwm_get_state,
	.apply = rockchip_pwm_apply,
	.owner = THIS_MODULE,
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
	const struct of_device_id *id;
	struct rockchip_pwm_chip *pc;
	u32 enable_conf, ctrl;
	int ret, count;

	id = of_match_device(rockchip_pwm_dt_ids, &pdev->dev);
	if (!id)
		return -EINVAL;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	pc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	pc->clk = devm_clk_get(&pdev->dev, "pwm");
	if (IS_ERR(pc->clk)) {
		pc->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(pc->clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(pc->clk),
					     "Can't get bus clk\n");
	}

	count = of_count_phandle_with_args(pdev->dev.of_node,
					   "clocks", "#clock-cells");
	if (count == 2)
		pc->pclk = devm_clk_get(&pdev->dev, "pclk");
	else
		pc->pclk = pc->clk;

	if (IS_ERR(pc->pclk)) {
		ret = PTR_ERR(pc->pclk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get APB clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(pc->clk);
	if (ret) {
		dev_err(&pdev->dev, "Can't prepare enable bus clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare(pc->pclk);
	if (ret) {
		dev_err(&pdev->dev, "Can't prepare APB clk: %d\n", ret);
		goto err_clk;
	}

	platform_set_drvdata(pdev, pc);

	pc->data = id->data;
	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &rockchip_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = 1;

	if (pc->data->supports_polarity) {
		pc->chip.of_xlate = of_pwm_xlate_with_flags;
		pc->chip.of_pwm_n_cells = 3;
	}

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		clk_unprepare(pc->clk);
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		goto err_pclk;
	}

	/* Keep the PWM clk enabled if the PWM appears to be up and running. */
	enable_conf = pc->data->enable_conf;
	ctrl = readl_relaxed(pc->base + pc->data->regs.ctrl);
	if ((ctrl & enable_conf) != enable_conf)
		clk_disable(pc->clk);

	return 0;

err_pclk:
	clk_unprepare(pc->pclk);
err_clk:
	clk_disable_unprepare(pc->clk);

	return ret;
}

static int rockchip_pwm_remove(struct platform_device *pdev)
{
	struct rockchip_pwm_chip *pc = platform_get_drvdata(pdev);

	/*
	 * Disable the PWM clk before unpreparing it if the PWM device is still
	 * running. This should only happen when the last PWM user left it
	 * enabled, or when nobody requested a PWM that was previously enabled
	 * by the bootloader.
	 *
	 * FIXME: Maybe the core should disable all PWM devices in
	 * pwmchip_remove(). In this case we'd only have to call
	 * clk_unprepare() after pwmchip_remove().
	 *
	 */
	if (pwm_is_enabled(pc->chip.pwms))
		clk_disable(pc->clk);

	clk_unprepare(pc->pclk);
	clk_unprepare(pc->clk);

	return pwmchip_remove(&pc->chip);
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
