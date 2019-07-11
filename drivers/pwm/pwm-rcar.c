// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car PWM Timer driver
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define RCAR_PWM_MAX_DIVISION	24
#define RCAR_PWM_MAX_CYCLE	1023

#define RCAR_PWMCR		0x00
#define  RCAR_PWMCR_CC0_MASK	0x000f0000
#define  RCAR_PWMCR_CC0_SHIFT	16
#define  RCAR_PWMCR_CCMD	BIT(15)
#define  RCAR_PWMCR_SYNC	BIT(11)
#define  RCAR_PWMCR_SS0		BIT(4)
#define  RCAR_PWMCR_EN0		BIT(0)

#define RCAR_PWMCNT		0x04
#define  RCAR_PWMCNT_CYC0_MASK	0x03ff0000
#define  RCAR_PWMCNT_CYC0_SHIFT	16
#define  RCAR_PWMCNT_PH0_MASK	0x000003ff
#define  RCAR_PWMCNT_PH0_SHIFT	0

struct rcar_pwm_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
};

static inline struct rcar_pwm_chip *to_rcar_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct rcar_pwm_chip, chip);
}

static void rcar_pwm_write(struct rcar_pwm_chip *rp, u32 data,
			   unsigned int offset)
{
	writel(data, rp->base + offset);
}

static u32 rcar_pwm_read(struct rcar_pwm_chip *rp, unsigned int offset)
{
	return readl(rp->base + offset);
}

static void rcar_pwm_update(struct rcar_pwm_chip *rp, u32 mask, u32 data,
			    unsigned int offset)
{
	u32 value;

	value = rcar_pwm_read(rp, offset);
	value &= ~mask;
	value |= data & mask;
	rcar_pwm_write(rp, value, offset);
}

static int rcar_pwm_get_clock_division(struct rcar_pwm_chip *rp, int period_ns)
{
	unsigned long clk_rate = clk_get_rate(rp->clk);
	u64 div, tmp;

	if (clk_rate == 0)
		return -EINVAL;

	div = (u64)NSEC_PER_SEC * RCAR_PWM_MAX_CYCLE;
	tmp = (u64)period_ns * clk_rate + div - 1;
	tmp = div64_u64(tmp, div);
	div = ilog2(tmp - 1) + 1;

	return (div <= RCAR_PWM_MAX_DIVISION) ? div : -ERANGE;
}

static void rcar_pwm_set_clock_control(struct rcar_pwm_chip *rp,
				       unsigned int div)
{
	u32 value;

	value = rcar_pwm_read(rp, RCAR_PWMCR);
	value &= ~(RCAR_PWMCR_CCMD | RCAR_PWMCR_CC0_MASK);

	if (div & 1)
		value |= RCAR_PWMCR_CCMD;

	div >>= 1;

	value |= div << RCAR_PWMCR_CC0_SHIFT;
	rcar_pwm_write(rp, value, RCAR_PWMCR);
}

static int rcar_pwm_set_counter(struct rcar_pwm_chip *rp, int div, int duty_ns,
				int period_ns)
{
	unsigned long long one_cycle, tmp;	/* 0.01 nanoseconds */
	unsigned long clk_rate = clk_get_rate(rp->clk);
	u32 cyc, ph;

	one_cycle = (unsigned long long)NSEC_PER_SEC * 100ULL * (1 << div);
	do_div(one_cycle, clk_rate);

	tmp = period_ns * 100ULL;
	do_div(tmp, one_cycle);
	cyc = (tmp << RCAR_PWMCNT_CYC0_SHIFT) & RCAR_PWMCNT_CYC0_MASK;

	tmp = duty_ns * 100ULL;
	do_div(tmp, one_cycle);
	ph = tmp & RCAR_PWMCNT_PH0_MASK;

	/* Avoid prohibited setting */
	if (cyc == 0 || ph == 0)
		return -EINVAL;

	rcar_pwm_write(rp, cyc | ph, RCAR_PWMCNT);

	return 0;
}

static int rcar_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	return pm_runtime_get_sync(chip->dev);
}

static void rcar_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	pm_runtime_put(chip->dev);
}

static int rcar_pwm_enable(struct rcar_pwm_chip *rp)
{
	u32 value;

	/* Don't enable the PWM device if CYC0 or PH0 is 0 */
	value = rcar_pwm_read(rp, RCAR_PWMCNT);
	if ((value & RCAR_PWMCNT_CYC0_MASK) == 0 ||
	    (value & RCAR_PWMCNT_PH0_MASK) == 0)
		return -EINVAL;

	rcar_pwm_update(rp, RCAR_PWMCR_EN0, RCAR_PWMCR_EN0, RCAR_PWMCR);

	return 0;
}

