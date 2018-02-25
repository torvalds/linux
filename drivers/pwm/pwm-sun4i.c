/*
 * Driver for Allwinner sun4i Pulse Width Modulation Controller
 *
 * Copyright (C) 2014 Alexandre Belloni <alexandre.belloni@free-electrons.com>
 *
 * Licensed under GPLv2.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>

#define PWM_CTRL_REG		0x0

#define PWM_CH_PRD_BASE		0x4
#define PWM_CH_PRD_OFFSET	0x4
#define PWM_CH_PRD(ch)		(PWM_CH_PRD_BASE + PWM_CH_PRD_OFFSET * (ch))

#define PWMCH_OFFSET		15
#define PWM_PRESCAL_MASK	GENMASK(3, 0)
#define PWM_PRESCAL_OFF		0
#define PWM_EN			BIT(4)
#define PWM_ACT_STATE		BIT(5)
#define PWM_CLK_GATING		BIT(6)
#define PWM_MODE		BIT(7)
#define PWM_PULSE		BIT(8)
#define PWM_BYPASS		BIT(9)

#define PWM_RDY_BASE		28
#define PWM_RDY_OFFSET		1
#define PWM_RDY(ch)		BIT(PWM_RDY_BASE + PWM_RDY_OFFSET * (ch))

#define PWM_PRD(prd)		(((prd) - 1) << 16)
#define PWM_PRD_MASK		GENMASK(15, 0)

#define PWM_DTY_MASK		GENMASK(15, 0)

#define PWM_REG_PRD(reg)	((((reg) >> 16) & PWM_PRD_MASK) + 1)
#define PWM_REG_DTY(reg)	((reg) & PWM_DTY_MASK)
#define PWM_REG_PRESCAL(reg, chan)	(((reg) >> ((chan) * PWMCH_OFFSET)) & PWM_PRESCAL_MASK)

#define BIT_CH(bit, chan)	((bit) << ((chan) * PWMCH_OFFSET))

static const u32 prescaler_table[] = {
	120,
	180,
	240,
	360,
	480,
	0,
	0,
	0,
	12000,
	24000,
	36000,
	48000,
	72000,
	0,
	0,
	0, /* Actually 1 but tested separately */
};

struct sun4i_pwm_data {
	bool has_prescaler_bypass;
	bool has_rdy;
	unsigned int npwm;
};

struct sun4i_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk;
	void __iomem *base;
	spinlock_t ctrl_lock;
	const struct sun4i_pwm_data *data;
	unsigned long next_period[2];
	bool needs_delay[2];
};

static inline struct sun4i_pwm_chip *to_sun4i_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct sun4i_pwm_chip, chip);
}

static inline u32 sun4i_pwm_readl(struct sun4i_pwm_chip *chip,
				  unsigned long offset)
{
	return readl(chip->base + offset);
}

static inline void sun4i_pwm_writel(struct sun4i_pwm_chip *chip,
				    u32 val, unsigned long offset)
{
	writel(val, chip->base + offset);
}

static void sun4i_pwm_get_state(struct pwm_chip *chip,
				struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct sun4i_pwm_chip *sun4i_pwm = to_sun4i_pwm_chip(chip);
	u64 clk_rate, tmp;
	u32 val;
	unsigned int prescaler;

	clk_rate = clk_get_rate(sun4i_pwm->clk);

	val = sun4i_pwm_readl(sun4i_pwm, PWM_CTRL_REG);

	if ((PWM_REG_PRESCAL(val, pwm->hwpwm) == PWM_PRESCAL_MASK) &&
	    sun4i_pwm->data->has_prescaler_bypass)
		prescaler = 1;
	else
		prescaler = prescaler_table[PWM_REG_PRESCAL(val, pwm->hwpwm)];

	if (prescaler == 0)
		return;

	if (val & BIT_CH(PWM_ACT_STATE, pwm->hwpwm))
		state->polarity = PWM_POLARITY_NORMAL;
	else
		state->polarity = PWM_POLARITY_INVERSED;

	if ((val & BIT_CH(PWM_CLK_GATING | PWM_EN, pwm->hwpwm)) ==
	    BIT_CH(PWM_CLK_GATING | PWM_EN, pwm->hwpwm))
		state->enabled = true;
	else
		state->enabled = false;

	val = sun4i_pwm_readl(sun4i_pwm, PWM_CH_PRD(pwm->hwpwm));

	tmp = prescaler * NSEC_PER_SEC * PWM_REG_DTY(val);
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, clk_rate);

	tmp = prescaler * NSEC_PER_SEC * PWM_REG_PRD(val);
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, clk_rate);
}

