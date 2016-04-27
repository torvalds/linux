/*
 * Intel Low Power Subsystem PWM controller driver
 *
 * Copyright (C) 2014, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 * Author: Chew Kean Ho <kean.ho.chew@intel.com>
 * Author: Chang Rebecca Swee Fun <rebecca.swee.fun.chang@intel.com>
 * Author: Chew Chiau Ee <chiau.ee.chew@intel.com>
 * Author: Alan Cox <alan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/time.h>

#include "pwm-lpss.h"

#define PWM				0x00000000
#define PWM_ENABLE			BIT(31)
#define PWM_SW_UPDATE			BIT(30)
#define PWM_BASE_UNIT_SHIFT		8
#define PWM_ON_TIME_DIV_MASK		0x000000ff
#define PWM_DIVISION_CORRECTION		0x2

/* Size of each PWM register space if multiple */
#define PWM_SIZE			0x400

struct pwm_lpss_chip {
	struct pwm_chip chip;
	void __iomem *regs;
	const struct pwm_lpss_boardinfo *info;
};

/* BayTrail */
const struct pwm_lpss_boardinfo pwm_lpss_byt_info = {
	.clk_rate = 25000000,
	.npwm = 1,
	.base_unit_bits = 16,
};
EXPORT_SYMBOL_GPL(pwm_lpss_byt_info);

/* Braswell */
const struct pwm_lpss_boardinfo pwm_lpss_bsw_info = {
	.clk_rate = 19200000,
	.npwm = 1,
	.base_unit_bits = 16,
};
EXPORT_SYMBOL_GPL(pwm_lpss_bsw_info);

/* Broxton */
const struct pwm_lpss_boardinfo pwm_lpss_bxt_info = {
	.clk_rate = 19200000,
	.npwm = 4,
	.base_unit_bits = 22,
};
EXPORT_SYMBOL_GPL(pwm_lpss_bxt_info);

static inline struct pwm_lpss_chip *to_lpwm(struct pwm_chip *chip)
{
	return container_of(chip, struct pwm_lpss_chip, chip);
}

static inline u32 pwm_lpss_read(const struct pwm_device *pwm)
{
	struct pwm_lpss_chip *lpwm = to_lpwm(pwm->chip);

	return readl(lpwm->regs + pwm->hwpwm * PWM_SIZE + PWM);
}

static inline void pwm_lpss_write(const struct pwm_device *pwm, u32 value)
{
	struct pwm_lpss_chip *lpwm = to_lpwm(pwm->chip);

	writel(value, lpwm->regs + pwm->hwpwm * PWM_SIZE + PWM);
}

static void pwm_lpss_update(struct pwm_device *pwm)
{
	pwm_lpss_write(pwm, pwm_lpss_read(pwm) | PWM_SW_UPDATE);
	/* Give it some time to propagate */
	usleep_range(10, 50);
}

static int pwm_lpss_config(struct pwm_chip *chip, struct pwm_device *pwm,
			   int duty_ns, int period_ns)
{
	struct pwm_lpss_chip *lpwm = to_lpwm(chip);
	u8 on_time_div;
	unsigned long c, base_unit_range;
	unsigned long long base_unit, freq = NSEC_PER_SEC;
	u32 ctrl;

	do_div(freq, period_ns);

	/*
	 * The equation is:
	 * base_unit = ((freq / c) * base_unit_range) + correction
	 */
	base_unit_range = BIT(lpwm->info->base_unit_bits);
	base_unit = freq * base_unit_range;

	c = lpwm->info->clk_rate;
	if (!c)
		return -EINVAL;

	do_div(base_unit, c);
	base_unit += PWM_DIVISION_CORRECTION;

	if (duty_ns <= 0)
		duty_ns = 1;
	on_time_div = 255 - (255 * duty_ns / period_ns);

	pm_runtime_get_sync(chip->dev);

	ctrl = pwm_lpss_read(pwm);
	ctrl &= ~PWM_ON_TIME_DIV_MASK;
	ctrl &= ~((base_unit_range - 1) << PWM_BASE_UNIT_SHIFT);
	base_unit &= (base_unit_range - 1);
	ctrl |= (u32) base_unit << PWM_BASE_UNIT_SHIFT;
	ctrl |= on_time_div;
	pwm_lpss_write(pwm, ctrl);

	/*
	 * If the PWM is already enabled we need to notify the hardware
	 * about the change by setting PWM_SW_UPDATE.
	 */
	if (pwm_is_enabled(pwm))
		pwm_lpss_update(pwm);

	pm_runtime_put(chip->dev);

	return 0;
}

static int pwm_lpss_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	pm_runtime_get_sync(chip->dev);

	/*
	 * Hardware must first see PWM_SW_UPDATE before the PWM can be
	 * enabled.
	 */
	pwm_lpss_update(pwm);
	pwm_lpss_write(pwm, pwm_lpss_read(pwm) | PWM_ENABLE);
	return 0;
}

static void pwm_lpss_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	pwm_lpss_write(pwm, pwm_lpss_read(pwm) & ~PWM_ENABLE);
	pm_runtime_put(chip->dev);
}

static const struct pwm_ops pwm_lpss_ops = {
	.config = pwm_lpss_config,
	.enable = pwm_lpss_enable,
	.disable = pwm_lpss_disable,
	.owner = THIS_MODULE,
};

struct pwm_lpss_chip *pwm_lpss_probe(struct device *dev, struct resource *r,
				     const struct pwm_lpss_boardinfo *info)
{
	struct pwm_lpss_chip *lpwm;
	int ret;

	lpwm = devm_kzalloc(dev, sizeof(*lpwm), GFP_KERNEL);
	if (!lpwm)
		return ERR_PTR(-ENOMEM);

	lpwm->regs = devm_ioremap_resource(dev, r);
	if (IS_ERR(lpwm->regs))
		return ERR_CAST(lpwm->regs);

	lpwm->info = info;
	lpwm->chip.dev = dev;
	lpwm->chip.ops = &pwm_lpss_ops;
	lpwm->chip.base = -1;
	lpwm->chip.npwm = info->npwm;

	ret = pwmchip_add(&lpwm->chip);
	if (ret) {
		dev_err(dev, "failed to add PWM chip: %d\n", ret);
		return ERR_PTR(ret);
	}

	return lpwm;
}
EXPORT_SYMBOL_GPL(pwm_lpss_probe);

int pwm_lpss_remove(struct pwm_lpss_chip *lpwm)
{
	return pwmchip_remove(&lpwm->chip);
}
EXPORT_SYMBOL_GPL(pwm_lpss_remove);

MODULE_DESCRIPTION("PWM driver for Intel LPSS");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_LICENSE("GPL v2");
