// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Author: Shobhit Kumar <shobhit.kumar@intel.com>
 */

#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/pwm.h>

#define PWM0_CLK_DIV		0x4B
#define  PWM_OUTPUT_ENABLE	BIT(7)
#define  PWM_DIV_CLK_0		0x00 /* DIVIDECLK = BASECLK */
#define  PWM_DIV_CLK_100	0x63 /* DIVIDECLK = BASECLK/100 */
#define  PWM_DIV_CLK_128	0x7F /* DIVIDECLK = BASECLK/128 */

#define PWM0_DUTY_CYCLE		0x4E
#define BACKLIGHT_EN		0x51

#define PWM_MAX_LEVEL		0xFF

#define PWM_BASE_CLK		6000000  /* 6 MHz */
#define PWM_MAX_PERIOD_NS	21333    /* 46.875KHz */

/**
 * struct crystalcove_pwm - Crystal Cove PWM controller
 * @chip: the abstract pwm_chip structure.
 * @regmap: the regmap from the parent device.
 */
struct crystalcove_pwm {
	struct pwm_chip chip;
	struct regmap *regmap;
};

static inline struct crystalcove_pwm *to_crc_pwm(struct pwm_chip *pc)
{
	return container_of(pc, struct crystalcove_pwm, chip);
}

static int crc_pwm_enable(struct pwm_chip *c, struct pwm_device *pwm)
{
	struct crystalcove_pwm *crc_pwm = to_crc_pwm(c);

	regmap_write(crc_pwm->regmap, BACKLIGHT_EN, 1);

	return 0;
}

static void crc_pwm_disable(struct pwm_chip *c, struct pwm_device *pwm)
{
	struct crystalcove_pwm *crc_pwm = to_crc_pwm(c);

	regmap_write(crc_pwm->regmap, BACKLIGHT_EN, 0);
}

static int crc_pwm_config(struct pwm_chip *c, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	struct crystalcove_pwm *crc_pwm = to_crc_pwm(c);
	struct device *dev = crc_pwm->chip.dev;
	int level;

	if (period_ns > PWM_MAX_PERIOD_NS) {
		dev_err(dev, "un-supported period_ns\n");
		return -EINVAL;
	}

	if (pwm_get_period(pwm) != period_ns) {
		int clk_div;

		/* changing the clk divisor, need to disable fisrt */
		crc_pwm_disable(c, pwm);
		clk_div = PWM_BASE_CLK * period_ns / NSEC_PER_SEC;

		regmap_write(crc_pwm->regmap, PWM0_CLK_DIV,
					clk_div | PWM_OUTPUT_ENABLE);

		/* enable back */
		crc_pwm_enable(c, pwm);
	}

	/* change the pwm duty cycle */
	level = duty_ns * PWM_MAX_LEVEL / period_ns;
	regmap_write(crc_pwm->regmap, PWM0_DUTY_CYCLE, level);

	return 0;
}

static const struct pwm_ops crc_pwm_ops = {
	.config = crc_pwm_config,
	.enable = crc_pwm_enable,
	.disable = crc_pwm_disable,
};

static int crystalcove_pwm_probe(struct platform_device *pdev)
{
	struct crystalcove_pwm *pwm;
	struct device *dev = pdev->dev.parent;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &crc_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = 1;

	/* get the PMIC regmap */
	pwm->regmap = pmic->regmap;

	platform_set_drvdata(pdev, pwm);

	return pwmchip_add(&pwm->chip);
}

static int crystalcove_pwm_remove(struct platform_device *pdev)
{
	struct crystalcove_pwm *pwm = platform_get_drvdata(pdev);

	return pwmchip_remove(&pwm->chip);
}

static struct platform_driver crystalcove_pwm_driver = {
	.probe = crystalcove_pwm_probe,
	.remove = crystalcove_pwm_remove,
	.driver = {
		.name = "crystal_cove_pwm",
	},
};

builtin_platform_driver(crystalcove_pwm_driver);
