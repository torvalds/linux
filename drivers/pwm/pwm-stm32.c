/*
 * Copyright (C) STMicroelectronics 2016
 *
 * Author: Gerald Baeza <gerald.baeza@st.com>
 *
 * License terms: GNU General Public License (GPL), version 2
 *
 * Inspired by timer-stm32.c from Maxime Coquelin
 *             pwm-atmel.c from Bo Shen
 */

#include <linux/mfd/stm32-timers.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define CCMR_CHANNEL_SHIFT 8
#define CCMR_CHANNEL_MASK  0xFF
#define MAX_BREAKINPUT 2

struct stm32_pwm {
	struct pwm_chip chip;
	struct device *dev;
	struct clk *clk;
	struct regmap *regmap;
	u32 max_arr;
	bool have_complementary_output;
};

struct stm32_breakinput {
	u32 index;
	u32 level;
	u32 filter;
};

static inline struct stm32_pwm *to_stm32_pwm_dev(struct pwm_chip *chip)
{
	return container_of(chip, struct stm32_pwm, chip);
}

static u32 active_channels(struct stm32_pwm *dev)
{
	u32 ccer;

	regmap_read(dev->regmap, TIM_CCER, &ccer);

	return ccer & TIM_CCER_CCXE;
}

static int write_ccrx(struct stm32_pwm *dev, int ch, u32 value)
{
	switch (ch) {
	case 0:
		return regmap_write(dev->regmap, TIM_CCR1, value);
	case 1:
		return regmap_write(dev->regmap, TIM_CCR2, value);
	case 2:
		return regmap_write(dev->regmap, TIM_CCR3, value);
	case 3:
		return regmap_write(dev->regmap, TIM_CCR4, value);
	}
	return -EINVAL;
}

static int stm32_pwm_config(struct stm32_pwm *priv, int ch,
			    int duty_ns, int period_ns)
{
	unsigned long long prd, div, dty;
	unsigned int prescaler = 0;
	u32 ccmr, mask, shift;

	/* Period and prescaler values depends on clock rate */
	div = (unsigned long long)clk_get_rate(priv->clk) * period_ns;

	do_div(div, NSEC_PER_SEC);
	prd = div;

	while (div > priv->max_arr) {
		prescaler++;
		div = prd;
		do_div(div, prescaler + 1);
	}

	prd = div;

	if (prescaler > MAX_TIM_PSC)
		return -EINVAL;

	/*
	 * All channels share the same prescaler and counter so when two
	 * channels are active at the same time we can't change them
	 */
	if (active_channels(priv) & ~(1 << ch * 4)) {
		u32 psc, arr;

		regmap_read(priv->regmap, TIM_PSC, &psc);
		regmap_read(priv->regmap, TIM_ARR, &arr);

		if ((psc != prescaler) || (arr != prd - 1))
			return -EBUSY;
	}

	regmap_write(priv->regmap, TIM_PSC, prescaler);
	regmap_write(priv->regmap, TIM_ARR, prd - 1);
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, TIM_CR1_ARPE);

	/* Calculate the duty cycles */
	dty = prd * duty_ns;
	do_div(dty, period_ns);

	write_ccrx(priv, ch, dty);

	/* Configure output mode */
	shift = (ch & 0x1) * CCMR_CHANNEL_SHIFT;
	ccmr = (TIM_CCMR_PE | TIM_CCMR_M1) << shift;
	mask = CCMR_CHANNEL_MASK << shift;

	if (ch < 2)
		regmap_update_bits(priv->regmap, TIM_CCMR1, mask, ccmr);
	else
		regmap_update_bits(priv->regmap, TIM_CCMR2, mask, ccmr);

	regmap_update_bits(priv->regmap, TIM_BDTR,
			   TIM_BDTR_MOE | TIM_BDTR_AOE,
			   TIM_BDTR_MOE | TIM_BDTR_AOE);

	return 0;
}

