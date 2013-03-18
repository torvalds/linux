/*
 * Copyright 2012 Alexandre Pereira da Silva <aletes.xgr@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

struct lpc32xx_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *base;
};

#define PWM_ENABLE	(1 << 31)
#define PWM_RELOADV(x)	(((x) & 0xFF) << 8)
#define PWM_DUTY(x)	((x) & 0xFF)

#define to_lpc32xx_pwm_chip(_chip) \
	container_of(_chip, struct lpc32xx_pwm_chip, chip)

static int lpc32xx_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	struct lpc32xx_pwm_chip *lpc32xx = to_lpc32xx_pwm_chip(chip);
	unsigned long long c;
	int period_cycles, duty_cycles;

	c = clk_get_rate(lpc32xx->clk) / 256;
	c = c * period_ns;
	do_div(c, NSEC_PER_SEC);

	/* Handle high and low extremes */
	if (c == 0)
		c = 1;
	if (c > 255)
		c = 0; /* 0 set division by 256 */
	period_cycles = c;

	/* The duty-cycle value is as follows:
	 *
	 *  DUTY-CYCLE     HIGH LEVEL
	 *      1            99.9%
	 *      25           90.0%
	 *      128          50.0%
	 *      220          10.0%
	 *      255           0.1%
	 *      0             0.0%
	 *
	 * In other words, the register value is duty-cycle % 256 with
	 * duty-cycle in the range 1-256.
	 */
	c = 256 * duty_ns;
	do_div(c, period_ns);
	if (c > 255)
		c = 255;
	duty_cycles = 256 - c;

	writel(PWM_ENABLE | PWM_RELOADV(period_cycles) | PWM_DUTY(duty_cycles),
		lpc32xx->base + (pwm->hwpwm << 2));

	return 0;
}

static int lpc32xx_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct lpc32xx_pwm_chip *lpc32xx = to_lpc32xx_pwm_chip(chip);

	return clk_enable(lpc32xx->clk);
}

static void lpc32xx_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct lpc32xx_pwm_chip *lpc32xx = to_lpc32xx_pwm_chip(chip);

	writel(0, lpc32xx->base + (pwm->hwpwm << 2));
	clk_disable(lpc32xx->clk);
}

static const struct pwm_ops lpc32xx_pwm_ops = {
	.config = lpc32xx_pwm_config,
	.enable = lpc32xx_pwm_enable,
	.disable = lpc32xx_pwm_disable,
	.owner = THIS_MODULE,
};

static int lpc32xx_pwm_probe(struct platform_device *pdev)
{
	struct lpc32xx_pwm_chip *lpc32xx;
	struct resource *res;
	int ret;

	lpc32xx = devm_kzalloc(&pdev->dev, sizeof(*lpc32xx), GFP_KERNEL);
	if (!lpc32xx)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	lpc32xx->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lpc32xx->base))
		return PTR_ERR(lpc32xx->base);

	lpc32xx->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lpc32xx->clk))
		return PTR_ERR(lpc32xx->clk);

	lpc32xx->chip.dev = &pdev->dev;
	lpc32xx->chip.ops = &lpc32xx_pwm_ops;
	lpc32xx->chip.npwm = 2;
	lpc32xx->chip.base = -1;

	ret = pwmchip_add(&lpc32xx->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip, error %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, lpc32xx);

	return 0;
}

static int lpc32xx_pwm_remove(struct platform_device *pdev)
{
	struct lpc32xx_pwm_chip *lpc32xx = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < lpc32xx->chip.npwm; i++)
		pwm_disable(&lpc32xx->chip.pwms[i]);

	return pwmchip_remove(&lpc32xx->chip);
}

static struct of_device_id lpc32xx_pwm_dt_ids[] = {
	{ .compatible = "nxp,lpc3220-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lpc32xx_pwm_dt_ids);

static struct platform_driver lpc32xx_pwm_driver = {
	.driver = {
		.name = "lpc32xx-pwm",
		.of_match_table = of_match_ptr(lpc32xx_pwm_dt_ids),
	},
	.probe = lpc32xx_pwm_probe,
	.remove = lpc32xx_pwm_remove,
};
module_platform_driver(lpc32xx_pwm_driver);

MODULE_ALIAS("platform:lpc32xx-pwm");
MODULE_AUTHOR("Alexandre Pereira da Silva <aletes.xgr@gmail.com>");
MODULE_DESCRIPTION("LPC32XX PWM Driver");
MODULE_LICENSE("GPL v2");
