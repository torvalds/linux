// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/atmel-hlcdc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define ATMEL_HLCDC_PWMCVAL_MASK	GENMASK(15, 8)
#define ATMEL_HLCDC_PWMCVAL(x)		(((x) << 8) & ATMEL_HLCDC_PWMCVAL_MASK)
#define ATMEL_HLCDC_PWMPOL		BIT(4)
#define ATMEL_HLCDC_PWMPS_MASK		GENMASK(2, 0)
#define ATMEL_HLCDC_PWMPS_MAX		0x6
#define ATMEL_HLCDC_PWMPS(x)		((x) & ATMEL_HLCDC_PWMPS_MASK)

struct atmel_hlcdc_pwm_errata {
	bool slow_clk_erratum;
	bool div1_clk_erratum;
};

struct atmel_hlcdc_pwm {
	struct pwm_chip chip;
	struct atmel_hlcdc *hlcdc;
	struct clk *cur_clk;
	const struct atmel_hlcdc_pwm_errata *errata;
};

static inline struct atmel_hlcdc_pwm *to_atmel_hlcdc_pwm(struct pwm_chip *chip)
{
	return container_of(chip, struct atmel_hlcdc_pwm, chip);
}

static int atmel_hlcdc_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
				 const struct pwm_state *state)
{
	struct atmel_hlcdc_pwm *atmel = to_atmel_hlcdc_pwm(chip);
	struct atmel_hlcdc *hlcdc = atmel->hlcdc;
	unsigned int status;
	int ret;

	if (state->enabled) {
		struct clk *new_clk = hlcdc->slow_clk;
		u64 pwmcval = state->duty_cycle * 256;
		unsigned long clk_freq;
		u64 clk_period_ns;
		u32 pwmcfg;
		int pres;

		if (!atmel->errata || !atmel->errata->slow_clk_erratum) {
			clk_freq = clk_get_rate(new_clk);
			if (!clk_freq)
				return -EINVAL;

			clk_period_ns = (u64)NSEC_PER_SEC * 256;
			do_div(clk_period_ns, clk_freq);
		}

		/* Errata: cannot use slow clk on some IP revisions */
		if ((atmel->errata && atmel->errata->slow_clk_erratum) ||
		    clk_period_ns > state->period) {
			new_clk = hlcdc->sys_clk;
			clk_freq = clk_get_rate(new_clk);
			if (!clk_freq)
				return -EINVAL;

			clk_period_ns = (u64)NSEC_PER_SEC * 256;
			do_div(clk_period_ns, clk_freq);
		}

		for (pres = 0; pres <= ATMEL_HLCDC_PWMPS_MAX; pres++) {
		/* Errata: cannot divide by 1 on some IP revisions */
			if (!pres && atmel->errata &&
			    atmel->errata->div1_clk_erratum)
				continue;

			if ((clk_period_ns << pres) >= state->period)
				break;
		}

		if (pres > ATMEL_HLCDC_PWMPS_MAX)
			return -EINVAL;

		pwmcfg = ATMEL_HLCDC_PWMPS(pres);

		if (new_clk != atmel->cur_clk) {
			u32 gencfg = 0;
			int ret;

			ret = clk_prepare_enable(new_clk);
			if (ret)
				return ret;

			clk_disable_unprepare(atmel->cur_clk);
			atmel->cur_clk = new_clk;

			if (new_clk == hlcdc->sys_clk)
				gencfg = ATMEL_HLCDC_CLKPWMSEL;

			ret = regmap_update_bits(hlcdc->regmap,
						 ATMEL_HLCDC_CFG(0),
						 ATMEL_HLCDC_CLKPWMSEL,
						 gencfg);
			if (ret)
				return ret;
		}

		do_div(pwmcval, state->period);

		/*
		 * The PWM duty cycle is configurable from 0/256 to 255/256 of
		 * the period cycle. Hence we can't set a duty cycle occupying
		 * the whole period cycle if we're asked to.
		 * Set it to 255 if pwmcval is greater than 256.
		 */
		if (pwmcval > 255)
			pwmcval = 255;

		pwmcfg |= ATMEL_HLCDC_PWMCVAL(pwmcval);

		if (state->polarity == PWM_POLARITY_NORMAL)
			pwmcfg |= ATMEL_HLCDC_PWMPOL;

		ret = regmap_update_bits(hlcdc->regmap, ATMEL_HLCDC_CFG(6),
					 ATMEL_HLCDC_PWMCVAL_MASK |
					 ATMEL_HLCDC_PWMPS_MASK |
					 ATMEL_HLCDC_PWMPOL,
					 pwmcfg);
		if (ret)
			return ret;

		ret = regmap_write(hlcdc->regmap, ATMEL_HLCDC_EN,
				   ATMEL_HLCDC_PWM);
		if (ret)
			return ret;

		ret = regmap_read_poll_timeout(hlcdc->regmap, ATMEL_HLCDC_SR,
					       status,
					       status & ATMEL_HLCDC_PWM,
					       10, 0);
		if (ret)
			return ret;
	} else {
		ret = regmap_write(hlcdc->regmap, ATMEL_HLCDC_DIS,
				   ATMEL_HLCDC_PWM);
		if (ret)
			return ret;

		ret = regmap_read_poll_timeout(hlcdc->regmap, ATMEL_HLCDC_SR,
					       status,
					       !(status & ATMEL_HLCDC_PWM),
					       10, 0);
		if (ret)
			return ret;

		clk_disable_unprepare(atmel->cur_clk);
		atmel->cur_clk = NULL;
	}

	return 0;
}

