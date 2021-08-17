// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Allwinner sun4i Pulse Width Modulation Controller
 *
 * Copyright (C) 2014 Alexandre Belloni <alexandre.belloni@free-electrons.com>
 *
 * Limitations:
 * - When outputing the source clock directly, the PWM logic will be bypassed
 *   and the currently running period is not guaranteed to be completed
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
#include <linux/reset.h>
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
	bool has_direct_mod_clk_output;
	unsigned int npwm;
};

struct sun4i_pwm_chip {
	struct pwm_chip chip;
	struct clk *bus_clk;
	struct clk *clk;
	struct reset_control *rst;
	void __iomem *base;
	spinlock_t ctrl_lock;
	const struct sun4i_pwm_data *data;
	unsigned long next_period[2];
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

	/*
	 * PWM chapter in H6 manual has a diagram which explains that if bypass
	 * bit is set, no other setting has any meaning. Even more, experiment
	 * proved that also enable bit is ignored in this case.
	 */
	if ((val & BIT_CH(PWM_BYPASS, pwm->hwpwm)) &&
	    sun4i_pwm->data->has_direct_mod_clk_output) {
		state->period = DIV_ROUND_UP_ULL(NSEC_PER_SEC, clk_rate);
		state->duty_cycle = DIV_ROUND_UP_ULL(state->period, 2);
		state->polarity = PWM_POLARITY_NORMAL;
		state->enabled = true;
		return;
	}

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

	tmp = (u64)prescaler * NSEC_PER_SEC * PWM_REG_DTY(val);
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, clk_rate);

	tmp = (u64)prescaler * NSEC_PER_SEC * PWM_REG_PRD(val);
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, clk_rate);
}

