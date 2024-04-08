// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2016
 *
 * Author: Gerald Baeza <gerald.baeza@st.com>
 *
 * Inspired by timer-stm32.c from Maxime Coquelin
 *             pwm-atmel.c from Bo Shen
 */

#include <linux/bitfield.h>
#include <linux/mfd/stm32-timers.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define CCMR_CHANNEL_SHIFT 8
#define CCMR_CHANNEL_MASK  0xFF
#define MAX_BREAKINPUT 2

struct stm32_breakinput {
	u32 index;
	u32 level;
	u32 filter;
};

struct stm32_pwm {
	struct mutex lock; /* protect pwm config/enable */
	struct clk *clk;
	struct regmap *regmap;
	u32 max_arr;
	bool have_complementary_output;
	struct stm32_breakinput breakinputs[MAX_BREAKINPUT];
	unsigned int num_breakinputs;
	u32 capture[4] ____cacheline_aligned; /* DMA'able buffer */
};

static inline struct stm32_pwm *to_stm32_pwm_dev(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static u32 active_channels(struct stm32_pwm *dev)
{
	u32 ccer;

	regmap_read(dev->regmap, TIM_CCER, &ccer);

	return ccer & TIM_CCER_CCXE;
}

#define TIM_CCER_CC12P (TIM_CCER_CC1P | TIM_CCER_CC2P)
#define TIM_CCER_CC12E (TIM_CCER_CC1E | TIM_CCER_CC2E)
#define TIM_CCER_CC34P (TIM_CCER_CC3P | TIM_CCER_CC4P)
#define TIM_CCER_CC34E (TIM_CCER_CC3E | TIM_CCER_CC4E)

/*
 * Capture using PWM input mode:
 *                              ___          ___
 * TI[1, 2, 3 or 4]: ........._|   |________|
 *                             ^0  ^1       ^2
 *                              .   .        .
 *                              .   .        XXXXX
 *                              .   .   XXXXX     |
 *                              .  XXXXX     .    |
 *                            XXXXX .        .    |
 * COUNTER:        ______XXXXX  .   .        .    |_XXX
 *                 start^       .   .        .        ^stop
 *                      .       .   .        .
 *                      v       v   .        v
 *                                  v
 * CCR1/CCR3:       tx..........t0...........t2
 * CCR2/CCR4:       tx..............t1.........
 *
 * DMA burst transfer:          |            |
 *                              v            v
 * DMA buffer:                  { t0, tx }   { t2, t1 }
 * DMA done:                                 ^
 *
 * 0: IC1/3 snapchot on rising edge: counter value -> CCR1/CCR3
 *    + DMA transfer CCR[1/3] & CCR[2/4] values (t0, tx: doesn't care)
 * 1: IC2/4 snapchot on falling edge: counter value -> CCR2/CCR4
 * 2: IC1/3 snapchot on rising edge: counter value -> CCR1/CCR3
 *    + DMA transfer CCR[1/3] & CCR[2/4] values (t2, t1)
 *
 * DMA done, compute:
 * - Period     = t2 - t0
 * - Duty cycle = t1 - t0
 */
static int stm32_pwm_raw_capture(struct pwm_chip *chip, struct pwm_device *pwm,
				 unsigned long tmo_ms, u32 *raw_prd,
				 u32 *raw_dty)
{
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	struct device *parent = pwmchip_parent(chip)->parent;
	enum stm32_timers_dmas dma_id;
	u32 ccen, ccr;
	int ret;

	/* Ensure registers have been updated, enable counter and capture */
	regmap_set_bits(priv->regmap, TIM_EGR, TIM_EGR_UG);
	regmap_set_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN);

	/* Use cc1 or cc3 DMA resp for PWM input channels 1 & 2 or 3 & 4 */
	dma_id = pwm->hwpwm < 2 ? STM32_TIMERS_DMA_CH1 : STM32_TIMERS_DMA_CH3;
	ccen = pwm->hwpwm < 2 ? TIM_CCER_CC12E : TIM_CCER_CC34E;
	ccr = pwm->hwpwm < 2 ? TIM_CCR1 : TIM_CCR3;
	regmap_set_bits(priv->regmap, TIM_CCER, ccen);

	/*
	 * Timer DMA burst mode. Request 2 registers, 2 bursts, to get both
	 * CCR1 & CCR2 (or CCR3 & CCR4) on each capture event.
	 * We'll get two capture snapchots: { CCR1, CCR2 }, { CCR1, CCR2 }
	 * or { CCR3, CCR4 }, { CCR3, CCR4 }
	 */
	ret = stm32_timers_dma_burst_read(parent, priv->capture, dma_id, ccr, 2,
					  2, tmo_ms);
	if (ret)
		goto stop;

	/* Period: t2 - t0 (take care of counter overflow) */
	if (priv->capture[0] <= priv->capture[2])
		*raw_prd = priv->capture[2] - priv->capture[0];
	else
		*raw_prd = priv->max_arr - priv->capture[0] + priv->capture[2];

	/* Duty cycle capture requires at least two capture units */
	if (pwm->chip->npwm < 2)
		*raw_dty = 0;
	else if (priv->capture[0] <= priv->capture[3])
		*raw_dty = priv->capture[3] - priv->capture[0];
	else
		*raw_dty = priv->max_arr - priv->capture[0] + priv->capture[3];

	if (*raw_dty > *raw_prd) {
		/*
		 * Race beetween PWM input and DMA: it may happen
		 * falling edge triggers new capture on TI2/4 before DMA
		 * had a chance to read CCR2/4. It means capture[1]
		 * contains period + duty_cycle. So, subtract period.
		 */
		*raw_dty -= *raw_prd;
	}

stop:
	regmap_clear_bits(priv->regmap, TIM_CCER, ccen);
	regmap_clear_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN);

	return ret;
}

