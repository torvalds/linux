// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/stmp_device.h>

#define SET	0x4
#define CLR	0x8
#define TOG	0xc

#define PWM_CTRL		0x0
#define PWM_ACTIVE0		0x10
#define PWM_PERIOD0		0x20
#define  PERIOD_PERIOD(p)	((p) & 0xffff)
#define  PERIOD_PERIOD_MAX	0x10000
#define  PERIOD_ACTIVE_HIGH	(3 << 16)
#define  PERIOD_ACTIVE_LOW	(2 << 16)
#define  PERIOD_INACTIVE_HIGH	(3 << 18)
#define  PERIOD_INACTIVE_LOW	(2 << 18)
#define  PERIOD_POLARITY_NORMAL	(PERIOD_ACTIVE_HIGH | PERIOD_INACTIVE_LOW)
#define  PERIOD_POLARITY_INVERSE	(PERIOD_ACTIVE_LOW | PERIOD_INACTIVE_HIGH)
#define  PERIOD_CDIV(div)	(((div) & 0x7) << 20)
#define  PERIOD_CDIV_MAX	8

static const u8 cdiv_shift[PERIOD_CDIV_MAX] = {
	0, 1, 2, 3, 4, 6, 8, 10
};

struct mxs_pwm_chip {
	struct clk *clk;
	void __iomem *base;
};

static inline struct mxs_pwm_chip *to_mxs_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int mxs_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct mxs_pwm_chip *mxs = to_mxs_pwm_chip(chip);
	int ret, div = 0;
	unsigned int period_cycles, duty_cycles;
	unsigned long rate;
	unsigned long long c;
	unsigned int pol_bits;

	/*
	 * If the PWM channel is disabled, make sure to turn on the
	 * clock before calling clk_get_rate() and writing to the
	 * registers. Otherwise, just keep it enabled.
	 */
	if (!pwm_is_enabled(pwm)) {
		ret = clk_prepare_enable(mxs->clk);
		if (ret)
			return ret;
	}

	if (!state->enabled && pwm_is_enabled(pwm))
		writel(1 << pwm->hwpwm, mxs->base + PWM_CTRL + CLR);

	rate = clk_get_rate(mxs->clk);
	while (1) {
		c = rate >> cdiv_shift[div];
		c = c * state->period;
		do_div(c, 1000000000);
		if (c < PERIOD_PERIOD_MAX)
			break;
		div++;
		if (div >= PERIOD_CDIV_MAX)
			return -EINVAL;
	}

	period_cycles = c;
	c *= state->duty_cycle;
	do_div(c, state->period);
	duty_cycles = c;

	/*
	 * The data sheet the says registers must be written to in
	 * this order (ACTIVEn, then PERIODn). Also, the new settings
	 * only take effect at the beginning of a new period, avoiding
	 * glitches.
	 */

	pol_bits = state->polarity == PWM_POLARITY_NORMAL ?
		PERIOD_POLARITY_NORMAL : PERIOD_POLARITY_INVERSE;
	writel(duty_cycles << 16,
	       mxs->base + PWM_ACTIVE0 + pwm->hwpwm * 0x20);
	writel(PERIOD_PERIOD(period_cycles) | pol_bits | PERIOD_CDIV(div),
	       mxs->base + PWM_PERIOD0 + pwm->hwpwm * 0x20);

	if (state->enabled) {
		if (!pwm_is_enabled(pwm)) {
			/*
			 * The clock was enabled above. Just enable
			 * the channel in the control register.
			 */
			writel(1 << pwm->hwpwm, mxs->base + PWM_CTRL + SET);
		}
	} else {
		clk_disable_unprepare(mxs->clk);
	}
	return 0;
}

static const struct pwm_ops mxs_pwm_ops = {
	.apply = mxs_pwm_apply,
};

static int mxs_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct pwm_chip *chip;
	struct mxs_pwm_chip *mxs;
	u32 npwm;
	int ret;

	ret = of_property_read_u32(np, "fsl,pwm-number", &npwm);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get pwm number: %d\n", ret);
		return ret;
	}

	chip = devm_pwmchip_alloc(&pdev->dev, npwm, sizeof(*mxs));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	mxs = to_mxs_pwm_chip(chip);

	mxs->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mxs->base))
		return PTR_ERR(mxs->base);

	mxs->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mxs->clk))
		return PTR_ERR(mxs->clk);

	chip->ops = &mxs_pwm_ops;

	/* FIXME: Only do this if the PWM isn't already running */
	ret = stmp_reset_block(mxs->base);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to reset PWM\n");

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add pwm chip %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id mxs_pwm_dt_ids[] = {
	{ .compatible = "fsl,imx23-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_pwm_dt_ids);

static struct platform_driver mxs_pwm_driver = {
	.driver = {
		.name = "mxs-pwm",
		.of_match_table = mxs_pwm_dt_ids,
	},
	.probe = mxs_pwm_probe,
};
module_platform_driver(mxs_pwm_driver);

MODULE_ALIAS("platform:mxs-pwm");
MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale MXS PWM Driver");
MODULE_LICENSE("GPL v2");
