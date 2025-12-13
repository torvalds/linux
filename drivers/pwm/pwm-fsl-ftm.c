// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Freescale FlexTimer Module (FTM) PWM Driver
 *
 *  Copyright 2012-2013 Freescale Semiconductor, Inc.
 *  Copyright 2020-2025 NXP
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/fsl/ftm.h>

#define FTM_SC_CLK(c)	(((c) + 1) << FTM_SC_CLK_MASK_SHIFT)

enum fsl_pwm_clk {
	FSL_PWM_CLK_SYS,
	FSL_PWM_CLK_FIX,
	FSL_PWM_CLK_EXT,
	FSL_PWM_CLK_CNTEN,
	FSL_PWM_CLK_MAX
};

struct fsl_ftm_soc {
	bool has_enable_bits;
	bool has_flt_reg;
	unsigned int npwm;
};

struct fsl_pwm_periodcfg {
	enum fsl_pwm_clk clk_select;
	unsigned int clk_ps;
	unsigned int mod_period;
};

struct fsl_pwm_chip {
	struct regmap *regmap;

	/* This value is valid iff a pwm is running */
	struct fsl_pwm_periodcfg period;

	struct clk *ipg_clk;
	struct clk *clk[FSL_PWM_CLK_MAX];

	const struct fsl_ftm_soc *soc;
};

static inline struct fsl_pwm_chip *to_fsl_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static void ftm_clear_write_protection(struct fsl_pwm_chip *fpc)
{
	u32 val;

	regmap_read(fpc->regmap, FTM_FMS, &val);
	if (val & FTM_FMS_WPEN)
		regmap_set_bits(fpc->regmap, FTM_MODE, FTM_MODE_WPDIS);
}

static void ftm_set_write_protection(struct fsl_pwm_chip *fpc)
{
	regmap_set_bits(fpc->regmap, FTM_FMS, FTM_FMS_WPEN);
}

static bool fsl_pwm_periodcfg_are_equal(const struct fsl_pwm_periodcfg *a,
					const struct fsl_pwm_periodcfg *b)
{
	if (a->clk_select != b->clk_select)
		return false;
	if (a->clk_ps != b->clk_ps)
		return false;
	if (a->mod_period != b->mod_period)
		return false;
	return true;
}

static int fsl_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int ret;
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);

	ret = clk_prepare_enable(fpc->ipg_clk);
	if (!ret && fpc->soc->has_enable_bits)
		regmap_set_bits(fpc->regmap, FTM_SC, BIT(pwm->hwpwm + 16));

	return ret;
}

static void fsl_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);

	if (fpc->soc->has_enable_bits)
		regmap_clear_bits(fpc->regmap, FTM_SC, BIT(pwm->hwpwm + 16));

	clk_disable_unprepare(fpc->ipg_clk);
}

static unsigned int fsl_pwm_ticks_to_ns(struct fsl_pwm_chip *fpc,
					  unsigned int ticks)
{
	unsigned long rate;
	unsigned long long exval;

	rate = clk_get_rate(fpc->clk[fpc->period.clk_select]);
	if (rate >> fpc->period.clk_ps == 0)
		return 0;

	exval = ticks;
	exval *= 1000000000UL;
	do_div(exval, rate >> fpc->period.clk_ps);
	return exval;
}

static bool fsl_pwm_calculate_period_clk(struct fsl_pwm_chip *fpc,
					 unsigned int period_ns,
					 enum fsl_pwm_clk index,
					 struct fsl_pwm_periodcfg *periodcfg
					 )
{
	unsigned long long c;
	unsigned int ps;

	c = clk_get_rate(fpc->clk[index]);
	c = c * period_ns;
	do_div(c, 1000000000UL);

	if (c == 0)
		return false;

	for (ps = 0; ps < 8 ; ++ps, c >>= 1) {
		if (c <= 0x10000) {
			periodcfg->clk_select = index;
			periodcfg->clk_ps = ps;
			periodcfg->mod_period = c - 1;
			return true;
		}
	}
	return false;
}

static bool fsl_pwm_calculate_period(struct fsl_pwm_chip *fpc,
				     unsigned int period_ns,
				     struct fsl_pwm_periodcfg *periodcfg)
{
	enum fsl_pwm_clk m0, m1;
	unsigned long fix_rate, ext_rate;
	bool ret;