static const struct pwm_ops atmel_hlcdc_pwm_ops = {
	.apply = atmel_hlcdc_pwm_apply,
};

static const struct atmel_hlcdc_pwm_errata atmel_hlcdc_pwm_at91sam9x5_errata = {
	.slow_clk_erratum = true,
};

static const struct atmel_hlcdc_pwm_errata atmel_hlcdc_pwm_sama5d3_errata = {
	.div1_clk_erratum = true,
};

static int atmel_hlcdc_pwm_suspend(struct device *dev)
{
	struct atmel_hlcdc_pwm *atmel = dev_get_drvdata(dev);

	/* Keep the periph clock enabled if the PWM is still running. */
	if (pwm_is_enabled(&atmel->chip.pwms[0]))
		clk_disable_unprepare(atmel->hlcdc->periph_clk);

	return 0;
}

static int atmel_hlcdc_pwm_resume(struct device *dev)
{
	struct atmel_hlcdc_pwm *atmel = dev_get_drvdata(dev);
	struct pwm_state state;
	int ret;

	pwm_get_state(&atmel->chip.pwms[0], &state);

	/* Re-enable the periph clock it was stopped during suspend. */
	if (!state.enabled) {
		ret = clk_prepare_enable(atmel->hlcdc->periph_clk);
		if (ret)
			return ret;
	}

	return atmel_hlcdc_pwm_apply(&atmel->chip, &atmel->chip.pwms[0],
				     &state);
}

static DEFINE_SIMPLE_DEV_PM_OPS(atmel_hlcdc_pwm_pm_ops,
				atmel_hlcdc_pwm_suspend, atmel_hlcdc_pwm_resume);

static const struct of_device_id atmel_hlcdc_dt_ids[] = {
	{
		.compatible = "atmel,at91sam9n12-hlcdc",
		/* 9n12 has same errata as 9x5 HLCDC PWM */
		.data = &atmel_hlcdc_pwm_at91sam9x5_errata,
	},
	{
		.compatible = "atmel,at91sam9x5-hlcdc",
		.data = &atmel_hlcdc_pwm_at91sam9x5_errata,
	},
	{
		.compatible = "atmel,sama5d2-hlcdc",
	},
	{
		.compatible = "atmel,sama5d3-hlcdc",
		.data = &atmel_hlcdc_pwm_sama5d3_errata,
	},
	{
		.compatible = "atmel,sama5d4-hlcdc",
		.data = &atmel_hlcdc_pwm_sama5d3_errata,
	},
	{	.compatible = "microchip,sam9x60-hlcdc", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, atmel_hlcdc_dt_ids);

static int atmel_hlcdc_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct atmel_hlcdc_pwm *atmel;
	struct atmel_hlcdc *hlcdc;
	int ret;

	hlcdc = dev_get_drvdata(dev->parent);

	atmel = devm_kzalloc(dev, sizeof(*atmel), GFP_KERNEL);
	if (!atmel)
		return -ENOMEM;

	ret = clk_prepare_enable(hlcdc->periph_clk);
	if (ret)
		return ret;

	match = of_match_node(atmel_hlcdc_dt_ids, dev->parent->of_node);
	if (match)
		atmel->errata = match->data;

	atmel->hlcdc = hlcdc;
	atmel->chip.ops = &atmel_hlcdc_pwm_ops;
	atmel->chip.dev = dev;
	atmel->chip.npwm = 1;

	ret = pwmchip_add(&atmel->chip);
	if (ret) {
		clk_disable_unprepare(hlcdc->periph_clk);
		return ret;
	}

	platform_set_drvdata(pdev, atmel);

	return 0;
}

static void atmel_hlcdc_pwm_remove(struct platform_device *pdev)
{
	struct atmel_hlcdc_pwm *atmel = platform_get_drvdata(pdev);

	pwmchip_remove(&atmel->chip);

	clk_disable_unprepare(atmel->hlcdc->periph_clk);
}

static const struct of_device_id atmel_hlcdc_pwm_dt_ids[] = {
	{ .compatible = "atmel,hlcdc-pwm" },
	{ /* sentinel */ },
};

static struct platform_driver atmel_hlcdc_pwm_driver = {
	.driver = {
		.name = "atmel-hlcdc-pwm",
		.of_match_table = atmel_hlcdc_pwm_dt_ids,
		.pm = pm_ptr(&atmel_hlcdc_pwm_pm_ops),
	},
	.probe = atmel_hlcdc_pwm_probe,
	.remove_new = atmel_hlcdc_pwm_remove,
};
module_platform_driver(atmel_hlcdc_pwm_driver);

MODULE_ALIAS("platform:atmel-hlcdc-pwm");
MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("Atmel HLCDC PWM driver");
MODULE_LICENSE("GPL v2");