static int stm32_pwm_capture(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_capture *result, unsigned long tmo_ms)
{
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	unsigned long long prd, div, dty;
	unsigned long rate;
	unsigned int psc = 0, icpsc, scale;
	u32 raw_prd = 0, raw_dty = 0;
	int ret = 0;

	mutex_lock(&priv->lock);

	if (active_channels(priv)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = clk_enable(priv->clk);
	if (ret) {
		dev_err(pwmchip_parent(chip), "failed to enable counter clock\n");
		goto unlock;
	}

	rate = clk_get_rate(priv->clk);
	if (!rate) {
		ret = -EINVAL;
		goto clk_dis;
	}

	/* prescaler: fit timeout window provided by upper layer */
	div = (unsigned long long)rate * (unsigned long long)tmo_ms;
	do_div(div, MSEC_PER_SEC);
	prd = div;
	while ((div > priv->max_arr) && (psc < MAX_TIM_PSC)) {
		psc++;
		div = prd;
		do_div(div, psc + 1);
	}
	regmap_write(priv->regmap, TIM_ARR, priv->max_arr);
	regmap_write(priv->regmap, TIM_PSC, psc);

	/* Reset input selector to its default input and disable slave mode */
	regmap_write(priv->regmap, TIM_TISEL, 0x0);
	regmap_write(priv->regmap, TIM_SMCR, 0x0);

	/* Map TI1 or TI2 PWM input to IC1 & IC2 (or TI3/4 to IC3 & IC4) */
	regmap_update_bits(priv->regmap,
			   pwm->hwpwm < 2 ? TIM_CCMR1 : TIM_CCMR2,
			   TIM_CCMR_CC1S | TIM_CCMR_CC2S, pwm->hwpwm & 0x1 ?
			   TIM_CCMR_CC1S_TI2 | TIM_CCMR_CC2S_TI2 :
			   TIM_CCMR_CC1S_TI1 | TIM_CCMR_CC2S_TI1);

	/* Capture period on IC1/3 rising edge, duty cycle on IC2/4 falling. */
	regmap_update_bits(priv->regmap, TIM_CCER, pwm->hwpwm < 2 ?
			   TIM_CCER_CC12P : TIM_CCER_CC34P, pwm->hwpwm < 2 ?
			   TIM_CCER_CC2P : TIM_CCER_CC4P);

	ret = stm32_pwm_raw_capture(chip, pwm, tmo_ms, &raw_prd, &raw_dty);
	if (ret)
		goto stop;

	/*
	 * Got a capture. Try to improve accuracy at high rates:
	 * - decrease counter clock prescaler, scale up to max rate.
	 * - use input prescaler, capture once every /2 /4 or /8 edges.
	 */
	if (raw_prd) {
		u32 max_arr = priv->max_arr - 0x1000; /* arbitrary margin */

		scale = max_arr / min(max_arr, raw_prd);
	} else {
		scale = priv->max_arr; /* bellow resolution, use max scale */
	}

	if (psc && scale > 1) {
		/* 2nd measure with new scale */
		psc /= scale;
		regmap_write(priv->regmap, TIM_PSC, psc);
		ret = stm32_pwm_raw_capture(chip, pwm, tmo_ms, &raw_prd,
					    &raw_dty);
		if (ret)
			goto stop;
	}

	/* Compute intermediate period not to exceed timeout at low rates */
	prd = (unsigned long long)raw_prd * (psc + 1) * NSEC_PER_SEC;
	do_div(prd, rate);

	for (icpsc = 0; icpsc < MAX_TIM_ICPSC ; icpsc++) {
		/* input prescaler: also keep arbitrary margin */
		if (raw_prd >= (priv->max_arr - 0x1000) >> (icpsc + 1))
			break;
		if (prd >= (tmo_ms * NSEC_PER_MSEC) >> (icpsc + 2))
			break;
	}

	if (!icpsc)
		goto done;

	/* Last chance to improve period accuracy, using input prescaler */
	regmap_update_bits(priv->regmap,
			   pwm->hwpwm < 2 ? TIM_CCMR1 : TIM_CCMR2,
			   TIM_CCMR_IC1PSC | TIM_CCMR_IC2PSC,
			   FIELD_PREP(TIM_CCMR_IC1PSC, icpsc) |
			   FIELD_PREP(TIM_CCMR_IC2PSC, icpsc));

	ret = stm32_pwm_raw_capture(chip, pwm, tmo_ms, &raw_prd, &raw_dty);
	if (ret)
		goto stop;

	if (raw_dty >= (raw_prd >> icpsc)) {
		/*
		 * We may fall here using input prescaler, when input
		 * capture starts on high side (before falling edge).
		 * Example with icpsc to capture on each 4 events:
		 *
		 *       start   1st capture                     2nd capture
		 *         v     v                               v
		 *         ___   _____   _____   _____   _____   ____
		 * TI1..4     |__|    |__|    |__|    |__|    |__|
		 *            v  v    .  .    .  .    .       v  v
		 * icpsc1/3:  .  0    .  1    .  2    .  3    .  0
		 * icpsc2/4:  0       1       2       3       0
		 *            v  v                            v  v
		 * CCR1/3  ......t0..............................t2
		 * CCR2/4  ..t1..............................t1'...
		 *               .                            .  .
		 * Capture0:     .<----------------------------->.
		 * Capture1:     .<-------------------------->.  .
		 *               .                            .  .
		 * Period:       .<------>                    .  .
		 * Low side:                                  .<>.
		 *
		 * Result:
		 * - Period = Capture0 / icpsc
		 * - Duty = Period - Low side = Period - (Capture0 - Capture1)
		 */
		raw_dty = (raw_prd >> icpsc) - (raw_prd - raw_dty);
	}

done:
	prd = (unsigned long long)raw_prd * (psc + 1) * NSEC_PER_SEC;
	result->period = DIV_ROUND_UP_ULL(prd, rate << icpsc);
	dty = (unsigned long long)raw_dty * (psc + 1) * NSEC_PER_SEC;
	result->duty_cycle = DIV_ROUND_UP_ULL(dty, rate);
stop:
	regmap_write(priv->regmap, TIM_CCER, 0);
	regmap_write(priv->regmap, pwm->hwpwm < 2 ? TIM_CCMR1 : TIM_CCMR2, 0);
	regmap_write(priv->regmap, TIM_PSC, 0);
clk_dis:
	clk_disable(priv->clk);
unlock:
	mutex_unlock(&priv->lock);

	return ret;
}

static int stm32_pwm_config(struct stm32_pwm *priv, unsigned int ch,
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
	regmap_set_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE);

	/* Calculate the duty cycles */
	dty = prd * duty_ns;
	do_div(dty, period_ns);

	regmap_write(priv->regmap, TIM_CCR1 + 4 * ch, dty);

	/* Configure output mode */
	shift = (ch & 0x1) * CCMR_CHANNEL_SHIFT;
	ccmr = (TIM_CCMR_PE | TIM_CCMR_M1) << shift;
	mask = CCMR_CHANNEL_MASK << shift;

	if (ch < 2)
		regmap_update_bits(priv->regmap, TIM_CCMR1, mask, ccmr);
	else
		regmap_update_bits(priv->regmap, TIM_CCMR2, mask, ccmr);

	regmap_set_bits(priv->regmap, TIM_BDTR, TIM_BDTR_MOE);

	return 0;
}