static int sun4i_pwm_calculate(struct sun4i_pwm_chip *sun4i_pwm,
			       const struct pwm_state *state,
			       u32 *dty, u32 *prd, unsigned int *prsclr,
			       bool *bypass)
{
	u64 clk_rate, div = 0;
	unsigned int prescaler = 0;

	clk_rate = clk_get_rate(sun4i_pwm->clk);

	*bypass = sun4i_pwm->data->has_direct_mod_clk_output &&
		  state->enabled &&
		  (state->period * clk_rate >= NSEC_PER_SEC) &&
		  (state->period * clk_rate < 2 * NSEC_PER_SEC) &&
		  (state->duty_cycle * clk_rate * 2 >= NSEC_PER_SEC);

	/* Skip calculation of other parameters if we bypass them */
	if (*bypass)
		return 0;

	if (sun4i_pwm->data->has_prescaler_bypass) {
		/* First, test without any prescaler when available */
		prescaler = PWM_PRESCAL_MASK;
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
			unsigned int pval = prescaler_table[prescaler];

			if (!pval)
				continue;

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

	return 0;
}

static int sun4i_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct sun4i_pwm_chip *sun4i_pwm = to_sun4i_pwm_chip(chip);
	struct pwm_state cstate;
	u32 ctrl, duty = 0, period = 0, val;
	int ret;
	unsigned int delay_us, prescaler = 0;
	unsigned long now;
	bool bypass;

	pwm_get_state(pwm, &cstate);

	if (!cstate.enabled) {
		ret = clk_prepare_enable(sun4i_pwm->clk);
		if (ret) {
			dev_err(chip->dev, "failed to enable PWM clock\n");
			return ret;
		}
	}

	ret = sun4i_pwm_calculate(sun4i_pwm, state, &duty, &period, &prescaler,
				  &bypass);
	if (ret) {
		dev_err(chip->dev, "period exceeds the maximum value\n");
		if (!cstate.enabled)
			clk_disable_unprepare(sun4i_pwm->clk);
		return ret;
	}

	spin_lock(&sun4i_pwm->ctrl_lock);
	ctrl = sun4i_pwm_readl(sun4i_pwm, PWM_CTRL_REG);

	if (sun4i_pwm->data->has_direct_mod_clk_output) {
		if (bypass) {
			ctrl |= BIT_CH(PWM_BYPASS, pwm->hwpwm);
			/* We can skip other parameter */
			sun4i_pwm_writel(sun4i_pwm, ctrl, PWM_CTRL_REG);
			spin_unlock(&sun4i_pwm->ctrl_lock);
			return 0;
		}

		ctrl &= ~BIT_CH(PWM_BYPASS, pwm->hwpwm);
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
		nsecs_to_jiffies(cstate.period + 1000);

	if (state->polarity != PWM_POLARITY_NORMAL)
		ctrl &= ~BIT_CH(PWM_ACT_STATE, pwm->hwpwm);
	else
		ctrl |= BIT_CH(PWM_ACT_STATE, pwm->hwpwm);

	ctrl |= BIT_CH(PWM_CLK_GATING, pwm->hwpwm);

	if (state->enabled)
		ctrl |= BIT_CH(PWM_EN, pwm->hwpwm);

	sun4i_pwm_writel(sun4i_pwm, ctrl, PWM_CTRL_REG);

	spin_unlock(&sun4i_pwm->ctrl_lock);

	if (state->enabled)
		return 0;

	/* We need a full period to elapse before disabling the channel. */
	now = jiffies;
	if (time_before(now, sun4i_pwm->next_period[pwm->hwpwm])) {
		delay_us = jiffies_to_usecs(sun4i_pwm->next_period[pwm->hwpwm] -
					   now);
		if ((delay_us / 500) > MAX_UDELAY_MS)
			msleep(delay_us / 1000 + 1);
		else
			usleep_range(delay_us, delay_us * 2);
	}

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

static const struct sun4i_pwm_data sun4i_pwm_dual_nobypass = {
	.has_prescaler_bypass = false,
	.npwm = 2,
};

static const struct sun4i_pwm_data sun4i_pwm_dual_bypass = {
	.has_prescaler_bypass = true,
	.npwm = 2,
};

static const struct sun4i_pwm_data sun4i_pwm_single_bypass = {
	.has_prescaler_bypass = true,
	.npwm = 1,
};

static const struct sun4i_pwm_data sun50i_a64_pwm_data = {
	.has_prescaler_bypass = true,
	.has_direct_mod_clk_output = true,
	.npwm = 1,
};

static const struct sun4i_pwm_data sun50i_h6_pwm_data = {
	.has_prescaler_bypass = true,
	.has_direct_mod_clk_output = true,
	.npwm = 2,
};

static const struct of_device_id sun4i_pwm_dt_ids[] = {
	{
		.compatible = "allwinner,sun4i-a10-pwm",
		.data = &sun4i_pwm_dual_nobypass,
	}, {
		.compatible = "allwinner,sun5i-a10s-pwm",
		.data = &sun4i_pwm_dual_bypass,
	}, {
		.compatible = "allwinner,sun5i-a13-pwm",
		.data = &sun4i_pwm_single_bypass,
	}, {
		.compatible = "allwinner,sun7i-a20-pwm",
		.data = &sun4i_pwm_dual_bypass,
	}, {
		.compatible = "allwinner,sun8i-h3-pwm",
		.data = &sun4i_pwm_single_bypass,
	}, {
		.compatible = "allwinner,sun50i-a64-pwm",
		.data = &sun50i_a64_pwm_data,
	}, {
		.compatible = "allwinner,sun50i-h6-pwm",
		.data = &sun50i_h6_pwm_data,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, sun4i_pwm_dt_ids);

static int sun4i_pwm_probe(struct platform_device *pdev)
{
	struct sun4i_pwm_chip *pwm;
	int ret;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->data = of_device_get_match_data(&pdev->dev);
	if (!pwm->data)
		return -ENODEV;

	pwm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pwm->base))
		return PTR_ERR(pwm->base);

	/*
	 * All hardware variants need a source clock that is divided and
	 * then feeds the counter that defines the output wave form. In the
	 * device tree this clock is either unnamed or called "mod".
	 * Some variants (e.g. H6) need another clock to access the
	 * hardware registers; this is called "bus".
	 * So we request "mod" first (and ignore the corner case that a
	 * parent provides a "mod" clock while the right one would be the
	 * unnamed one of the PWM device) and if this is not found we fall
	 * back to the first clock of the PWM.
	 */
	pwm->clk = devm_clk_get_optional(&pdev->dev, "mod");
	if (IS_ERR(pwm->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pwm->clk),
				     "get mod clock failed\n");

	if (!pwm->clk) {
		pwm->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(pwm->clk))
			return dev_err_probe(&pdev->dev, PTR_ERR(pwm->clk),
					     "get unnamed clock failed\n");
	}

	pwm->bus_clk = devm_clk_get_optional(&pdev->dev, "bus");
	if (IS_ERR(pwm->bus_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pwm->bus_clk),
				     "get bus clock failed\n");

	pwm->rst = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(pwm->rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(pwm->rst),
				     "get reset failed\n");

	/* Deassert reset */
	ret = reset_control_deassert(pwm->rst);
	if (ret) {
		dev_err(&pdev->dev, "cannot deassert reset control: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/*
	 * We're keeping the bus clock on for the sake of simplicity.
	 * Actually it only needs to be on for hardware register accesses.
	 */
	ret = clk_prepare_enable(pwm->bus_clk);
	if (ret) {
		dev_err(&pdev->dev, "cannot prepare and enable bus_clk %pe\n",
			ERR_PTR(ret));
		goto err_bus;
	}

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &sun4i_pwm_ops;
	pwm->chip.npwm = pwm->data->npwm;

	spin_lock_init(&pwm->ctrl_lock);

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip: %d\n", ret);
		goto err_pwm_add;
	}

	platform_set_drvdata(pdev, pwm);

	return 0;

err_pwm_add:
	clk_disable_unprepare(pwm->bus_clk);
err_bus:
	reset_control_assert(pwm->rst);

	return ret;
}

static int sun4i_pwm_remove(struct platform_device *pdev)
{
	struct sun4i_pwm_chip *pwm = platform_get_drvdata(pdev);
	int ret;

	ret = pwmchip_remove(&pwm->chip);
	if (ret)
		return ret;

	clk_disable_unprepare(pwm->bus_clk);
	reset_control_assert(pwm->rst);

	return 0;
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
