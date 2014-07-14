/*
 * PWM device driver for ST SoCs.
 * Author: Ajit Pal Singh <ajitpal.singh@st.com>
 *
 * Copyright (C) 2013-2014 STMicroelectronics (R&D) Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bsearch.h>
#include <linux/clk.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>

#define STI_DS_REG(ch)	(4 * (ch))	/* Channel's Duty Cycle register */
#define STI_PWMCR	0x50		/* Control/Config register */
#define STI_INTEN	0x54		/* Interrupt Enable/Disable register */
#define PWM_PRESCALE_LOW_MASK		0x0f
#define PWM_PRESCALE_HIGH_MASK		0xf0

/* Regfield IDs */
enum {
	PWMCLK_PRESCALE_LOW,
	PWMCLK_PRESCALE_HIGH,
	PWM_EN,
	PWM_INT_EN,

	/* Keep last */
	MAX_REGFIELDS
};

struct sti_pwm_compat_data {
	const struct reg_field *reg_fields;
	unsigned int num_chan;
	unsigned int max_pwm_cnt;
	unsigned int max_prescale;
};

struct sti_pwm_chip {
	struct device *dev;
	struct clk *clk;
	unsigned long clk_rate;
	struct regmap *regmap;
	struct sti_pwm_compat_data *cdata;
	struct regmap_field *prescale_low;
	struct regmap_field *prescale_high;
	struct regmap_field *pwm_en;
	struct regmap_field *pwm_int_en;
	unsigned long *pwm_periods;
	struct pwm_chip chip;
	struct pwm_device *cur;
	unsigned int en_count;
	struct mutex sti_pwm_lock; /* To sync between enable/disable calls */
	void __iomem *mmio;
};

static const struct reg_field sti_pwm_regfields[MAX_REGFIELDS] = {
	[PWMCLK_PRESCALE_LOW]	= REG_FIELD(STI_PWMCR, 0, 3),
	[PWMCLK_PRESCALE_HIGH]	= REG_FIELD(STI_PWMCR, 11, 14),
	[PWM_EN]		= REG_FIELD(STI_PWMCR, 9, 9),
	[PWM_INT_EN]		= REG_FIELD(STI_INTEN, 0, 0),
};

static inline struct sti_pwm_chip *to_sti_pwmchip(struct pwm_chip *chip)
{
	return container_of(chip, struct sti_pwm_chip, chip);
}

/*
 * Calculate the period values supported by the PWM for the
 * current clock rate.
 */
static void sti_pwm_calc_periods(struct sti_pwm_chip *pc)
{
	struct sti_pwm_compat_data *cdata = pc->cdata;
	struct device *dev = pc->dev;
	unsigned long val;
	int i;

	/*
	 * period_ns = (10^9 * (prescaler + 1) * (MAX_PWM_COUNT + 1)) / CLK_RATE
	 */
	val = NSEC_PER_SEC / pc->clk_rate;
	val *= cdata->max_pwm_cnt + 1;

	dev_dbg(dev, "possible periods for clkrate[HZ]:%lu\n", pc->clk_rate);

	for (i = 0; i <= cdata->max_prescale; i++) {
		pc->pwm_periods[i] = val * (i + 1);
		dev_dbg(dev, "prescale:%d, period[ns]:%lu\n",
			i, pc->pwm_periods[i]);
	}
}

/* Calculate the number of PWM devices configured with a period. */
static unsigned int sti_pwm_count_configured(struct pwm_chip *chip)
{
	struct pwm_device *pwm;
	unsigned int ncfg = 0;
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		pwm = &chip->pwms[i];
		if (test_bit(PWMF_REQUESTED, &pwm->flags)) {
			if (pwm_get_period(pwm))
				ncfg++;
		}
	}

	return ncfg;
}

static int sti_pwm_cmp_periods(const void *key, const void *elt)
{
	unsigned long i = *(unsigned long *)key;
	unsigned long j = *(unsigned long *)elt;

	if (i < j)
		return -1;
	else
		return i == j ? 0 : 1;
}

/*
 * For STiH4xx PWM IP, the PWM period is fixed to 256 local clock cycles.
 * The only way to change the period (apart from changing the PWM input clock)
 * is to change the PWM clock prescaler.
 * The prescaler is of 8 bits, so 256 prescaler values and hence
 * 256 possible period values are supported (for a particular clock rate).
 * The requested period will be applied only if it matches one of these
 * 256 values.
 */
