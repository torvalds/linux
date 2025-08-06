// SPDX-License-Identifier: GPL-2.0
/*
 * STM32 Low-Power Timer PWM driver
 *
 * Copyright (C) STMicroelectronics 2017
 *
 * Author: Gerald Baeza <gerald.baeza@st.com>
 *
 * Inspired by Gerald Baeza's pwm-stm32 driver
 */

#include <linux/bitfield.h>
#include <linux/mfd/stm32-lptimer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

struct stm32_pwm_lp {
	struct clk *clk;
	struct regmap *regmap;
	unsigned int num_cc_chans;
};

static inline struct stm32_pwm_lp *to_stm32_pwm_lp(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

/* STM32 Low-Power Timer is preceded by a configurable power-of-2 prescaler */
#define STM32_LPTIM_MAX_PRESCALER	128

static int stm32_pwm_lp_update_allowed(struct stm32_pwm_lp *priv, int channel)
{
	int ret;
	u32 ccmr1;
	unsigned long ccmr;

	/* Only one PWM on this LPTIMER: enable, prescaler and reload value can be changed */
	if (!priv->num_cc_chans)
		return true;

	ret = regmap_read(priv->regmap, STM32_LPTIM_CCMR1, &ccmr1);
	if (ret)
		return ret;
	ccmr = ccmr1 & (STM32_LPTIM_CC1E | STM32_LPTIM_CC2E);

	/* More than one channel enabled: enable, prescaler or ARR value can't be changed */
	if (bitmap_weight(&ccmr, sizeof(u32) * BITS_PER_BYTE) > 1)
		return false;

	/*
	 * Only one channel is enabled (or none): check status on the other channel, to
	 * report if enable, prescaler or ARR value can be changed.
	 */
	if (channel)
		return !(ccmr1 & STM32_LPTIM_CC1E);
	else
		return !(ccmr1 & STM32_LPTIM_CC2E);
}

static int stm32_pwm_lp_compare_channel_apply(struct stm32_pwm_lp *priv, int channel,
					      bool enable, enum pwm_polarity polarity)
{
	u32 ccmr1, val, mask;
	bool reenable;
	int ret;

	/* No dedicated CC channel: nothing to do */
	if (!priv->num_cc_chans)
		return 0;

	ret = regmap_read(priv->regmap, STM32_LPTIM_CCMR1, &ccmr1);
	if (ret)
		return ret;

	if (channel) {
		/* Must disable CC channel (CCxE) to modify polarity (CCxP), then re-enable */
		reenable = (enable && FIELD_GET(STM32_LPTIM_CC2E, ccmr1)) &&
			(polarity != FIELD_GET(STM32_LPTIM_CC2P, ccmr1));

		mask = STM32_LPTIM_CC2SEL | STM32_LPTIM_CC2E | STM32_LPTIM_CC2P;
		val = FIELD_PREP(STM32_LPTIM_CC2P, polarity);
		val |= FIELD_PREP(STM32_LPTIM_CC2E, enable);
	} else {
		reenable = (enable && FIELD_GET(STM32_LPTIM_CC1E, ccmr1)) &&
			(polarity != FIELD_GET(STM32_LPTIM_CC1P, ccmr1));

		mask = STM32_LPTIM_CC1SEL | STM32_LPTIM_CC1E | STM32_LPTIM_CC1P;
		val = FIELD_PREP(STM32_LPTIM_CC1P, polarity);
		val |= FIELD_PREP(STM32_LPTIM_CC1E, enable);
	}

	if (reenable) {
		u32 cfgr, presc;
		unsigned long rate;
		unsigned int delay_us;

		ret = regmap_update_bits(priv->regmap, STM32_LPTIM_CCMR1,
					 channel ? STM32_LPTIM_CC2E : STM32_LPTIM_CC1E, 0);
		if (ret)
			return ret;
		/*
		 * After a write to the LPTIM_CCMRx register, a new write operation can only be
		 * performed after a delay of at least (PRESC × 3) clock cycles
		 */
		ret = regmap_read(priv->regmap, STM32_LPTIM_CFGR, &cfgr);
		if (ret)
			return ret;
		presc = FIELD_GET(STM32_LPTIM_PRESC, cfgr);
		rate = clk_get_rate(priv->clk) >> presc;
		if (!rate)
			return -EINVAL;
		delay_us = 3 * DIV_ROUND_UP(USEC_PER_SEC, rate);
		usleep_range(delay_us, delay_us * 2);
	}

	return regmap_update_bits(priv->regmap, STM32_LPTIM_CCMR1, mask, val);
}

static int stm32_pwm_lp_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct stm32_pwm_lp *priv = to_stm32_pwm_lp(chip);
	unsigned long long prd, div, dty;
	struct pwm_state cstate;
	u32 arr, val, mask, cfgr, presc = 0;
	bool reenable;
	int ret;

	pwm_get_state(pwm, &cstate);
	reenable = !cstate.enabled;

	if (!state->enabled) {
		if (cstate.enabled) {
			/* Disable CC channel if any */
			ret = stm32_pwm_lp_compare_channel_apply(priv, pwm->hwpwm, false,
								 state->polarity);
			if (ret)
				return ret;
			ret = regmap_write(priv->regmap, pwm->hwpwm ?
					   STM32_LPTIM_CCR2 : STM32_LPTIM_CMP, 0);
			if (ret)
				return ret;

			/* Check if the timer can be disabled */
			ret = stm32_pwm_lp_update_allowed(priv, pwm->hwpwm);
			if (ret < 0)
				return ret;

			if (ret) {
				/* Disable LP timer */
				ret = regmap_write(priv->regmap, STM32_LPTIM_CR, 0);
				if (ret)
					return ret;
			}

			/* disable clock to PWM counter */
			clk_disable(priv->clk);
		}
		return 0;
	}

	/* Calculate the period and prescaler value */
	div = (unsigned long long)clk_get_rate(priv->clk) * state->period;
	do_div(div, NSEC_PER_SEC);
	if (!div) {
		/* Clock is too slow to achieve requested period. */
		dev_dbg(pwmchip_parent(chip), "Can't reach %llu ns\n", state->period);
		return -EINVAL;
	}

	prd = div;
	while (div > STM32_LPTIM_MAX_ARR) {
		presc++;
		if ((1 << presc) > STM32_LPTIM_MAX_PRESCALER) {
			dev_err(pwmchip_parent(chip), "max prescaler exceeded\n");
			return -EINVAL;
		}
		div = prd >> presc;
	}
	prd = div;

	/* Calculate the duty cycle */
	dty = prd * state->duty_cycle;
	do_div(dty, state->period);

	ret = regmap_read(priv->regmap, STM32_LPTIM_CFGR, &cfgr);
	if (ret)
		return ret;

	/*
	 * When there are several channels, they share the same prescaler and reload value.
	 * Check if this can be changed, or the values are the same for all channels.
	 */
	if (!stm32_pwm_lp_update_allowed(priv, pwm->hwpwm)) {
		ret = regmap_read(priv->regmap, STM32_LPTIM_ARR, &arr);
		if (ret)
			return ret;

		if ((FIELD_GET(STM32_LPTIM_PRESC, cfgr) != presc) || (arr != prd - 1))
			return -EBUSY;
	}

	if (!cstate.enabled) {
		/* enable clock to drive PWM counter */
		ret = clk_enable(priv->clk);
		if (ret)
			return ret;
	}

	if ((FIELD_GET(STM32_LPTIM_PRESC, cfgr) != presc) ||
	    ((FIELD_GET(STM32_LPTIM_WAVPOL, cfgr) != state->polarity) && !priv->num_cc_chans)) {
		val = FIELD_PREP(STM32_LPTIM_PRESC, presc);
		mask = STM32_LPTIM_PRESC;

		if (!priv->num_cc_chans) {
			/*
			 * WAVPOL bit is only available when no capature compare channel is used,
			 * e.g. on LPTIMER instances that have only one output channel. CCMR1 is
			 * used otherwise.
			 */
			val |= FIELD_PREP(STM32_LPTIM_WAVPOL, state->polarity);
			mask |= STM32_LPTIM_WAVPOL;
		}

		/* Must disable LP timer to modify CFGR */
		reenable = true;
		ret = regmap_write(priv->regmap, STM32_LPTIM_CR, 0);
		if (ret)
			goto err;

		ret = regmap_update_bits(priv->regmap, STM32_LPTIM_CFGR, mask,
					 val);
		if (ret)
			goto err;
	}

	if (reenable) {
		/* Must (re)enable LP timer to modify CMP & ARR */
		ret = regmap_write(priv->regmap, STM32_LPTIM_CR,
				   STM32_LPTIM_ENABLE);
		if (ret)
			goto err;
	}

	ret = regmap_write(priv->regmap, STM32_LPTIM_ARR, prd - 1);
	if (ret)
		goto err;

	/* Write CMP/CCRx register and ensure it's been properly written */
	ret = regmap_write(priv->regmap, pwm->hwpwm ? STM32_LPTIM_CCR2 : STM32_LPTIM_CMP,
			   prd - (1 + dty));
	if (ret)
		goto err;

	/* ensure ARR and CMP/CCRx registers are properly written */
	ret = regmap_read_poll_timeout(priv->regmap, STM32_LPTIM_ISR, val, pwm->hwpwm ?
				       (val & STM32_LPTIM_CMP2_ARROK) == STM32_LPTIM_CMP2_ARROK :
				       (val & STM32_LPTIM_CMPOK_ARROK) == STM32_LPTIM_CMPOK_ARROK,
				       100, 1000);
	if (ret) {
		dev_err(pwmchip_parent(chip), "ARR/CMP registers write issue\n");
		goto err;
	}
	ret = regmap_write(priv->regmap, STM32_LPTIM_ICR, pwm->hwpwm ?
			   STM32_LPTIM_CMP2OKCF_ARROKCF : STM32_LPTIM_CMPOKCF_ARROKCF);
	if (ret)
		goto err;

	ret = stm32_pwm_lp_compare_channel_apply(priv, pwm->hwpwm, true, state->polarity);
	if (ret)
		goto err;

	if (reenable) {
		/* Start LP timer in continuous mode */
		ret = regmap_set_bits(priv->regmap, STM32_LPTIM_CR,
				      STM32_LPTIM_CNTSTRT);
		if (ret) {
			regmap_write(priv->regmap, STM32_LPTIM_CR, 0);
			goto err;
		}
	}

	return 0;
err:
	if (!cstate.enabled)
		clk_disable(priv->clk);

	return ret;
}