static int sun4i_pwm_calculate(struct sun4i_pwm_chip *sun4i_pwm,
			       struct pwm_state *state,
			       u32 *dty, u32 *prd, unsigned int *prsclr)
{
	u64 clk_rate, div = 0;
	unsigned int pval, prescaler = 0;

	clk_rate = clk_get_rate(sun4i_pwm->clk);

	if (sun4i_pwm->data->has_prescaler_bypass) {
		/* First, test without any prescaler when available */
		prescaler = PWM_PRESCAL_MASK;
		pval = 1;
		/*
		 * When not using any prescaler, the clock period in nanoseconds
		 * is not an integer so round it half up instead of
		 * truncating to get less surprising values.
		 */
		div = clk_rate * state->period + NSEC_PER_SEC / 2;
		do_div(div, NSEC_PER_SEC);
		if (div - 1 > PWM_PRD_MASK)
			prescaler = 0;
	}

	if (prescaler == 0) {
		/* Go up from the first divider */
		for (prescaler = 0; prescaler < PWM_PRESCAL_MASK; prescaler++) {
			if (!prescaler_table[prescaler])
				continue;
			pval = prescaler_table[prescaler];
			div = clk_rate;
			do_div(div, pval);
			div = div * state->period;
			do_div(div, NSEC_PER_SEC);
			if (div - 1 <= PWM_PRD_MASK)
				break;
		}

		if (div - 1 > PWM_PRD_MASK)
			return -EINVAL;
	}

	*prd = div;
	div *= state->duty_cycle;
	do_div(div, state->period);
	*dty = div;
	*prsclr = prescaler;

	div = (u64)pval * NSEC_PER_SEC * *prd;
	state->period = DIV_ROUND_CLOSEST_ULL(div, clk_rate);

	div = (u64)pval * NSEC_PER_SEC * *dty;
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(div, clk_rate);

	return 0;
}

static int sun4i_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   struct pwm_state *state)
{
	struct sun4i_pwm_chip *sun4i_pwm = to_sun4i_pwm_chip(chip);
	struct pwm_state cstate;
	u32 ctrl;
	int ret;
	unsigned int delay_us;
	unsigned long now;

	pwm_get_state(pwm, &cstate);

	if (!cstate.enabled) {
		ret = clk_prepare_enable(sun4i_pwm->clk);
		if (ret) {
			dev_err(chip->dev, "failed to enable PWM clock\n");
			return ret;
		}
	}

	spin_lock(&sun4i_pwm->ctrl_lock);
	ctrl = sun4i_pwm_readl(sun4i_pwm, PWM_CTRL_REG);

	if ((cstate.period != state->period) ||
	    (cstate.duty_cycle != state->duty_cycle)) {
		u32 period, duty, val;
		unsigned int prescaler;

		ret = sun4i_pwm_calculate(sun4i_pwm, state,
					  &duty, &period, &prescaler);
		if (ret) {
			dev_err(chip->dev, "period exceeds the maximum value\n");
			spin_unlock(&sun4i_pwm->ctrl_lock);
			if (!cstate.enabled)
				clk_disable_unprepare(sun4i_pwm->clk);
			return ret;
		}

		if (PWM_REG_PRESCAL(ctrl, pwm->hwpwm) != prescaler) {
			/* Prescaler changed, the clock has to be gated */
			ctrl &= ~BIT_CH(PWM_CLK_GATING, pwm->hwpwm);
			sun4i_pwm_writel(sun4i_pwm, ctrl, PWM_CTRL_REG);

			ctrl &= ~BIT_CH(PWM_PRESCAL_MASK, pwm->hwpwm);
			ctrl |= BIT_CH(prescaler, pwm->hwpwm);
		}

		val = (duty & PWM_DTY_MASK) | PWM_PRD(period);
		sun4i_pwm_writel(sun4i_pwm, val, PWM_CH_PRD(pwm->hwpwm));
		sun4i_pwm->next_period[pwm->hwpwm] = jiffies +
			usecs_to_jiffies(cstate.period / 1000 + 1);
		sun4i_pwm->needs_delay[pwm->hwpwm] = true;
	}

	if (state->polarity != PWM_POLARITY_NORMAL)
		ctrl &= ~BIT_CH(PWM_ACT_STATE, pwm->hwpwm);
	else
		ctrl |= BIT_CH(PWM_ACT_STATE, pwm->hwpwm);

	ctrl |= BIT_CH(PWM_CLK_GATING, pwm->hwpwm);
	if (state->enabled) {
		ctrl |= BIT_CH(PWM_EN, pwm->hwpwm);
	} else if (!sun4i_pwm->needs_delay[pwm->hwpwm]) {
		ctrl &= ~BIT_CH(PWM_EN, pwm->hwpwm);
		ctrl &= ~BIT_CH(PWM_CLK_GATING, pwm->hwpwm);
	}

	sun4i_pwm_writel(sun4i_pwm, ctrl, PWM_CTRL_REG);

	spin_unlock(&sun4i_pwm->ctrl_lock);

	if (state->enabled)
		return 0;

	if (!sun4i_pwm->needs_delay[pwm->hwpwm]) {
		clk_disable_unprepare(sun4i_pwm->clk);
		return 0;
	}

	/* We need a full period to elapse before disabling the channel. */
	now = jiffies;
	if (sun4i_pwm->needs_delay[pwm->hwpwm] &&
	    time_before(now, sun4i_pwm->next_period[pwm->hwpwm])) {
		delay_us = jiffies_to_usecs(sun4i_pwm->next_period[pwm->hwpwm] -
					   now);
		if ((delay_us / 500) > MAX_UDELAY_MS)
			msleep(delay_us / 1000 + 1);
		else
			usleep_range(delay_us, delay_us * 2);
	}
	sun4i_pwm->needs_delay[pwm->hwpwm] = false;

	spin_lock(&sun4i_pwm->ctrl_lock);
	ctrl = sun4i_pwm_readl(sun4i_pwm, PWM_CTRL_REG);
	ctrl &= ~BIT_CH(PWM_CLK_GATING, pwm->hwpwm);
	ctrl &= ~BIT_CH(PWM_EN, pwm->hwpwm);
	sun4i_pwm_writel(sun4i_pwm, ctrl, PWM_CTRL_REG);
	spin_unlock(&sun4i_pwm->ctrl_lock);

	clk_disable_unprepare(sun4i_pwm->clk);

	return 0;
}