	ret = fsl_pwm_calculate_period_clk(fpc, period_ns, FSL_PWM_CLK_SYS,
					   periodcfg);
	if (ret)
		return true;

	fix_rate = clk_get_rate(fpc->clk[FSL_PWM_CLK_FIX]);
	ext_rate = clk_get_rate(fpc->clk[FSL_PWM_CLK_EXT]);

	if (fix_rate > ext_rate) {
		m0 = FSL_PWM_CLK_FIX;
		m1 = FSL_PWM_CLK_EXT;
	} else {
		m0 = FSL_PWM_CLK_EXT;
		m1 = FSL_PWM_CLK_FIX;
	}

	ret = fsl_pwm_calculate_period_clk(fpc, period_ns, m0, periodcfg);
	if (ret)
		return true;

	return fsl_pwm_calculate_period_clk(fpc, period_ns, m1, periodcfg);
}

static unsigned int fsl_pwm_calculate_duty(struct fsl_pwm_chip *fpc,
					   unsigned int duty_ns)
{
	unsigned long long duty;

	unsigned int period = fpc->period.mod_period + 1;
	unsigned int period_ns = fsl_pwm_ticks_to_ns(fpc, period);

	if (!period_ns)
		return 0;

	duty = (unsigned long long)duty_ns * period;
	do_div(duty, period_ns);

	return (unsigned int)duty;
}

static bool fsl_pwm_is_any_pwm_enabled(struct fsl_pwm_chip *fpc,
				       struct pwm_device *pwm)
{
	u32 val;

	regmap_read(fpc->regmap, FTM_OUTMASK, &val);
	if (~val & 0xFF)
		return true;
	else
		return false;
}

static bool fsl_pwm_is_other_pwm_enabled(struct fsl_pwm_chip *fpc,
					 struct pwm_device *pwm)
{
	u32 val;

	regmap_read(fpc->regmap, FTM_OUTMASK, &val);
	if (~(val | BIT(pwm->hwpwm)) & 0xFF)
		return true;
	else
		return false;
}

static int fsl_pwm_apply_config(struct pwm_chip *chip,
				struct pwm_device *pwm,
				const struct pwm_state *newstate)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	unsigned int duty;
	u32 reg_polarity;

	struct fsl_pwm_periodcfg periodcfg;
	bool do_write_period = false;

	if (!fsl_pwm_calculate_period(fpc, newstate->period, &periodcfg)) {
		dev_err(pwmchip_parent(chip), "failed to calculate new period\n");
		return -EINVAL;
	}

	if (!fsl_pwm_is_any_pwm_enabled(fpc, pwm))
		do_write_period = true;
	/*
	 * The Freescale FTM controller supports only a single period for
	 * all PWM channels, therefore verify if the newly computed period
	 * is different than the current period being used. In such case
	 * we allow to change the period only if no other pwm is running.
	 */
	else if (!fsl_pwm_periodcfg_are_equal(&fpc->period, &periodcfg)) {
		if (fsl_pwm_is_other_pwm_enabled(fpc, pwm)) {
			dev_err(pwmchip_parent(chip),
				"Cannot change period for PWM %u, disable other PWMs first\n",
				pwm->hwpwm);
			return -EBUSY;
		}
		if (fpc->period.clk_select != periodcfg.clk_select) {
			int ret;
			enum fsl_pwm_clk oldclk = fpc->period.clk_select;
			enum fsl_pwm_clk newclk = periodcfg.clk_select;

			ret = clk_prepare_enable(fpc->clk[newclk]);
			if (ret)
				return ret;
			clk_disable_unprepare(fpc->clk[oldclk]);
		}
		do_write_period = true;
	}

	ftm_clear_write_protection(fpc);

	if (do_write_period) {
		regmap_update_bits(fpc->regmap, FTM_SC, FTM_SC_CLK_MASK,
				   FTM_SC_CLK(periodcfg.clk_select));
		regmap_update_bits(fpc->regmap, FTM_SC, FTM_SC_PS_MASK,
				   periodcfg.clk_ps);
		regmap_write(fpc->regmap, FTM_MOD, periodcfg.mod_period);

		fpc->period = periodcfg;
	}

	duty = fsl_pwm_calculate_duty(fpc, newstate->duty_cycle);

	regmap_write(fpc->regmap, FTM_CSC(pwm->hwpwm),
		     FTM_CSC_MSB | FTM_CSC_ELSB);
	regmap_write(fpc->regmap, FTM_CV(pwm->hwpwm), duty);

	reg_polarity = 0;
	if (newstate->polarity == PWM_POLARITY_INVERSED)
		reg_polarity = BIT(pwm->hwpwm);

	regmap_update_bits(fpc->regmap, FTM_POL, BIT(pwm->hwpwm), reg_polarity);

	ftm_set_write_protection(fpc);

	return 0;
}

