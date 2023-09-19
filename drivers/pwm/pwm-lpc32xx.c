// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Alexandre Pereira da Silva <aletes.xgr@gmail.com>
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

#define PWM_ENABLE	BIT(31)
#define PWM_PIN_LEVEL	BIT(30)

#define to_lpc32xx_pwm_chip(_chip) \
	container_of(_chip, struct lpc32xx_pwm_chip, chip)

static int lpc32xx_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			      int duty_ns, int period_ns)
{
	struct lpc32xx_pwm_chip *lpc32xx = to_lpc32xx_pwm_chip(chip);
	unsigned long long c;
	int period_cycles, duty_cycles;
	u32 val;
	c = clk_get_rate(lpc32xx->clk);

	/* The highest acceptable divisor is 256, which is represented by 0 */
	period_cycles = div64_u64(c * period_ns,
			       (unsigned long long)NSEC_PER_SEC * 256);
	if (!period_cycles || period_cycles > 256)
		return -ERANGE;
	if (period_cycles == 256)
		period_cycles = 0;

	/* Compute 256 x #duty/period value and care for corner cases */
	duty_cycles = div64_u64((unsigned long long)(period_ns - duty_ns) * 256,
				period_ns);
	if (!duty_cycles)
		duty_cycles = 1;
	if (duty_cycles > 255)
		duty_cycles = 255;

	val = readl(lpc32xx->base);
	val &= ~0xFFFF;
	val |= (period_cycles << 8) | duty_cycles;
	writel(val, lpc32xx->base);

	return 0;
}

static int lpc32xx_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct lpc32xx_pwm_chip *lpc32xx = to_lpc32xx_pwm_chip(chip);
	u32 val;
	int ret;

	ret = clk_prepare_enable(lpc32xx->clk);
	if (ret)
		return ret;

	val = readl(lpc32xx->base);
	val |= PWM_ENABLE;
	writel(val, lpc32xx->base);

	return 0;
}

static void lpc32xx_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct lpc32xx_pwm_chip *lpc32xx = to_lpc32xx_pwm_chip(chip);
	u32 val;

	val = readl(lpc32xx->base);
	val &= ~PWM_ENABLE;
	writel(val, lpc32xx->base);

	clk_disable_unprepare(lpc32xx->clk);
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
	u32 val;

	lpc32xx = devm_kzalloc(&pdev->dev, sizeof(*lpc32xx), GFP_KERNEL);
	if (!lpc32xx)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lpc32xx->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(lpc32xx->base))
		return PTR_ERR(lpc32xx->base);

	lpc32xx->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lpc32xx->clk))
		return PTR_ERR(lpc32xx->clk);

	lpc32xx->chip.dev = &pdev->dev;
	lpc32xx->chip.ops = &lpc32xx_pwm_ops;
	lpc32xx->chip.npwm = 1;
	lpc32xx->chip.base = -1;

	/* If PWM is disabled, configure the output to the default value */
	val = readl(lpc32xx->base);
	val &= ~PWM_PIN_LEVEL;
	writel(val, lpc32xx->base);

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

static const struct of_device_id lpc32xx_pwm_dt_ids[] = {
	{ .compatible = "nxp,lpc3220-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lpc32xx_pwm_dt_ids);

static struct platform_driver lpc32xx_pwm_driver = {
	.driver = {
		.name = "lpc32xx-pwm",
		.of_match_table = lpc32xx_pwm_dt_ids,
	},
	.probe = lpc32xx_pwm_probe,
	.remove = lpc32xx_pwm_remove,
};
module_platform_driver(lpc32xx_pwm_driver);

MODULE_ALIAS("platform:lpc32xx-pwm");
MODULE_AUTHOR("Alexandre Pereira da Silva <aletes.xgr@gmail.com>");
MODULE_DESCRIPTION("LPC32XX PWM Driver");
MODULE_LICENSE("GPL v2");