static const struct pwm_ops sun4i_pwm_ops = {
	.apply = sun4i_pwm_apply,
	.get_state = sun4i_pwm_get_state,
	.owner = THIS_MODULE,
};

static const struct sun4i_pwm_data sun4i_pwm_data_a10 = {
	.has_prescaler_bypass = false,
	.has_rdy = false,
	.npwm = 2,
};

static const struct sun4i_pwm_data sun4i_pwm_data_a10s = {
	.has_prescaler_bypass = true,
	.has_rdy = true,
	.npwm = 2,
};

static const struct sun4i_pwm_data sun4i_pwm_data_a13 = {
	.has_prescaler_bypass = true,
	.has_rdy = true,
	.npwm = 1,
};

static const struct sun4i_pwm_data sun4i_pwm_data_a20 = {
	.has_prescaler_bypass = true,
	.has_rdy = true,
	.npwm = 2,
};

static const struct sun4i_pwm_data sun4i_pwm_data_h3 = {
	.has_prescaler_bypass = true,
	.has_rdy = true,
	.npwm = 1,
};

static const struct of_device_id sun4i_pwm_dt_ids[] = {
	{
		.compatible = "allwinner,sun4i-a10-pwm",
		.data = &sun4i_pwm_data_a10,
	}, {
		.compatible = "allwinner,sun5i-a10s-pwm",
		.data = &sun4i_pwm_data_a10s,
	}, {
		.compatible = "allwinner,sun5i-a13-pwm",
		.data = &sun4i_pwm_data_a13,
	}, {
		.compatible = "allwinner,sun7i-a20-pwm",
		.data = &sun4i_pwm_data_a20,
	}, {
		.compatible = "allwinner,sun8i-h3-pwm",
		.data = &sun4i_pwm_data_h3,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, sun4i_pwm_dt_ids);

static int sun4i_pwm_probe(struct platform_device *pdev)
{
	struct sun4i_pwm_chip *pwm;
	struct resource *res;
	int ret;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->data = of_device_get_match_data(&pdev->dev);
	if (!pwm->data)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pwm->base))
		return PTR_ERR(pwm->base);

	pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pwm->clk))
		return PTR_ERR(pwm->clk);

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &sun4i_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = pwm->data->npwm;
	pwm->chip.of_xlate = of_pwm_xlate_with_flags;
	pwm->chip.of_pwm_n_cells = 3;

	spin_lock_init(&pwm->ctrl_lock);

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);

	return 0;
}

static int sun4i_pwm_remove(struct platform_device *pdev)
{
	struct sun4i_pwm_chip *pwm = platform_get_drvdata(pdev);

	return pwmchip_remove(&pwm->chip);
}

static struct platform_driver sun4i_pwm_driver = {
	.driver = {
		.name = "sun4i-pwm",
		.of_match_table = sun4i_pwm_dt_ids,
	},
	.probe = sun4i_pwm_probe,
	.remove = sun4i_pwm_remove,
};
module_platform_driver(sun4i_pwm_driver);

MODULE_ALIAS("platform:sun4i-pwm");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner sun4i PWM driver");
MODULE_LICENSE("GPL v2");