static int sti_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			 int duty_ns, int period_ns)
{
	struct sti_pwm_chip *pc = to_sti_pwmchip(chip);
	struct sti_pwm_compat_data *cdata = pc->cdata;
	struct pwm_device *cur = pc->cur;
	struct device *dev = pc->dev;
	unsigned int prescale = 0, pwmvalx;
	unsigned long *found;
	int ret;
	unsigned int ncfg;
	bool period_same = false;

	ncfg = sti_pwm_count_configured(chip);
	if (ncfg)
		period_same = (period_ns == pwm_get_period(cur));

	/* Allow configuration changes if one of the
	 * following conditions satisfy.
	 * 1. No channels have been configured.
	 * 2. Only one channel has been configured and the new request
	 *    is for the same channel.
	 * 3. Only one channel has been configured and the new request is
	 *    for a new channel and period of the new channel is same as
	 *    the current configured period.
	 * 4. More than one channels are configured and period of the new
	 *    requestis the same as the current period.
	 */
	if (!ncfg ||
	    ((ncfg == 1) && (pwm->hwpwm == cur->hwpwm)) ||
	    ((ncfg == 1) && (pwm->hwpwm != cur->hwpwm) && period_same) ||
	    ((ncfg > 1) && period_same)) {
		/* Enable clock before writing to PWM registers. */
		ret = clk_enable(pc->clk);
		if (ret)
			return ret;

		if (!period_same) {
			/*
			 * Search for matching period value.
			 * The corresponding index is our prescale value.
			 */
			found = bsearch(&period_ns, &pc->pwm_periods[0],
					cdata->max_prescale + 1,
					sizeof(unsigned long),
					sti_pwm_cmp_periods);
			if (!found) {
				dev_err(dev,
					"failed to find matching period\n");
				ret = -EINVAL;
				goto clk_dis;
			}
			prescale = found - &pc->pwm_periods[0];

			ret =
			regmap_field_write(pc->prescale_low,
					   prescale & PWM_PRESCALE_LOW_MASK);
			if (ret)
				goto clk_dis;

			ret =
			regmap_field_write(pc->prescale_high,
				(prescale & PWM_PRESCALE_HIGH_MASK) >> 4);
			if (ret)
				goto clk_dis;
		}

		/*
		 * When PWMVal == 0, PWM pulse = 1 local clock cycle.
		 * When PWMVal == max_pwm_count,
		 * PWM pulse = (max_pwm_count + 1) local cycles,
		 * that is continuous pulse: signal never goes low.
		 */
		pwmvalx = cdata->max_pwm_cnt * duty_ns / period_ns;

		ret = regmap_write(pc->regmap, STI_DS_REG(pwm->hwpwm), pwmvalx);
		if (ret)
			goto clk_dis;

		ret = regmap_field_write(pc->pwm_int_en, 0);

		pc->cur = pwm;

		dev_dbg(dev, "prescale:%u, period:%i, duty:%i, pwmvalx:%u\n",
			prescale, period_ns, duty_ns, pwmvalx);
	} else {
		return -EINVAL;
	}

clk_dis:
	clk_disable(pc->clk);
	return ret;
}

static int sti_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sti_pwm_chip *pc = to_sti_pwmchip(chip);
	struct device *dev = pc->dev;
	int ret = 0;

	/*
	 * Since we have a common enable for all PWM channels,
	 * do not enable if already enabled.
	 */
	mutex_lock(&pc->sti_pwm_lock);
	if (!pc->en_count) {
		ret = clk_enable(pc->clk);
		if (ret)
			goto out;

		ret = regmap_field_write(pc->pwm_en, 1);
		if (ret) {
			dev_err(dev, "failed to enable PWM device:%d\n",
				pwm->hwpwm);
			goto out;
		}
	}
	pc->en_count++;
out:
	mutex_unlock(&pc->sti_pwm_lock);
	return ret;
}

static void sti_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct sti_pwm_chip *pc = to_sti_pwmchip(chip);

	mutex_lock(&pc->sti_pwm_lock);
	if (--pc->en_count) {
		mutex_unlock(&pc->sti_pwm_lock);
		return;
	}
	regmap_field_write(pc->pwm_en, 0);

	clk_disable(pc->clk);
	mutex_unlock(&pc->sti_pwm_lock);
}

static const struct pwm_ops sti_pwm_ops = {
	.config = sti_pwm_config,
	.enable = sti_pwm_enable,
	.disable = sti_pwm_disable,
	.owner = THIS_MODULE,
};