static int stm32_pwm_lp_get_state(struct pwm_chip *chip,
				  struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct stm32_pwm_lp *priv = to_stm32_pwm_lp(chip);
	unsigned long rate = clk_get_rate(priv->clk);
	u32 val, presc, prd, ccmr1;
	bool enabled;
	u64 tmp;

	regmap_read(priv->regmap, STM32_LPTIM_CR, &val);
	enabled = !!FIELD_GET(STM32_LPTIM_ENABLE, val);
	if (priv->num_cc_chans) {
		/* There's a CC chan, need to also check if it's enabled */
		regmap_read(priv->regmap, STM32_LPTIM_CCMR1, &ccmr1);
		if (pwm->hwpwm)
			enabled &= !!FIELD_GET(STM32_LPTIM_CC2E, ccmr1);
		else
			enabled &= !!FIELD_GET(STM32_LPTIM_CC1E, ccmr1);
	}
	state->enabled = enabled;

	/* Keep PWM counter clock refcount in sync with PWM initial state */
	if (state->enabled) {
		int ret = clk_enable(priv->clk);

		if (ret)
			return ret;
	}

	regmap_read(priv->regmap, STM32_LPTIM_CFGR, &val);
	presc = FIELD_GET(STM32_LPTIM_PRESC, val);
	if (priv->num_cc_chans) {
		if (pwm->hwpwm)
			state->polarity = FIELD_GET(STM32_LPTIM_CC2P, ccmr1);
		else
			state->polarity = FIELD_GET(STM32_LPTIM_CC1P, ccmr1);
	} else {
		state->polarity = FIELD_GET(STM32_LPTIM_WAVPOL, val);
	}

	regmap_read(priv->regmap, STM32_LPTIM_ARR, &prd);
	tmp = prd + 1;
	tmp = (tmp << presc) * NSEC_PER_SEC;
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, rate);

	regmap_read(priv->regmap, pwm->hwpwm ? STM32_LPTIM_CCR2 : STM32_LPTIM_CMP, &val);
	tmp = prd - val;
	tmp = (tmp << presc) * NSEC_PER_SEC;
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, rate);

	return 0;
}

