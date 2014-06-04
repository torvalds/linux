/*
 *  Freescale FlexTimer Module (FTM) PWM Driver
 *
 *  Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define FTM_SC		0x00
#define FTM_SC_CLK_MASK	0x3
#define FTM_SC_CLK_SHIFT	3
#define FTM_SC_CLK(c)	(((c) + 1) << FTM_SC_CLK_SHIFT)
#define FTM_SC_PS_MASK	0x7
#define FTM_SC_PS_SHIFT	0

#define FTM_CNT		0x04
#define FTM_MOD		0x08

#define FTM_CSC_BASE	0x0C
#define FTM_CSC_MSB	BIT(5)
#define FTM_CSC_MSA	BIT(4)
#define FTM_CSC_ELSB	BIT(3)
#define FTM_CSC_ELSA	BIT(2)
#define FTM_CSC(_channel)	(FTM_CSC_BASE + ((_channel) * 8))

#define FTM_CV_BASE	0x10
#define FTM_CV(_channel)	(FTM_CV_BASE + ((_channel) * 8))

#define FTM_CNTIN	0x4C
#define FTM_STATUS	0x50

#define FTM_MODE	0x54
#define FTM_MODE_FTMEN	BIT(0)
#define FTM_MODE_INIT	BIT(2)
#define FTM_MODE_PWMSYNC	BIT(3)

#define FTM_SYNC	0x58
#define FTM_OUTINIT	0x5C
#define FTM_OUTMASK	0x60
#define FTM_COMBINE	0x64
#define FTM_DEADTIME	0x68
#define FTM_EXTTRIG	0x6C
#define FTM_POL		0x70
#define FTM_FMS		0x74
#define FTM_FILTER	0x78
#define FTM_FLTCTRL	0x7C
#define FTM_QDCTRL	0x80
#define FTM_CONF	0x84
#define FTM_FLTPOL	0x88
#define FTM_SYNCONF	0x8C
#define FTM_INVCTRL	0x90
#define FTM_SWOCTRL	0x94
#define FTM_PWMLOAD	0x98

enum fsl_pwm_clk {
	FSL_PWM_CLK_SYS,
	FSL_PWM_CLK_FIX,
	FSL_PWM_CLK_EXT,
	FSL_PWM_CLK_CNTEN,
	FSL_PWM_CLK_MAX
};

struct fsl_pwm_chip {
	struct pwm_chip chip;

	struct mutex lock;

	unsigned int use_count;
	unsigned int cnt_select;
	unsigned int clk_ps;

	void __iomem *base;

	int period_ns;

	struct clk *clk[FSL_PWM_CLK_MAX];
};

static inline struct fsl_pwm_chip *to_fsl_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct fsl_pwm_chip, chip);
}

static int fsl_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);

	return clk_prepare_enable(fpc->clk[FSL_PWM_CLK_SYS]);
}

static void fsl_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);

	clk_disable_unprepare(fpc->clk[FSL_PWM_CLK_SYS]);
}

static int fsl_pwm_calculate_default_ps(struct fsl_pwm_chip *fpc,
					enum fsl_pwm_clk index)
{
	unsigned long sys_rate, cnt_rate;
	unsigned long long ratio;

	sys_rate = clk_get_rate(fpc->clk[FSL_PWM_CLK_SYS]);
	if (!sys_rate)
		return -EINVAL;

	cnt_rate = clk_get_rate(fpc->clk[fpc->cnt_select]);
	if (!cnt_rate)
		return -EINVAL;

	switch (index) {
	case FSL_PWM_CLK_SYS:
		fpc->clk_ps = 1;
		break;
	case FSL_PWM_CLK_FIX:
		ratio = 2 * cnt_rate - 1;
		do_div(ratio, sys_rate);
		fpc->clk_ps = ratio;
		break;
	case FSL_PWM_CLK_EXT:
		ratio = 4 * cnt_rate - 1;
		do_div(ratio, sys_rate);
		fpc->clk_ps = ratio;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned long fsl_pwm_calculate_cycles(struct fsl_pwm_chip *fpc,
					      unsigned long period_ns)
{
	unsigned long long c, c0;

	c = clk_get_rate(fpc->clk[fpc->cnt_select]);
	c = c * period_ns;
	do_div(c, 1000000000UL);

	do {
		c0 = c;
		do_div(c0, (1 << fpc->clk_ps));
		if (c0 <= 0xFFFF)
			return (unsigned long)c0;
	} while (++fpc->clk_ps < 8);

	return 0;
}

static unsigned long fsl_pwm_calculate_period_cycles(struct fsl_pwm_chip *fpc,
						     unsigned long period_ns,
						     enum fsl_pwm_clk index)
{
	int ret;

	ret = fsl_pwm_calculate_default_ps(fpc, index);
	if (ret) {
		dev_err(fpc->chip.dev,
			"failed to calculate default prescaler: %d\n",
			ret);
		return 0;
	}

	return fsl_pwm_calculate_cycles(fpc, period_ns);
}

static unsigned long fsl_pwm_calculate_period(struct fsl_pwm_chip *fpc,
					      unsigned long period_ns)
{
	enum fsl_pwm_clk m0, m1;
	unsigned long fix_rate, ext_rate, cycles;

	cycles = fsl_pwm_calculate_period_cycles(fpc, period_ns,
			FSL_PWM_CLK_SYS);
	if (cycles) {
		fpc->cnt_select = FSL_PWM_CLK_SYS;
		return cycles;
	}

	fix_rate = clk_get_rate(fpc->clk[FSL_PWM_CLK_FIX]);
	ext_rate = clk_get_rate(fpc->clk[FSL_PWM_CLK_EXT]);

	if (fix_rate > ext_rate) {
		m0 = FSL_PWM_CLK_FIX;
		m1 = FSL_PWM_CLK_EXT;
	} else {
		m0 = FSL_PWM_CLK_EXT;
		m1 = FSL_PWM_CLK_FIX;
	}

	cycles = fsl_pwm_calculate_period_cycles(fpc, period_ns, m0);
	if (cycles) {
		fpc->cnt_select = m0;
		return cycles;
	}

	fpc->cnt_select = m1;

	return fsl_pwm_calculate_period_cycles(fpc, period_ns, m1);
}

static unsigned long fsl_pwm_calculate_duty(struct fsl_pwm_chip *fpc,
					    unsigned long period_ns,
					    unsigned long duty_ns)
{
	unsigned long long val, duty;

	val = readl(fpc->base + FTM_MOD);
	duty = duty_ns * (val + 1);
	do_div(duty, period_ns);

	return (unsigned long)duty;
}

static int fsl_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	u32 val, period, duty;

	mutex_lock(&fpc->lock);

	/*
	 * The Freescale FTM controller supports only a single period for
	 * all PWM channels, therefore incompatible changes need to be
	 * refused.
	 */
	if (fpc->period_ns && fpc->period_ns != period_ns) {
		dev_err(fpc->chip.dev,
			"conflicting period requested for PWM %u\n",
			pwm->hwpwm);
		mutex_unlock(&fpc->lock);
		return -EBUSY;
	}

	if (!fpc->period_ns && duty_ns) {
		period = fsl_pwm_calculate_period(fpc, period_ns);
		if (!period) {
			dev_err(fpc->chip.dev, "failed to calculate period\n");
			mutex_unlock(&fpc->lock);
			return -EINVAL;
		}

		val = readl(fpc->base + FTM_SC);
		val &= ~(FTM_SC_PS_MASK << FTM_SC_PS_SHIFT);
		val |= fpc->clk_ps;
		writel(val, fpc->base + FTM_SC);
		writel(period - 1, fpc->base + FTM_MOD);

		fpc->period_ns = period_ns;
	}

	mutex_unlock(&fpc->lock);

	duty = fsl_pwm_calculate_duty(fpc, period_ns, duty_ns);

	writel(FTM_CSC_MSB | FTM_CSC_ELSB, fpc->base + FTM_CSC(pwm->hwpwm));
	writel(duty, fpc->base + FTM_CV(pwm->hwpwm));

	return 0;
}