static int stm32_pwm_set_polarity(struct stm32_pwm *priv, unsigned int ch,
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

static int stm32_pwm_enable(struct stm32_pwm *priv, unsigned int ch)
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

	regmap_set_bits(priv->regmap, TIM_CCER, mask);

	/* Make sure that registers are updated */
	regmap_set_bits(priv->regmap, TIM_EGR, TIM_EGR_UG);

	/* Enable controller */
	regmap_set_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN);

	return 0;
}

static void stm32_pwm_disable(struct stm32_pwm *priv, unsigned int ch)
{
	u32 mask;

	/* Disable channel */
	mask = TIM_CCER_CC1E << (ch * 4);
	if (priv->have_complementary_output)
		mask |= TIM_CCER_CC1NE << (ch * 4);

	regmap_clear_bits(priv->regmap, TIM_CCER, mask);

	/* When all channels are disabled, we can disable the controller */
	if (!active_channels(priv))
		regmap_clear_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN);

	clk_disable(priv->clk);
}

static int stm32_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
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

static int stm32_pwm_apply_locked(struct pwm_chip *chip, struct pwm_device *pwm,
				  const struct pwm_state *state)
{
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	int ret;

	/* protect common prescaler for all active channels */
	mutex_lock(&priv->lock);
	ret = stm32_pwm_apply(chip, pwm, state);
	mutex_unlock(&priv->lock);

	return ret;
}

