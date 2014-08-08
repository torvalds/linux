/*
 * PWM driver for Rockchip SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
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
#define PWM_INACTIVE_NEGATIVE	(0 << 4)
#define PWM_OUTPUT_LEFT		(0 << 5)
#define PWM_LP_DISABLE		(0 << 8)

struct rockchip_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
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

	void (*set_enable)(struct pwm_chip *chip, bool enable);
};

static inline struct rockchip_pwm_chip *to_rockchip_pwm_chip(struct pwm_chip *c)
{
	return container_of(c, struct rockchip_pwm_chip, chip);
}

static void rockchip_pwm_set_enable_v1(struct pwm_chip *chip, bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 enable_conf = PWM_CTRL_OUTPUT_EN | PWM_CTRL_TIMER_EN;
	u32 val;

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);

	if (enable)
		val |= enable_conf;
	else
		val &= ~enable_conf;

	writel_relaxed(val, pc->base + pc->data->regs.ctrl);
}

static void rockchip_pwm_set_enable_v2(struct pwm_chip *chip, bool enable)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	u32 enable_conf = PWM_OUTPUT_LEFT | PWM_LP_DISABLE | PWM_ENABLE |
			  PWM_CONTINUOUS | PWM_DUTY_POSITIVE |
			  PWM_INACTIVE_NEGATIVE;
	u32 val;

	val = readl_relaxed(pc->base + pc->data->regs.ctrl);

	if (enable)
		val |= enable_conf;
	else
		val &= ~enable_conf;

	writel_relaxed(val, pc->base + pc->data->regs.ctrl);
}

static int rockchip_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			       int duty_ns, int period_ns)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	unsigned long period, duty;
	u64 clk_rate, div;
	int ret;

	clk_rate = clk_get_rate(pc->clk);

	/*
	 * Since period and duty cycle registers have a width of 32
	 * bits, every possible input period can be obtained using the
	 * default prescaler value for all practical clock rate values.
	 */
	div = clk_rate * period_ns;
	do_div(div, pc->data->prescaler * NSEC_PER_SEC);
	period = div;

	div = clk_rate * duty_ns;
	do_div(div, pc->data->prescaler * NSEC_PER_SEC);
	duty = div;

	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	writel(period, pc->base + pc->data->regs.period);
	writel(duty, pc->base + pc->data->regs.duty);
	writel(0, pc->base + pc->data->regs.cntr);

	clk_disable(pc->clk);

	return 0;
}

static int rockchip_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);
	int ret;

	ret = clk_enable(pc->clk);
	if (ret)
		return ret;

	pc->data->set_enable(chip, true);

	return 0;
}

static void rockchip_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct rockchip_pwm_chip *pc = to_rockchip_pwm_chip(chip);

	pc->data->set_enable(chip, false);

	clk_disable(pc->clk);
}

static const struct pwm_ops rockchip_pwm_ops = {
	.config = rockchip_pwm_config,
	.enable = rockchip_pwm_enable,
	.disable = rockchip_pwm_disable,
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
	.set_enable = rockchip_pwm_set_enable_v1,
};

static const struct rockchip_pwm_data pwm_data_v2 = {
	.regs = {
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x00,
		.ctrl = 0x0c,
	},
	.prescaler = 1,
	.set_enable = rockchip_pwm_set_enable_v2,
};

static const struct rockchip_pwm_data pwm_data_vop = {
	.regs = {
		.duty = 0x08,
		.period = 0x04,
		.cntr = 0x0c,
		.ctrl = 0x00,
	},
	.prescaler = 1,
	.set_enable = rockchip_pwm_set_enable_v2,
};

static const struct of_device_id rockchip_pwm_dt_ids[] = {
	{ .compatible = "rockchip,rk2928-pwm", .data = &pwm_data_v1},
	{ .compatible = "rockchip,rk3288-pwm", .data = &pwm_data_v2},
	{ .compatible = "rockchip,vop-pwm", .data = &pwm_data_vop},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rockchip_pwm_dt_ids);

static int rockchip_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct rockchip_pwm_chip *pc;
	struct resource *r;
	int ret;

	id = of_match_device(rockchip_pwm_dt_ids, &pdev->dev);
	if (!id)
		return -EINVAL;

	pc = devm_kzalloc(&pdev->dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pc->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pc->base))
		return PTR_ERR(pc->base);

	pc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	ret = clk_prepare(pc->clk);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pc);

	pc->data = id->data;
	pc->chip.dev = &pdev->dev;
	pc->chip.ops = &rockchip_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = 1;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		clk_unprepare(pc->clk);
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
	}

	return ret;
}

static int rockchip_pwm_remove(struct platform_device *pdev)
{
	struct rockchip_pwm_chip *pc = platform_get_drvdata(pdev);

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