static int sti_pwm_probe_dt(struct sti_pwm_chip *pc)
{
	struct device *dev = pc->dev;
	const struct reg_field *reg_fields;
	struct device_node *np = dev->of_node;
	struct sti_pwm_compat_data *cdata = pc->cdata;
	u32 num_chan;

	of_property_read_u32(np, "st,pwm-num-chan", &num_chan);
	if (num_chan)
		cdata->num_chan = num_chan;

	reg_fields = cdata->reg_fields;

	pc->prescale_low = devm_regmap_field_alloc(dev, pc->regmap,
					reg_fields[PWMCLK_PRESCALE_LOW]);
	if (IS_ERR(pc->prescale_low))
		return PTR_ERR(pc->prescale_low);

	pc->prescale_high = devm_regmap_field_alloc(dev, pc->regmap,
					reg_fields[PWMCLK_PRESCALE_HIGH]);
	if (IS_ERR(pc->prescale_high))
		return PTR_ERR(pc->prescale_high);

	pc->pwm_en = devm_regmap_field_alloc(dev, pc->regmap,
					     reg_fields[PWM_EN]);
	if (IS_ERR(pc->pwm_en))
		return PTR_ERR(pc->pwm_en);

	pc->pwm_int_en = devm_regmap_field_alloc(dev, pc->regmap,
						 reg_fields[PWM_INT_EN]);
	if (IS_ERR(pc->pwm_int_en))
		return PTR_ERR(pc->pwm_int_en);

	return 0;
}

static const struct regmap_config sti_pwm_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int sti_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sti_pwm_compat_data *cdata;
	struct sti_pwm_chip *pc;
	struct resource *res;
	int ret;

	pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;

	cdata = devm_kzalloc(dev, sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	pc->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(pc->mmio))
		return PTR_ERR(pc->mmio);

	pc->regmap = devm_regmap_init_mmio(dev, pc->mmio,
					   &sti_pwm_regmap_config);
	if (IS_ERR(pc->regmap))
		return PTR_ERR(pc->regmap);

	/*
	 * Setup PWM data with default values: some values could be replaced
	 * with specific ones provided from Device Tree.
	 */
	cdata->reg_fields   = &sti_pwm_regfields[0];
	cdata->max_prescale = 0xff;
	cdata->max_pwm_cnt  = 255;
	cdata->num_chan     = 1;

	pc->cdata = cdata;
	pc->dev = dev;
	pc->en_count = 0;
	mutex_init(&pc->sti_pwm_lock);

	ret = sti_pwm_probe_dt(pc);
	if (ret)
		return ret;

	pc->pwm_periods = devm_kzalloc(dev,
			sizeof(unsigned long) * (pc->cdata->max_prescale + 1),
			GFP_KERNEL);
	if (!pc->pwm_periods)
		return -ENOMEM;

	pc->clk = of_clk_get_by_name(dev->of_node, "pwm");
	if (IS_ERR(pc->clk)) {
		dev_err(dev, "failed to get PWM clock\n");
		return PTR_ERR(pc->clk);
	}

	pc->clk_rate = clk_get_rate(pc->clk);
	if (!pc->clk_rate) {
		dev_err(dev, "failed to get clock rate\n");
		return -EINVAL;
	}

	ret = clk_prepare(pc->clk);
	if (ret) {
		dev_err(dev, "failed to prepare clock\n");
		return ret;
	}

	sti_pwm_calc_periods(pc);

	pc->chip.dev = dev;
	pc->chip.ops = &sti_pwm_ops;
	pc->chip.base = -1;
	pc->chip.npwm = pc->cdata->num_chan;
	pc->chip.can_sleep = true;

	ret = pwmchip_add(&pc->chip);
	if (ret < 0) {
		clk_unprepare(pc->clk);
		return ret;
	}

	platform_set_drvdata(pdev, pc);

	return 0;
}

static int sti_pwm_remove(struct platform_device *pdev)
{
	struct sti_pwm_chip *pc = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < pc->cdata->num_chan; i++)
		pwm_disable(&pc->chip.pwms[i]);

	clk_unprepare(pc->clk);

	return pwmchip_remove(&pc->chip);
}

static const struct of_device_id sti_pwm_of_match[] = {
	{ .compatible = "st,sti-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sti_pwm_of_match);

static struct platform_driver sti_pwm_driver = {
	.driver = {
		.name = "sti-pwm",
		.of_match_table = sti_pwm_of_match,
	},
	.probe = sti_pwm_probe,
	.remove = sti_pwm_remove,
};
module_platform_driver(sti_pwm_driver);

MODULE_AUTHOR("Ajit Pal Singh <ajitpal.singh@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ST PWM driver");
MODULE_LICENSE("GPL");