static int stm32_pwm_get_state(struct pwm_chip *chip,
			       struct pwm_device *pwm, struct pwm_state *state)
{
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	int ch = pwm->hwpwm;
	unsigned long rate;
	u32 ccer, psc, arr, ccr;
	u64 dty, prd;
	int ret;

	mutex_lock(&priv->lock);

	ret = regmap_read(priv->regmap, TIM_CCER, &ccer);
	if (ret)
		goto out;

	state->enabled = ccer & (TIM_CCER_CC1E << (ch * 4));
	state->polarity = (ccer & (TIM_CCER_CC1P << (ch * 4))) ?
			  PWM_POLARITY_INVERSED : PWM_POLARITY_NORMAL;
	ret = regmap_read(priv->regmap, TIM_PSC, &psc);
	if (ret)
		goto out;
	ret = regmap_read(priv->regmap, TIM_ARR, &arr);
	if (ret)
		goto out;
	ret = regmap_read(priv->regmap, TIM_CCR1 + 4 * ch, &ccr);
	if (ret)
		goto out;

	rate = clk_get_rate(priv->clk);

	prd = (u64)NSEC_PER_SEC * (psc + 1) * (arr + 1);
	state->period = DIV_ROUND_UP_ULL(prd, rate);
	dty = (u64)NSEC_PER_SEC * (psc + 1) * ccr;
	state->duty_cycle = DIV_ROUND_UP_ULL(dty, rate);

out:
	mutex_unlock(&priv->lock);
	return ret;
}

static const struct pwm_ops stm32pwm_ops = {
	.apply = stm32_pwm_apply_locked,
	.get_state = stm32_pwm_get_state,
	.capture = IS_ENABLED(CONFIG_DMA_ENGINE) ? stm32_pwm_capture : NULL,
};

static int stm32_pwm_set_breakinput(struct stm32_pwm *priv,
				    const struct stm32_breakinput *bi)
{
	u32 shift = TIM_BDTR_BKF_SHIFT(bi->index);
	u32 bke = TIM_BDTR_BKE(bi->index);
	u32 bkp = TIM_BDTR_BKP(bi->index);
	u32 bkf = TIM_BDTR_BKF(bi->index);
	u32 mask = bkf | bkp | bke;
	u32 bdtr;

	bdtr = (bi->filter & TIM_BDTR_BKF_MASK) << shift | bke;

	if (bi->level)
		bdtr |= bkp;

	regmap_update_bits(priv->regmap, TIM_BDTR, mask, bdtr);

	regmap_read(priv->regmap, TIM_BDTR, &bdtr);

	return (bdtr & bke) ? 0 : -EINVAL;
}