static int fsl_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *newstate)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	struct pwm_state *oldstate = &pwm->state;
	int ret;

	/*
	 * oldstate to newstate : action
	 *
	 * disabled to disabled : ignore
	 * enabled to disabled : disable
	 * enabled to enabled : update settings
	 * disabled to enabled : update settings + enable
	 */

	if (!newstate->enabled) {
		if (oldstate->enabled) {
			regmap_set_bits(fpc->regmap, FTM_OUTMASK,
					BIT(pwm->hwpwm));
			clk_disable_unprepare(fpc->clk[FSL_PWM_CLK_CNTEN]);
			clk_disable_unprepare(fpc->clk[fpc->period.clk_select]);
		}

		return 0;
	}

	ret = fsl_pwm_apply_config(chip, pwm, newstate);
	if (ret)
		return ret;

	/* check if need to enable */
	if (!oldstate->enabled) {
		ret = clk_prepare_enable(fpc->clk[fpc->period.clk_select]);
		if (ret)
			return ret;

		ret = clk_prepare_enable(fpc->clk[FSL_PWM_CLK_CNTEN]);
		if (ret) {
			clk_disable_unprepare(fpc->clk[fpc->period.clk_select]);
			return ret;
		}

		regmap_clear_bits(fpc->regmap, FTM_OUTMASK, BIT(pwm->hwpwm));
	}

	return ret;
}

static const struct pwm_ops fsl_pwm_ops = {
	.request = fsl_pwm_request,
	.free = fsl_pwm_free,
	.apply = fsl_pwm_apply,
};

static int fsl_pwm_init(struct fsl_pwm_chip *fpc)
{
	int ret;

	ret = clk_prepare_enable(fpc->ipg_clk);
	if (ret)
		return ret;

	regmap_write(fpc->regmap, FTM_CNTIN, 0x00);
	regmap_write(fpc->regmap, FTM_OUTINIT, 0x00);
	regmap_write(fpc->regmap, FTM_OUTMASK, 0xFF);

	clk_disable_unprepare(fpc->ipg_clk);

	return 0;
}

static bool fsl_pwm_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case FTM_FMS:
	case FTM_MODE:
	case FTM_CNT:
		return true;
	}
	return false;
}

static bool fsl_pwm_is_reg(struct device *dev, unsigned int reg)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);

	if (reg >= FTM_CSC(fpc->soc->npwm) && reg < FTM_CNTIN)
		return false;

	if ((reg == FTM_FLTCTRL || reg == FTM_FLTPOL) && !fpc->soc->has_flt_reg)
		return false;

	return true;
}

static const struct regmap_config fsl_pwm_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.max_register = FTM_PWMLOAD,
	.volatile_reg = fsl_pwm_volatile_reg,
	.cache_type = REGCACHE_FLAT,
	.writeable_reg = fsl_pwm_is_reg,
	.readable_reg = fsl_pwm_is_reg,
};