static int fsl_pwm_set_polarity(struct pwm_chip *chip,
				struct pwm_device *pwm,
				enum pwm_polarity polarity)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	u32 val;

	val = readl(fpc->base + FTM_POL);

	if (polarity == PWM_POLARITY_INVERSED)
		val |= BIT(pwm->hwpwm);
	else
		val &= ~BIT(pwm->hwpwm);

	writel(val, fpc->base + FTM_POL);

	return 0;
}

static int fsl_counter_clock_enable(struct fsl_pwm_chip *fpc)
{
	u32 val;
	int ret;

	if (fpc->use_count != 0)
		return 0;

	/* select counter clock source */
	val = readl(fpc->base + FTM_SC);
	val &= ~(FTM_SC_CLK_MASK << FTM_SC_CLK_SHIFT);
	val |= FTM_SC_CLK(fpc->cnt_select);
	writel(val, fpc->base + FTM_SC);

	ret = clk_prepare_enable(fpc->clk[fpc->cnt_select]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(fpc->clk[FSL_PWM_CLK_CNTEN]);
	if (ret) {
		clk_disable_unprepare(fpc->clk[fpc->cnt_select]);
		return ret;
	}

	fpc->use_count++;

	return 0;
}

static int fsl_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	u32 val;
	int ret;

	mutex_lock(&fpc->lock);
	val = readl(fpc->base + FTM_OUTMASK);
	val &= ~BIT(pwm->hwpwm);
	writel(val, fpc->base + FTM_OUTMASK);

	ret = fsl_counter_clock_enable(fpc);
	mutex_unlock(&fpc->lock);

	return ret;
}

