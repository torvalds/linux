/*
 * Intel Low Power Subsystem PWM controller driver
 *
 * Copyright (C) 2014, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 * Author: Chew Kean Ho <kean.ho.chew@intel.com>
 * Author: Chang Rebecca Swee Fun <rebecca.swee.fun.chang@intel.com>
 * Author: Chew Chiau Ee <chiau.ee.chew@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/platform_device.h>

#define PWM				0x00000000
#define PWM_ENABLE			BIT(31)
#define PWM_SW_UPDATE			BIT(30)
#define PWM_BASE_UNIT_SHIFT		8
#define PWM_BASE_UNIT_MASK		0x00ffff00
#define PWM_ON_TIME_DIV_MASK		0x000000ff
#define PWM_DIVISION_CORRECTION		0x2
#define PWM_LIMIT			(0x8000 + PWM_DIVISION_CORRECTION)
#define NSECS_PER_SEC			1000000000UL

struct pwm_lpss_chip {
	struct pwm_chip chip;
	void __iomem *regs;
	struct clk *clk;
};

static inline struct pwm_lpss_chip *to_lpwm(struct pwm_chip *chip)
{
	return container_of(chip, struct pwm_lpss_chip, chip);
}

static int pwm_lpss_config(struct pwm_chip *chip, struct pwm_device *pwm,
			   int duty_ns, int period_ns)
{
	struct pwm_lpss_chip *lpwm = to_lpwm(chip);
	u8 on_time_div;
	unsigned long c;
	unsigned long long base_unit, freq = NSECS_PER_SEC;
	u32 ctrl;

	do_div(freq, period_ns);

	/* The equation is: base_unit = ((freq / c) * 65536) + correction */
	base_unit = freq * 65536;

	c = clk_get_rate(lpwm->clk);
	if (!c)
		return -EINVAL;

	do_div(base_unit, c);
	base_unit += PWM_DIVISION_CORRECTION;
	if (base_unit > PWM_LIMIT)
		return -EINVAL;

	if (duty_ns <= 0)
		duty_ns = 1;
	on_time_div = 255 - (255 * duty_ns / period_ns);

	ctrl = readl(lpwm->regs + PWM);
	ctrl &= ~(PWM_BASE_UNIT_MASK | PWM_ON_TIME_DIV_MASK);
	ctrl |= (u16) base_unit << PWM_BASE_UNIT_SHIFT;
	ctrl |= on_time_div;
	/* request PWM to update on next cycle */
	ctrl |= PWM_SW_UPDATE;
	writel(ctrl, lpwm->regs + PWM);

	return 0;
}

static int pwm_lpss_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_lpss_chip *lpwm = to_lpwm(chip);
	u32 ctrl;
	int ret;

	ret = clk_prepare_enable(lpwm->clk);
	if (ret)
		return ret;

	ctrl = readl(lpwm->regs + PWM);
	writel(ctrl | PWM_ENABLE, lpwm->regs + PWM);

	return 0;
}

static void pwm_lpss_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_lpss_chip *lpwm = to_lpwm(chip);
	u32 ctrl;

	ctrl = readl(lpwm->regs + PWM);
	writel(ctrl & ~PWM_ENABLE, lpwm->regs + PWM);

	clk_disable_unprepare(lpwm->clk);
}

static const struct pwm_ops pwm_lpss_ops = {
	.config = pwm_lpss_config,
	.enable = pwm_lpss_enable,
	.disable = pwm_lpss_disable,
	.owner = THIS_MODULE,
};

static const struct acpi_device_id pwm_lpss_acpi_match[] = {
	{ "80860F09", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, pwm_lpss_acpi_match);

static int pwm_lpss_probe(struct platform_device *pdev)
{
	struct pwm_lpss_chip *lpwm;
	struct resource *r;
	int ret;

	lpwm = devm_kzalloc(&pdev->dev, sizeof(*lpwm), GFP_KERNEL);
	if (!lpwm)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	lpwm->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(lpwm->regs))
		return PTR_ERR(lpwm->regs);

	lpwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lpwm->clk)) {
		dev_err(&pdev->dev, "failed to get PWM clock\n");
		return PTR_ERR(lpwm->clk);
	}

	lpwm->chip.dev = &pdev->dev;
	lpwm->chip.ops = &pwm_lpss_ops;
	lpwm->chip.base = -1;
	lpwm->chip.npwm = 1;

	ret = pwmchip_add(&lpwm->chip);
	if (ret) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, lpwm);
	return 0;
}

static int pwm_lpss_remove(struct platform_device *pdev)
{
	struct pwm_lpss_chip *lpwm = platform_get_drvdata(pdev);
	u32 ctrl;

	ctrl = readl(lpwm->regs + PWM);
	writel(ctrl & ~PWM_ENABLE, lpwm->regs + PWM);

	return pwmchip_remove(&lpwm->chip);
}

static struct platform_driver pwm_lpss_driver = {
	.driver = {
		.name = "pwm-lpss",
		.acpi_match_table = pwm_lpss_acpi_match,
	},
	.probe = pwm_lpss_probe,
	.remove = pwm_lpss_remove,
};
module_platform_driver(pwm_lpss_driver);

MODULE_DESCRIPTION("PWM driver for Intel LPSS");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pwm-lpss");