static int fsl_pwm_probe(struct platform_device *pdev)
{
	const struct fsl_ftm_soc *soc = of_device_get_match_data(&pdev->dev);
	struct pwm_chip *chip;
	struct fsl_pwm_chip *fpc;
	void __iomem *base;
	int ret;

	chip = devm_pwmchip_alloc(&pdev->dev, soc->npwm, sizeof(*fpc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	fpc = to_fsl_chip(chip);

	fpc->soc = soc;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fpc->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "ftm_sys", base,
						&fsl_pwm_regmap_config);
	if (IS_ERR(fpc->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(fpc->regmap);
	}

	fpc->clk[FSL_PWM_CLK_SYS] = devm_clk_get(&pdev->dev, "ftm_sys");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_SYS])) {
		dev_err(&pdev->dev, "failed to get \"ftm_sys\" clock\n");
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_SYS]);
	}

	fpc->clk[FSL_PWM_CLK_FIX] = devm_clk_get(&pdev->dev, "ftm_fix");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_FIX]))
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_FIX]);

	fpc->clk[FSL_PWM_CLK_EXT] = devm_clk_get(&pdev->dev, "ftm_ext");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_EXT]))
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_EXT]);

	fpc->clk[FSL_PWM_CLK_CNTEN] =
				devm_clk_get(&pdev->dev, "ftm_cnt_clk_en");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_CNTEN]))
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_CNTEN]);

	/*
	 * ipg_clk is the interface clock for the IP. If not provided, use the
	 * ftm_sys clock as the default.
	 */
	fpc->ipg_clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(fpc->ipg_clk))
		fpc->ipg_clk = fpc->clk[FSL_PWM_CLK_SYS];

	chip->ops = &fsl_pwm_ops;

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, chip);

	return fsl_pwm_init(fpc);
}

#ifdef CONFIG_PM_SLEEP
static int fsl_pwm_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	int i;

	regcache_cache_only(fpc->regmap, true);
	regcache_mark_dirty(fpc->regmap);

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		if (!test_bit(PWMF_REQUESTED, &pwm->flags))
			continue;

		clk_disable_unprepare(fpc->ipg_clk);

		if (!pwm_is_enabled(pwm))
			continue;

		clk_disable_unprepare(fpc->clk[FSL_PWM_CLK_CNTEN]);
		clk_disable_unprepare(fpc->clk[fpc->period.clk_select]);
	}

	return 0;
}

static int fsl_pwm_resume(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	int i;

	for (i = 0; i < chip->npwm; i++) {
		struct pwm_device *pwm = &chip->pwms[i];

		if (!test_bit(PWMF_REQUESTED, &pwm->flags))
			continue;

		clk_prepare_enable(fpc->ipg_clk);

		if (!pwm_is_enabled(pwm))
			continue;

		clk_prepare_enable(fpc->clk[fpc->period.clk_select]);
		clk_prepare_enable(fpc->clk[FSL_PWM_CLK_CNTEN]);
	}

	/* restore all registers from cache */
	regcache_cache_only(fpc->regmap, false);
	regcache_sync(fpc->regmap);

	return 0;
}
#endif

static const struct dev_pm_ops fsl_pwm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fsl_pwm_suspend, fsl_pwm_resume)
};

static const struct fsl_ftm_soc vf610_ftm_pwm = {
	.has_enable_bits = false,
	.has_flt_reg = true,
	.npwm = 8,
};

static const struct fsl_ftm_soc imx8qm_ftm_pwm = {
	.has_enable_bits = true,
	.has_flt_reg = true,
	.npwm = 8,
};

static const struct fsl_ftm_soc s32g2_ftm_pwm = {
	.has_enable_bits = true,
	.has_flt_reg = false,
	.npwm = 6,
};

static const struct of_device_id fsl_pwm_dt_ids[] = {
	{ .compatible = "fsl,vf610-ftm-pwm", .data = &vf610_ftm_pwm },
	{ .compatible = "fsl,imx8qm-ftm-pwm", .data = &imx8qm_ftm_pwm },
	{ .compatible = "nxp,s32g2-ftm-pwm", .data = &s32g2_ftm_pwm },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_pwm_dt_ids);

static struct platform_driver fsl_pwm_driver = {
	.driver = {
		.name = "fsl-ftm-pwm",
		.of_match_table = fsl_pwm_dt_ids,
		.pm = &fsl_pwm_pm_ops,
	},
	.probe = fsl_pwm_probe,
};
module_platform_driver(fsl_pwm_driver);

MODULE_DESCRIPTION("Freescale FlexTimer Module PWM Driver");
MODULE_AUTHOR("Xiubo Li <Li.Xiubo@freescale.com>");
MODULE_ALIAS("platform:fsl-ftm-pwm");
MODULE_LICENSE("GPL");