static int stm32_pwm_set_polarity(struct stm32_pwm *priv, int ch,
				  enum pwm_polarity polarity)
{
	u32 mask;

	mask = TIM_CCER_CC1P << (ch * 4);
	if (priv->have_complementary_output)
		mask |= TIM_CCER_CC1NP << (ch * 4);

	regmap_update_bits(priv->regmap, TIM_CCER, mask,
			   polarity == PWM_POLARITY_NORMAL ? 0 : mask);

	return 0;
}

static int stm32_pwm_enable(struct stm32_pwm *priv, int ch)
{
	u32 mask;
	int ret;

	ret = clk_enable(priv->clk);
	if (ret)
		return ret;

	/* Enable channel */
	mask = TIM_CCER_CC1E << (ch * 4);
	if (priv->have_complementary_output)
		mask |= TIM_CCER_CC1NE << (ch * 4);

	regmap_update_bits(priv->regmap, TIM_CCER, mask, mask);

	/* Make sure that registers are updated */
	regmap_update_bits(priv->regmap, TIM_EGR, TIM_EGR_UG, TIM_EGR_UG);

	/* Enable controller */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, TIM_CR1_CEN);

	return 0;
}

static void stm32_pwm_disable(struct stm32_pwm *priv, int ch)
{
	u32 mask;

	/* Disable channel */
	mask = TIM_CCER_CC1E << (ch * 4);
	if (priv->have_complementary_output)
		mask |= TIM_CCER_CC1NE << (ch * 4);

	regmap_update_bits(priv->regmap, TIM_CCER, mask, 0);

	/* When all channels are disabled, we can disable the controller */
	if (!active_channels(priv))
		regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);

	clk_disable(priv->clk);
}

static int stm32_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   struct pwm_state *state)
{
	bool enabled;
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	int ret;

	enabled = pwm->state.enabled;

	if (enabled && !state->enabled) {
		stm32_pwm_disable(priv, pwm->hwpwm);
		return 0;
	}

	if (state->polarity != pwm->state.polarity)
		stm32_pwm_set_polarity(priv, pwm->hwpwm, state->polarity);

	ret = stm32_pwm_config(priv, pwm->hwpwm,
			       state->duty_cycle, state->period);
	if (ret)
		return ret;

	if (!enabled && state->enabled)
		ret = stm32_pwm_enable(priv, pwm->hwpwm);

	return ret;
}

static const struct pwm_ops stm32pwm_ops = {
	.owner = THIS_MODULE,
	.apply = stm32_pwm_apply,
};

static int stm32_pwm_set_breakinput(struct stm32_pwm *priv,
				    int index, int level, int filter)
{
	u32 bke = (index == 0) ? TIM_BDTR_BKE : TIM_BDTR_BK2E;
	int shift = (index == 0) ? TIM_BDTR_BKF_SHIFT : TIM_BDTR_BK2F_SHIFT;
	u32 mask = (index == 0) ? TIM_BDTR_BKE | TIM_BDTR_BKP | TIM_BDTR_BKF
				: TIM_BDTR_BK2E | TIM_BDTR_BK2P | TIM_BDTR_BK2F;
	u32 bdtr = bke;

	/*
	 * The both bits could be set since only one will be wrote
	 * due to mask value.
	 */
	if (level)
		bdtr |= TIM_BDTR_BKP | TIM_BDTR_BK2P;

	bdtr |= (filter & TIM_BDTR_BKF_MASK) << shift;

	regmap_update_bits(priv->regmap, TIM_BDTR, mask, bdtr);

	regmap_read(priv->regmap, TIM_BDTR, &bdtr);

	return (bdtr & bke) ? 0 : -EINVAL;
}

static int stm32_pwm_apply_breakinputs(struct stm32_pwm *priv,
				       struct device_node *np)
{
	struct stm32_breakinput breakinput[MAX_BREAKINPUT];
	int nb, ret, i, array_size;