static void fsl_counter_clock_disable(struct fsl_pwm_chip *fpc)
{
	u32 val;

	/*
	 * already disabled, do nothing
	 */
	if (fpc->use_count == 0)
		return;

	/* there are still users, so can't disable yet */
	if (--fpc->use_count > 0)
		return;

	/* no users left, disable PWM counter clock */
	val = readl(fpc->base + FTM_SC);
	val &= ~(FTM_SC_CLK_MASK << FTM_SC_CLK_SHIFT);
	writel(val, fpc->base + FTM_SC);

	clk_disable_unprepare(fpc->clk[FSL_PWM_CLK_CNTEN]);
	clk_disable_unprepare(fpc->clk[fpc->cnt_select]);
}

static void fsl_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct fsl_pwm_chip *fpc = to_fsl_chip(chip);
	u32 val;

	mutex_lock(&fpc->lock);
	val = readl(fpc->base + FTM_OUTMASK);
	val |= BIT(pwm->hwpwm);
	writel(val, fpc->base + FTM_OUTMASK);

	fsl_counter_clock_disable(fpc);

	val = readl(fpc->base + FTM_OUTMASK);

	if ((val & 0xFF) == 0xFF)
		fpc->period_ns = 0;

	mutex_unlock(&fpc->lock);
}

static const struct pwm_ops fsl_pwm_ops = {
	.request = fsl_pwm_request,
	.free = fsl_pwm_free,
	.config = fsl_pwm_config,
	.set_polarity = fsl_pwm_set_polarity,
	.enable = fsl_pwm_enable,
	.disable = fsl_pwm_disable,
	.owner = THIS_MODULE,
};

static int fsl_pwm_init(struct fsl_pwm_chip *fpc)
{
	int ret;

	ret = clk_prepare_enable(fpc->clk[FSL_PWM_CLK_SYS]);
	if (ret)
		return ret;

	writel(0x00, fpc->base + FTM_CNTIN);
	writel(0x00, fpc->base + FTM_OUTINIT);
	writel(0xFF, fpc->base + FTM_OUTMASK);

	clk_disable_unprepare(fpc->clk[FSL_PWM_CLK_SYS]);

	return 0;
}

static int fsl_pwm_probe(struct platform_device *pdev)
{
	struct fsl_pwm_chip *fpc;
	struct resource *res;
	int ret;

	fpc = devm_kzalloc(&pdev->dev, sizeof(*fpc), GFP_KERNEL);
	if (!fpc)
		return -ENOMEM;

	mutex_init(&fpc->lock);

	fpc->chip.dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fpc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fpc->base))
		return PTR_ERR(fpc->base);

	fpc->clk[FSL_PWM_CLK_SYS] = devm_clk_get(&pdev->dev, "ftm_sys");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_SYS])) {
		dev_err(&pdev->dev, "failed to get \"ftm_sys\" clock\n");
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_SYS]);
	}

	fpc->clk[FSL_PWM_CLK_FIX] = devm_clk_get(fpc->chip.dev, "ftm_fix");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_FIX]))
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_FIX]);

	fpc->clk[FSL_PWM_CLK_EXT] = devm_clk_get(fpc->chip.dev, "ftm_ext");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_EXT]))
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_EXT]);

	fpc->clk[FSL_PWM_CLK_CNTEN] =
				devm_clk_get(fpc->chip.dev, "ftm_cnt_clk_en");
	if (IS_ERR(fpc->clk[FSL_PWM_CLK_CNTEN]))
		return PTR_ERR(fpc->clk[FSL_PWM_CLK_CNTEN]);

	fpc->chip.ops = &fsl_pwm_ops;
	fpc->chip.of_xlate = of_pwm_xlate_with_flags;
	fpc->chip.of_pwm_n_cells = 3;
	fpc->chip.base = -1;
	fpc->chip.npwm = 8;

	ret = pwmchip_add(&fpc->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, fpc);

	return fsl_pwm_init(fpc);
}

static int fsl_pwm_remove(struct platform_device *pdev)
{
	struct fsl_pwm_chip *fpc = platform_get_drvdata(pdev);

	return pwmchip_remove(&fpc->chip);
}

static const struct of_device_id fsl_pwm_dt_ids[] = {
	{ .compatible = "fsl,vf610-ftm-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_pwm_dt_ids);

static struct platform_driver fsl_pwm_driver = {
	.driver = {
		.name = "fsl-ftm-pwm",
		.of_match_table = fsl_pwm_dt_ids,
	},
	.probe = fsl_pwm_probe,
	.remove = fsl_pwm_remove,
};
module_platform_driver(fsl_pwm_driver);

MODULE_DESCRIPTION("Freescale FlexTimer Module PWM Driver");
MODULE_AUTHOR("Xiubo Li <Li.Xiubo@freescale.com>");
MODULE_ALIAS("platform:fsl-ftm-pwm");
MODULE_LICENSE("GPL");