static void rcar_pwm_disable(struct rcar_pwm_chip *rp)
{
	rcar_pwm_update(rp, RCAR_PWMCR_EN0, 0, RCAR_PWMCR);
}

static int rcar_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			  struct pwm_state *state)
{
	struct rcar_pwm_chip *rp = to_rcar_pwm_chip(chip);
	struct pwm_state cur_state;
	int div, ret;

	/* This HW/driver only supports normal polarity */
	pwm_get_state(pwm, &cur_state);
	if (state->polarity != PWM_POLARITY_NORMAL)
		return -ENOTSUPP;

	if (!state->enabled) {
		rcar_pwm_disable(rp);
		return 0;
	}

	div = rcar_pwm_get_clock_division(rp, state->period);
	if (div < 0)
		return div;

	rcar_pwm_update(rp, RCAR_PWMCR_SYNC, RCAR_PWMCR_SYNC, RCAR_PWMCR);

	ret = rcar_pwm_set_counter(rp, div, state->duty_cycle, state->period);
	if (!ret)
		rcar_pwm_set_clock_control(rp, div);

	/* The SYNC should be set to 0 even if rcar_pwm_set_counter failed */
	rcar_pwm_update(rp, RCAR_PWMCR_SYNC, 0, RCAR_PWMCR);

	if (!ret && state->enabled)
		ret = rcar_pwm_enable(rp);

	return ret;
}

static const struct pwm_ops rcar_pwm_ops = {
	.request = rcar_pwm_request,
	.free = rcar_pwm_free,
	.apply = rcar_pwm_apply,
	.owner = THIS_MODULE,
};

static int rcar_pwm_probe(struct platform_device *pdev)
{
	struct rcar_pwm_chip *rcar_pwm;
	struct resource *res;
	int ret;

	rcar_pwm = devm_kzalloc(&pdev->dev, sizeof(*rcar_pwm), GFP_KERNEL);
	if (rcar_pwm == NULL)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rcar_pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rcar_pwm->base))
		return PTR_ERR(rcar_pwm->base);

	rcar_pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(rcar_pwm->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		return PTR_ERR(rcar_pwm->clk);
	}

	platform_set_drvdata(pdev, rcar_pwm);

	rcar_pwm->chip.dev = &pdev->dev;
	rcar_pwm->chip.ops = &rcar_pwm_ops;
	rcar_pwm->chip.base = -1;
	rcar_pwm->chip.npwm = 1;

	ret = pwmchip_add(&rcar_pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM chip: %d\n", ret);
		return ret;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int rcar_pwm_remove(struct platform_device *pdev)
{
	struct rcar_pwm_chip *rcar_pwm = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	return pwmchip_remove(&rcar_pwm->chip);
}

static const struct of_device_id rcar_pwm_of_table[] = {
	{ .compatible = "renesas,pwm-rcar", },
	{ },
};
MODULE_DEVICE_TABLE(of, rcar_pwm_of_table);

#ifdef CONFIG_PM_SLEEP
static struct pwm_device *rcar_pwm_dev_to_pwm_dev(struct device *dev)
{
	struct rcar_pwm_chip *rcar_pwm = dev_get_drvdata(dev);
	struct pwm_chip *chip = &rcar_pwm->chip;

	return &chip->pwms[0];
}

static int rcar_pwm_suspend(struct device *dev)
{
	struct pwm_device *pwm = rcar_pwm_dev_to_pwm_dev(dev);

	if (!test_bit(PWMF_REQUESTED, &pwm->flags))
		return 0;

	pm_runtime_put(dev);

	return 0;
}

static int rcar_pwm_resume(struct device *dev)
{
	struct pwm_device *pwm = rcar_pwm_dev_to_pwm_dev(dev);
	struct pwm_state state;

	if (!test_bit(PWMF_REQUESTED, &pwm->flags))
		return 0;

	pm_runtime_get_sync(dev);

	pwm_get_state(pwm, &state);

	return rcar_pwm_apply(pwm->chip, pwm, &state);
}
#endif /* CONFIG_PM_SLEEP */
static SIMPLE_DEV_PM_OPS(rcar_pwm_pm_ops, rcar_pwm_suspend, rcar_pwm_resume);

static struct platform_driver rcar_pwm_driver = {
	.probe = rcar_pwm_probe,
	.remove = rcar_pwm_remove,
	.driver = {
		.name = "pwm-rcar",
		.pm	= &rcar_pwm_pm_ops,
		.of_match_table = of_match_ptr(rcar_pwm_of_table),
	}
};
module_platform_driver(rcar_pwm_driver);

MODULE_AUTHOR("Yoshihiro Shimoda <yoshihiro.shimoda.uh@renesas.com>");
MODULE_DESCRIPTION("Renesas PWM Timer Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pwm-rcar");