static int stm32_pwm_apply_breakinputs(struct stm32_pwm *priv)
{
	unsigned int i;
	int ret;

	for (i = 0; i < priv->num_breakinputs; i++) {
		ret = stm32_pwm_set_breakinput(priv, &priv->breakinputs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int stm32_pwm_probe_breakinputs(struct stm32_pwm *priv,
				       struct device_node *np)
{
	int nb, ret, array_size;
	unsigned int i;

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

	priv->num_breakinputs = nb;
	array_size = nb * sizeof(struct stm32_breakinput) / sizeof(u32);
	ret = of_property_read_u32_array(np, "st,breakinput",
					 (u32 *)priv->breakinputs, array_size);
	if (ret)
		return ret;

	for (i = 0; i < priv->num_breakinputs; i++) {
		if (priv->breakinputs[i].index > 1 ||
		    priv->breakinputs[i].level > 1 ||
		    priv->breakinputs[i].filter > 15)
			return -EINVAL;
	}

	return stm32_pwm_apply_breakinputs(priv);
}

static void stm32_pwm_detect_complementary(struct stm32_pwm *priv)
{
	u32 ccer;

	/*
	 * If complementary bit doesn't exist writing 1 will have no
	 * effect so we can detect it.
	 */
	regmap_set_bits(priv->regmap, TIM_CCER, TIM_CCER_CC1NE);
	regmap_read(priv->regmap, TIM_CCER, &ccer);
	regmap_clear_bits(priv->regmap, TIM_CCER, TIM_CCER_CC1NE);

	priv->have_complementary_output = (ccer != 0);
}

static unsigned int stm32_pwm_detect_channels(struct regmap *regmap,
					      unsigned int *num_enabled)
{
	u32 ccer, ccer_backup;

	/*
	 * If channels enable bits don't exist writing 1 will have no
	 * effect so we can detect and count them.
	 */
	regmap_read(regmap, TIM_CCER, &ccer_backup);
	regmap_set_bits(regmap, TIM_CCER, TIM_CCER_CCXE);
	regmap_read(regmap, TIM_CCER, &ccer);
	regmap_write(regmap, TIM_CCER, ccer_backup);

	*num_enabled = hweight32(ccer_backup & TIM_CCER_CCXE);

	return hweight32(ccer & TIM_CCER_CCXE);
}

static int stm32_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct stm32_timers *ddata = dev_get_drvdata(pdev->dev.parent);
	struct pwm_chip *chip;
	struct stm32_pwm *priv;
	unsigned int npwm, num_enabled;
	unsigned int i;
	int ret;

	npwm = stm32_pwm_detect_channels(ddata->regmap, &num_enabled);

	chip = devm_pwmchip_alloc(dev, npwm, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = to_stm32_pwm_dev(chip);

	mutex_init(&priv->lock);
	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->max_arr = ddata->max_arr;

	if (!priv->regmap || !priv->clk)
		return -EINVAL;

	ret = stm32_pwm_probe_breakinputs(priv, np);
	if (ret)
		return ret;

	stm32_pwm_detect_complementary(priv);

	chip->ops = &stm32pwm_ops;

	/* Initialize clock refcount to number of enabled PWM channels. */
	for (i = 0; i < num_enabled; i++)
		clk_enable(priv->clk);

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, chip);

	return 0;
}

static int stm32_pwm_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	unsigned int i;
	u32 ccer, mask;

	/* Look for active channels */
	ccer = active_channels(priv);

	for (i = 0; i < chip->npwm; i++) {
		mask = TIM_CCER_CC1E << (i * 4);
		if (ccer & mask) {
			dev_err(dev, "PWM %u still in use by consumer %s\n",
				i, chip->pwms[i].label);
			return -EBUSY;
		}
	}

	return pinctrl_pm_select_sleep_state(dev);
}

static int stm32_pwm_resume(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct stm32_pwm *priv = to_stm32_pwm_dev(chip);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret)
		return ret;

	/* restore breakinput registers that may have been lost in low power */
	return stm32_pwm_apply_breakinputs(priv);
}

static DEFINE_SIMPLE_DEV_PM_OPS(stm32_pwm_pm_ops, stm32_pwm_suspend, stm32_pwm_resume);

static const struct of_device_id stm32_pwm_of_match[] = {
	{ .compatible = "st,stm32-pwm",	},
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_pwm_of_match);

static struct platform_driver stm32_pwm_driver = {
	.probe	= stm32_pwm_probe,
	.driver	= {
		.name = "stm32-pwm",
		.of_match_table = stm32_pwm_of_match,
		.pm = pm_ptr(&stm32_pwm_pm_ops),
	},
};
module_platform_driver(stm32_pwm_driver);

MODULE_ALIAS("platform:stm32-pwm");
MODULE_DESCRIPTION("STMicroelectronics STM32 PWM driver");
MODULE_LICENSE("GPL v2");