static const struct pwm_ops stm32_pwm_lp_ops = {
	.apply = stm32_pwm_lp_apply,
	.get_state = stm32_pwm_lp_get_state,
};

static int stm32_pwm_lp_probe(struct platform_device *pdev)
{
	struct stm32_lptimer *ddata = dev_get_drvdata(pdev->dev.parent);
	struct stm32_pwm_lp *priv;
	struct pwm_chip *chip;
	unsigned int npwm;
	int ret;

	if (!ddata->num_cc_chans) {
		/* No dedicated CC channel, so there's only one PWM channel */
		npwm = 1;
	} else {
		/* There are dedicated CC channels, each with one PWM output */
		npwm = ddata->num_cc_chans;
	}

	chip = devm_pwmchip_alloc(&pdev->dev, npwm, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = to_stm32_pwm_lp(chip);

	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->num_cc_chans = ddata->num_cc_chans;
	chip->ops = &stm32_pwm_lp_ops;

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, chip);

	return 0;
}

static int stm32_pwm_lp_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct pwm_state state;
	unsigned int i;

	for (i = 0; i < chip->npwm; i++) {
		pwm_get_state(&chip->pwms[i], &state);
		if (state.enabled) {
			dev_err(dev, "The consumer didn't stop us (%s)\n",
				chip->pwms[i].label);
			return -EBUSY;
		}
	}

	return pinctrl_pm_select_sleep_state(dev);
}

static int stm32_pwm_lp_resume(struct device *dev)
{
	return pinctrl_pm_select_default_state(dev);
}

static DEFINE_SIMPLE_DEV_PM_OPS(stm32_pwm_lp_pm_ops, stm32_pwm_lp_suspend,
				stm32_pwm_lp_resume);

static const struct of_device_id stm32_pwm_lp_of_match[] = {
	{ .compatible = "st,stm32-pwm-lp", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_pwm_lp_of_match);

static struct platform_driver stm32_pwm_lp_driver = {
	.probe	= stm32_pwm_lp_probe,
	.driver	= {
		.name = "stm32-pwm-lp",
		.of_match_table = stm32_pwm_lp_of_match,
		.pm = pm_ptr(&stm32_pwm_lp_pm_ops),
	},
};
module_platform_driver(stm32_pwm_lp_driver);

MODULE_ALIAS("platform:stm32-pwm-lp");
MODULE_DESCRIPTION("STMicroelectronics STM32 PWM LP driver");
MODULE_LICENSE("GPL v2");