	nb = of_property_count_elems_of_size(np, "st,breakinput",
					     sizeof(struct stm32_breakinput));

	/*
	 * Because "st,breakinput" parameter is optional do not make probe
	 * failed if it doesn't exist.
	 */
	if (nb <= 0)
		return 0;

	if (nb > MAX_BREAKINPUT)
		return -EINVAL;

	array_size = nb * sizeof(struct stm32_breakinput) / sizeof(u32);
	ret = of_property_read_u32_array(np, "st,breakinput",
					 (u32 *)breakinput, array_size);
	if (ret)
		return ret;

	for (i = 0; i < nb && !ret; i++) {
		ret = stm32_pwm_set_breakinput(priv,
					       breakinput[i].index,
					       breakinput[i].level,
					       breakinput[i].filter);
	}

	return ret;
}

static void stm32_pwm_detect_complementary(struct stm32_pwm *priv)
{
	u32 ccer;

	/*
	 * If complementary bit doesn't exist writing 1 will have no
	 * effect so we can detect it.
	 */
	regmap_update_bits(priv->regmap,
			   TIM_CCER, TIM_CCER_CC1NE, TIM_CCER_CC1NE);
	regmap_read(priv->regmap, TIM_CCER, &ccer);
	regmap_update_bits(priv->regmap, TIM_CCER, TIM_CCER_CC1NE, 0);

	priv->have_complementary_output = (ccer != 0);
}

static int stm32_pwm_detect_channels(struct stm32_pwm *priv)
{
	u32 ccer;
	int npwm = 0;

	/*
	 * If channels enable bits don't exist writing 1 will have no
	 * effect so we can detect and count them.
	 */
	regmap_update_bits(priv->regmap,
			   TIM_CCER, TIM_CCER_CCXE, TIM_CCER_CCXE);
	regmap_read(priv->regmap, TIM_CCER, &ccer);
	regmap_update_bits(priv->regmap, TIM_CCER, TIM_CCER_CCXE, 0);

	if (ccer & TIM_CCER_CC1E)
		npwm++;

	if (ccer & TIM_CCER_CC2E)
		npwm++;

	if (ccer & TIM_CCER_CC3E)
		npwm++;

	if (ccer & TIM_CCER_CC4E)
		npwm++;

	return npwm;
}

static int stm32_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct stm32_timers *ddata = dev_get_drvdata(pdev->dev.parent);
	struct stm32_pwm *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->max_arr = ddata->max_arr;

	if (!priv->regmap || !priv->clk)
		return -EINVAL;

	ret = stm32_pwm_apply_breakinputs(priv, np);
	if (ret)
		return ret;

	stm32_pwm_detect_complementary(priv);

	priv->chip.base = -1;
	priv->chip.dev = dev;
	priv->chip.ops = &stm32pwm_ops;
	priv->chip.npwm = stm32_pwm_detect_channels(priv);

	ret = pwmchip_add(&priv->chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int stm32_pwm_remove(struct platform_device *pdev)
{
	struct stm32_pwm *priv = platform_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < priv->chip.npwm; i++)
		pwm_disable(&priv->chip.pwms[i]);

	pwmchip_remove(&priv->chip);

	return 0;
}

static const struct of_device_id stm32_pwm_of_match[] = {
	{ .compatible = "st,stm32-pwm",	},
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_pwm_of_match);

static struct platform_driver stm32_pwm_driver = {
	.probe	= stm32_pwm_probe,
	.remove	= stm32_pwm_remove,
	.driver	= {
		.name = "stm32-pwm",
		.of_match_table = stm32_pwm_of_match,
	},
};
module_platform_driver(stm32_pwm_driver);

MODULE_ALIAS("platform:stm32-pwm");
MODULE_DESCRIPTION("STMicroelectronics STM32 PWM driver");
MODULE_LICENSE("GPL v2");
