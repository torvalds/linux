/*
 * linux/arch/unicore32/kernel/pwm.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>

#include <asm/div64.h>
#include <mach/hardware.h>

struct puv3_pwm_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
	bool enabled;
};

static inline struct puv3_pwm_chip *to_puv3(struct pwm_chip *chip)
{
	return container_of(chip, struct puv3_pwm_chip, chip);
}

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
static int puv3_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			   int duty_ns, int period_ns)
{
	unsigned long period_cycles, prescale, pv, dc;
	struct puv3_pwm_chip *puv3 = to_puv3(chip);
	unsigned long long c;

	c = clk_get_rate(puv3->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;

	prescale = (period_cycles - 1) / 1024;
	pv = period_cycles / (prescale + 1) - 1;

	if (prescale > 63)
		return -EINVAL;

	if (duty_ns == period_ns)
		dc = OST_PWMDCCR_FDCYCLE;
	else
		dc = (pv + 1) * duty_ns / period_ns;

	/*
	 * NOTE: the clock to PWM has to be enabled first
	 * before writing to the registers
	 */
	clk_prepare_enable(puv3->clk);

	writel(prescale, puv3->base + OST_PWM_PWCR);
	writel(pv - dc, puv3->base + OST_PWM_DCCR);
	writel(pv, puv3->base + OST_PWM_PCR);

	clk_disable_unprepare(puv3->clk);

	return 0;
}

static int puv3_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct puv3_pwm_chip *puv3 = to_puv3(chip);

	return clk_prepare_enable(puv3->clk);
}

static void puv3_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct puv3_pwm_chip *puv3 = to_puv3(chip);

	clk_disable_unprepare(puv3->clk);
}

static const struct pwm_ops puv3_pwm_ops = {
	.config = puv3_pwm_config,
	.enable = puv3_pwm_enable,
	.disable = puv3_pwm_disable,
	.owner = THIS_MODULE,
};

static int __devinit pwm_probe(struct platform_device *pdev)
{
	struct puv3_pwm_chip *puv3;
	struct resource *r;
	int ret;

	puv3 = devm_kzalloc(&pdev->dev, sizeof(*puv3), GFP_KERNEL);
	if (puv3 == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	puv3->clk = devm_clk_get(&pdev->dev, "OST_CLK");
	if (IS_ERR(puv3->clk))
		return PTR_ERR(puv3->clk);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	puv3->base = devm_request_and_ioremap(&pdev->dev, r);
	if (puv3->base == NULL)
		return -EADDRNOTAVAIL;

	puv3->chip.dev = &pdev->dev;
	puv3->chip.ops = &puv3_pwm_ops;
	puv3->chip.base = -1;
	puv3->chip.npwm = 1;

	ret = pwmchip_add(&puv3->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, puv3);
	return 0;
}

static int __devexit pwm_remove(struct platform_device *pdev)
{
	struct puv3_pwm_chip *puv3 = platform_get_drvdata(pdev);

	return pwmchip_remove(&puv3->chip);
}

static struct platform_driver puv3_pwm_driver = {
	.driver = {
		.name = "PKUnity-v3-PWM",
	},
	.probe = pwm_probe,
	.remove = __devexit_p(pwm_remove),
};
module_platform_driver(puv3_pwm_driver);

MODULE_LICENSE("GPL v2");
